#!/usr/bin/env python3
import argparse
import hashlib
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from gridftp_port_window import assert_epsv_port_in_window, clamp_passive_data_port_base


def read_line(sock: socket.socket, buffer: bytearray) -> str:
    while b"\n" not in buffer:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("control connection closed")
        buffer.extend(chunk)
    index = buffer.index(ord("\n"))
    line = bytes(buffer[: index + 1]).decode("utf-8", errors="replace").rstrip("\r\n")
    del buffer[: index + 1]
    return line


def read_reply(sock: socket.socket, buffer: bytearray) -> list[str]:
    first = read_line(sock, buffer)
    lines = [first]
    if len(first) >= 4 and first[:3].isdigit() and first[3] == "-":
        expected = first[:3] + " "
        while True:
            line = read_line(sock, buffer)
            lines.append(line)
            if line.startswith(expected):
                break
    return lines


def reply_code(lines: list[str]) -> int:
    return int(lines[0][:3]) if lines and len(lines[0]) >= 3 and lines[0][:3].isdigit() else 0


def send_command(sock: socket.socket, buffer: bytearray, command: str) -> list[str]:
    sock.sendall((command + "\r\n").encode("utf-8"))
    return read_reply(sock, buffer)


def parse_epsv_port(lines: list[str]) -> int:
    match = re.search(r"\(\|\|\|(\d+)\|\)", "\n".join(lines))
    if not match:
        raise RuntimeError(f"failed to parse EPSV port from {lines!r}")
    return int(match.group(1))


def parse_transfer_id(lines: list[str]) -> str:
    match = re.search(r"transfer_id=GFID:([A-Za-z0-9._-]+)", "\n".join(lines))
    if not match:
        raise RuntimeError(f"failed to parse transfer id from {lines!r}")
    return match.group(1)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def ssh_prefix() -> list[str]:
    if os.environ.get("CPNETFLUX_SSH_PASSWORD"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no"]
    return ["ssh", "-o", "StrictHostKeyChecking=no"]


def run_remote(remote: str, command: str) -> str:
    env = os.environ.copy()
    if env.get("CPNETFLUX_SSH_PASSWORD") and not env.get("SSHPASS"):
        env["SSHPASS"] = env["CPNETFLUX_SSH_PASSWORD"]
    completed = subprocess.run(
        [*ssh_prefix(), remote, command],
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return completed.stdout


def connect_control(host: str, port: int) -> tuple[socket.socket, bytearray, list[str]]:
    deadline = time.monotonic() + 15.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((host, port), timeout=3.0)
            buffer = bytearray()
            greeting = read_reply(sock, buffer)
            return sock, buffer, greeting
        except OSError as error:
            last_error = error
            time.sleep(0.1)
    raise RuntimeError(f"failed to connect control server: {last_error}")


def login_and_epsv(host: str, port: int, args: argparse.Namespace) -> tuple[socket.socket, bytearray, int]:
    sock, buffer, greeting = connect_control(host, port)
    assert reply_code(greeting) == 220, greeting
    if args.auth_mode == "token":
        token = Path(args.auth_token_file).read_text(encoding="utf-8").rstrip("\r\n")
        assert reply_code(send_command(sock, buffer, "USER token")) == 331
        assert reply_code(send_command(sock, buffer, "PASS " + token)) == 230
    else:
        assert reply_code(send_command(sock, buffer, "USER cpnetflux")) == 331
        assert reply_code(send_command(sock, buffer, "PASS cpnetflux")) == 230
    assert reply_code(send_command(sock, buffer, "TYPE I")) == 200
    epsv = send_command(sock, buffer, "EPSV")
    assert reply_code(epsv) == 229, epsv
    data_port = parse_epsv_port(epsv)
    assert_epsv_port_in_window(data_port, args.data_port_base)
    return sock, buffer, data_port


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private GridFTP control RETR smoke.")
    parser.add_argument("--remote", default="root@<redacted>")
    parser.add_argument("--server-host", default="<redacted>")
    parser.add_argument("--control-port", type=int, default=2121)
    parser.add_argument("--root", default="/tmp/cpnetflux-gridftp-retr-private-root")
    parser.add_argument("--local-build-dir", default="/root/projects/CPNetFlux/build")
    parser.add_argument("--remote-build-dir", default="/root/projects/CPNetFlux/build")
    parser.add_argument("--download-output", default="/tmp/cpnetflux-gridftp-private-retr.bin")
    parser.add_argument("--connections", type=int, default=4)
    parser.add_argument("--bytes", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--checksum", choices=["crc32c", "none"], default="crc32c")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    parser.add_argument("--auth-mode", choices=["anonymous", "token"], default="anonymous")
    parser.add_argument("--auth-token-file", default="")
    parser.add_argument("--data-port-base", type=int, default=20300)
    parser.add_argument("--output-dir", default="tools/perf/results")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--max-chunks", type=int, default=8)
    args = parser.parse_args()
    args.data_port_base = clamp_passive_data_port_base(args.data_port_base)

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    server_log = output_dir / f"{timestamp}_gridftp_control_retr_private.log"

    server_bin = f"{args.local_build_dir.rstrip('/')}/cpnetflux-gridftp-server"
    client_bin = f"{args.remote_build_dir.rstrip('/')}/cpnetflux-file-download-client"
    root = Path(args.root)
    source_name = "private-retr-source.bin"
    source_path = root / source_name
    with tempfile.TemporaryDirectory(prefix="cpnetflux-private-retr.") as temp_text:
        temp_dir = Path(temp_text)
        local_source = temp_dir / source_name
        block = bytes((index * 31) % 251 for index in range(1024 * 1024))
        remaining = args.bytes
        with local_source.open("wb") as handle:
            while remaining > 0:
                size = min(remaining, len(block))
                handle.write(block[:size])
                remaining -= size
        source_sha = sha256_file(local_source)

        subprocess.run(["rm", "-rf", str(root)], check=True)
        root.mkdir(parents=True)
        subprocess.run(["cp", str(local_source), str(source_path)], check=True)
        run_remote(args.remote, f"test -x {client_bin}")
        run_remote(
            args.remote,
            f"rm -f {args.download_output} {args.download_output}.part.* "
            f"{args.download_output}.cpnetflux.download.manifest",
        )

        server_cmd = [
            server_bin,
            "--host",
            "0.0.0.0",
            "--port",
            str(args.control_port),
            "--root",
            str(root),
            "--data-port-base",
            str(args.data_port_base),
            "--connections",
            str(args.connections),
            "--chunk-size",
            str(args.chunk_size),
            "--buffer-size",
            str(args.buffer_size),
            "--checksum",
            args.checksum,
            "--checksum-backend",
            args.checksum_backend,
        ]
        if args.auth_mode == "token":
            server_cmd.extend(["--auth-mode", "token", "--auth-token-file", args.auth_token_file])
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

        try:
            sock, buffer, data_port = login_and_epsv(args.server_host, args.control_port, args)
            with sock:
                retr = send_command(sock, buffer, f"RETR {source_name}")
                assert reply_code(retr) == 150, retr
                transfer_id = parse_transfer_id(retr)
                base_remote_cmd = (
                    f"{client_bin} --host {args.server_host} --port {data_port} "
                    f"--output {args.download_output} --connections {args.connections} "
                    f"--buffer-size {args.buffer_size} --checksum {args.checksum} "
                    f"--checksum-backend {args.checksum_backend} --transfer-id {transfer_id}"
                )
                if args.resume:
                    partial = subprocess.run(
                        [*ssh_prefix(), args.remote, base_remote_cmd + f" --max-chunks {args.max_chunks}"],
                        text=True,
                        capture_output=True,
                        check=False,
                        env={**os.environ, "SSHPASS": os.environ.get("CPNETFLUX_SSH_PASSWORD", "")},
                    )
                    if partial.returncode == 0:
                        raise RuntimeError("partial private RETR unexpectedly succeeded")
                    failed = read_reply(sock, buffer)
                    assert reply_code(failed) == 550, failed
                else:
                    run_remote(args.remote, base_remote_cmd)
                    complete = read_reply(sock, buffer)
                    assert reply_code(complete) == 226, complete
                    assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            if args.resume:
                sock, buffer, data_port = login_and_epsv(args.server_host, args.control_port, args)
                with sock:
                    assert reply_code(send_command(sock, buffer, f"REST GFID:{transfer_id}")) == 350
                    retr = send_command(sock, buffer, f"RETR {source_name}")
                    assert reply_code(retr) == 150, retr
                    resume_cmd = (
                        f"{client_bin} --host {args.server_host} --port {data_port} "
                        f"--output {args.download_output} --connections {args.connections} "
                        f"--buffer-size {args.buffer_size} --checksum {args.checksum} "
                        f"--checksum-backend {args.checksum_backend} --transfer-id {transfer_id} "
                        f"--resume"
                    )
                    resume_out = run_remote(args.remote, resume_cmd)
                    if "skipped_bytes=" not in resume_out or "resent_bytes=" not in resume_out:
                        raise RuntimeError(f"missing resume stats: {resume_out}")
                    complete = read_reply(sock, buffer)
                    assert reply_code(complete) == 226, complete
                    assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            remote_sha = run_remote(args.remote, f"sha256sum {args.download_output}").split()[0]
            if remote_sha != source_sha:
                raise RuntimeError(f"sha256 mismatch: {source_sha} != {remote_sha}")
            mode = "resume" if args.resume else "full"
            print(f"gridftp private RETR {mode} passed transfer_id={transfer_id}")
            print(f"source_sha256={source_sha}")
            print(f"dest_sha256={remote_sha}")
            print(f"server_log={server_log}")
            return 0
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()


if __name__ == "__main__":
    sys.exit(main())
