#!/usr/bin/env python3
import argparse
import hashlib
import os
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from gridftp_port_window import assert_epsv_port_in_window, clamp_passive_data_port_base


TOKEN = os.environ.get("CPNETFLUX_TEST_TOKEN", "")
if not TOKEN:
    raise RuntimeError("CPNETFLUX_TEST_TOKEN must be set for token-auth smoke tests")


def make_file(path: Path, total_bytes: int) -> None:
    block = bytes((index * 23) % 251 for index in range(1024 * 1024))
    remaining = total_bytes
    with path.open("wb") as handle:
        while remaining > 0:
            size = min(remaining, len(block))
            handle.write(block[:size])
            remaining -= size


def make_remote_file(remote: str, path: str, total_bytes: int) -> None:
    script = """
import sys
path = sys.argv[1]
remaining = int(sys.argv[2])
block = bytes((index * 23) % 251 for index in range(1024 * 1024))
with open(path, "wb") as handle:
    while remaining > 0:
        size = min(remaining, len(block))
        handle.write(block[:size])
        remaining -= size
"""
    run_remote(remote, f"python3 - {shlex.quote(path)} {total_bytes}", input_text=script)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def remote_sha256(remote: str, path: str) -> str:
    completed = run_remote(remote, f"sha256sum {shlex.quote(path)}")
    return completed.stdout.split()[0]


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


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


def ssh_prefix() -> list[str]:
    if os.environ.get("CPNETFLUX_SSH_PASSWORD"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no"]
    return ["ssh", "-o", "StrictHostKeyChecking=no"]


def run_remote(
    remote: str,
    command: str,
    *,
    input_text: str | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env.get("CPNETFLUX_SSH_PASSWORD") and not env.get("SSHPASS"):
        env["SSHPASS"] = env["CPNETFLUX_SSH_PASSWORD"]
    completed = subprocess.run(
        ssh_prefix() + [remote, command],
        input=input_text,
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )
    if check and completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return completed


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


def login(host: str, port: int) -> tuple[socket.socket, bytearray]:
    sock, buffer, greeting = connect_control(host, port)
    if reply_code(greeting) != 220:
        raise RuntimeError(f"unexpected greeting: {greeting!r}")
    if reply_code(send_command(sock, buffer, "USER token")) != 331:
        raise RuntimeError("USER token rejected")
    if reply_code(send_command(sock, buffer, "PASS " + TOKEN)) != 230:
        raise RuntimeError("PASS token rejected")
    if reply_code(send_command(sock, buffer, "TYPE I")) != 200:
        raise RuntimeError("TYPE I rejected")
    return sock, buffer


def assert_no_secret_leak(*paths: Path) -> None:
    pass_pattern = re.compile(r"PASS\s+\S+", re.IGNORECASE)
    for path in paths:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if TOKEN in text or "wrong-token" in text or pass_pattern.search(text):
            raise RuntimeError(f"secret leaked in {path}")


def run_smoke(args: argparse.Namespace) -> int:
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    server_log = output_dir / f"{timestamp}_gridftp_control_token_private.log"
    event_log = output_dir / f"{timestamp}_gridftp_control_token_private_events.jsonl"
    remote_source = f"/tmp/{timestamp}_gridftp_control_token_private_source.bin"
    remote_download = f"/tmp/{timestamp}_gridftp_control_token_private_download.bin"

    with tempfile.TemporaryDirectory(prefix="cpnetflux-gridftp-token-private.") as temp_text:
        temp = Path(temp_text)
        root = temp / "root"
        root.mkdir()
        source = temp / "source.bin"
        token_file = temp / "token.txt"
        make_file(source, args.bytes)
        expected_sha = sha256_file(source)
        token_file.write_text(TOKEN + "\n", encoding="utf-8")
        token_file.chmod(0o600)

        server_bin = Path(args.local_build_dir) / "cpnetflux-gridftp-server"
        upload_bin = f"{args.remote_build_dir.rstrip('/')}/cpnetflux-file-client"
        download_bin = f"{args.remote_build_dir.rstrip('/')}/cpnetflux-file-download-client"
        if not server_bin.exists():
            raise FileNotFoundError(f"missing server binary: {server_bin}")

        server_cmd = [
            str(server_bin),
            "--host",
            "0.0.0.0",
            "--port",
            str(args.control_port),
            "--root",
            str(root),
            "--data-port-base",
            str(args.data_port_base),
            "--auth-mode",
            "token",
            "--auth-token-file",
            str(token_file),
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
            "--event-log",
            str(event_log),
        ]
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

        try:
            run_remote(
                args.remote,
                f"test -x {shlex.quote(upload_bin)} && test -x {shlex.quote(download_bin)}",
            )
            make_remote_file(args.remote, remote_source, args.bytes)
            remote_source_sha = remote_sha256(args.remote, remote_source)
            if remote_source_sha != expected_sha:
                raise RuntimeError(
                    f"remote source sha256 mismatch: {remote_source_sha} != {expected_sha}"
                )

            sock, buffer = login(args.server_host, args.control_port)
            with sock:
                epsv = send_command(sock, buffer, "EPSV")
                if reply_code(epsv) != 229:
                    raise RuntimeError(f"EPSV rejected: {epsv!r}")
                data_port = parse_epsv_port(epsv)
                assert_epsv_port_in_window(data_port, args.data_port_base)

                stor = send_command(sock, buffer, "STOR uploaded.bin")
                if reply_code(stor) != 150:
                    raise RuntimeError(f"STOR rejected: {stor!r}")
                upload_transfer_id = parse_transfer_id(stor)
                run_remote(
                    args.remote,
                    " ".join(
                        [
                            shlex.quote(upload_bin),
                            "--host",
                            shlex.quote(args.server_host),
                            "--port",
                            str(data_port),
                            "--input",
                            shlex.quote(remote_source),
                            "--connections",
                            str(args.connections),
                            "--chunk-size",
                            str(args.chunk_size),
                            "--buffer-size",
                            str(args.buffer_size),
                            "--checksum",
                            shlex.quote(args.checksum),
                            "--checksum-backend",
                            shlex.quote(args.checksum_backend),
                            "--transfer-id",
                            shlex.quote(upload_transfer_id),
                        ]
                    ),
                )
                if reply_code(read_reply(sock, buffer)) != 226:
                    raise RuntimeError("STOR did not complete")

                uploaded_path = root / "uploaded.bin"
                if sha256_file(uploaded_path) != expected_sha:
                    raise RuntimeError("uploaded SHA256 mismatch")

                epsv = send_command(sock, buffer, "EPSV")
                if reply_code(epsv) != 229:
                    raise RuntimeError(f"EPSV rejected: {epsv!r}")
                data_port = parse_epsv_port(epsv)
                assert_epsv_port_in_window(data_port, args.data_port_base)

                retr = send_command(sock, buffer, "RETR uploaded.bin")
                if reply_code(retr) != 150:
                    raise RuntimeError(f"RETR rejected: {retr!r}")
                download_transfer_id = parse_transfer_id(retr)
                run_remote(
                    args.remote,
                    " ".join(
                        [
                            shlex.quote(download_bin),
                            "--host",
                            shlex.quote(args.server_host),
                            "--port",
                            str(data_port),
                            "--output",
                            shlex.quote(remote_download),
                            "--connections",
                            str(args.connections),
                            "--buffer-size",
                            str(args.buffer_size),
                            "--checksum",
                            shlex.quote(args.checksum),
                            "--checksum-backend",
                            shlex.quote(args.checksum_backend),
                            "--transfer-id",
                            shlex.quote(download_transfer_id),
                        ]
                    ),
                )
                if reply_code(read_reply(sock, buffer)) != 226:
                    raise RuntimeError("RETR did not complete")
                if reply_code(send_command(sock, buffer, "QUIT")) != 221:
                    raise RuntimeError("QUIT rejected")

            remote_download_sha = remote_sha256(args.remote, remote_download)
            if remote_download_sha != expected_sha:
                raise RuntimeError(
                    f"remote download sha256 mismatch: {remote_download_sha} != {expected_sha}"
                )
            assert_no_secret_leak(server_log, event_log)
            print(
                "gridftp private token auth STOR/RETR smoke passed "
                f"upload_transfer_id={upload_transfer_id} "
                f"download_transfer_id={download_transfer_id}"
            )
            print(f"server_log={server_log}")
            print(f"source_sha256={expected_sha}")
            print(f"dest_sha256={remote_download_sha}")
            return 0
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            if server.returncode not in (0, -15, -9):
                print(server_log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
            run_remote(
                args.remote,
                f"rm -f {shlex.quote(remote_source)} {shlex.quote(remote_download)} "
                f"{shlex.quote(remote_download)}.part.* "
                f"{shlex.quote(remote_download)}.cpnetflux.download.manifest",
                check=False,
            )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private GridFTP token auth smoke.")
    parser.add_argument("--remote", required=True)
    parser.add_argument("--server-host", required=True)
    parser.add_argument("--control-port", type=int, default=2121)
    parser.add_argument("--data-port-base", type=int, default=20300)
    parser.add_argument("--local-build-dir", default="/root/projects/CPNetFlux-Beta/build")
    parser.add_argument("--remote-build-dir", default="/root/projects/CPNetFlux-Beta/build")
    parser.add_argument("--root", default="/tmp/cpnetflux-gridftp-token-private-root")
    parser.add_argument("--connections", type=int, default=1)
    parser.add_argument("--bytes", type=int, default=256 * 1024)
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--checksum", choices=["crc32c", "none"], default="crc32c")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    parser.add_argument("--output-dir", "--results-dir", dest="output_dir", default="tools/perf/results")
    args = parser.parse_args()
    args.data_port_base = clamp_passive_data_port_base(args.data_port_base)
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
