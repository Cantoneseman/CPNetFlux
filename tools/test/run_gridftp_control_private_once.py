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


def ssh_prefix(remote: str) -> list[str]:
    if os.environ.get("GRIDFLUX_SSH_PASSWORD"):
        env_value = os.environ["GRIDFLUX_SSH_PASSWORD"]
        os.environ["SSHPASS"] = env_value
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no", remote]
    return ["ssh", "-o", "StrictHostKeyChecking=no", remote]


def run_ssh(remote: str, command: str, *, input_text: str | None = None,
            check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ssh_prefix(remote) + [command],
        input=input_text,
        text=True,
        capture_output=True,
        check=check,
    )


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def make_remote_file(remote: str, path: str, total_bytes: int) -> None:
    script = """
import sys
path = sys.argv[1]
remaining = int(sys.argv[2])
block = bytes(index % 251 for index in range(1024 * 1024))
with open(path, "wb") as handle:
    while remaining > 0:
        size = min(remaining, len(block))
        handle.write(block[:size])
        remaining -= size
"""
    run_ssh(remote, f"python3 - {shlex.quote(path)} {total_bytes}", input_text=script)


def remote_sha256(remote: str, path: str) -> str:
    result = run_ssh(remote, f"sha256sum {shlex.quote(path)}")
    return result.stdout.split()[0]


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


def connect_control(host: str, port: int) -> tuple[socket.socket, bytearray, list[str]]:
    deadline = time.monotonic() + 10.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection((host, port), timeout=2.0)
            buffer = bytearray()
            greeting = read_reply(sock, buffer)
            return sock, buffer, greeting
        except OSError as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"failed to connect control server: {last_error}")


def login_and_type(host: str, port: int, args: argparse.Namespace) -> tuple[socket.socket, bytearray]:
    sock, buffer, greeting = connect_control(host, port)
    assert reply_code(greeting) == 220, greeting
    if args.auth_mode == "token":
        token = Path(args.auth_token_file).read_text(encoding="utf-8").rstrip("\r\n")
        assert reply_code(send_command(sock, buffer, "USER token")) == 331
        assert reply_code(send_command(sock, buffer, "PASS " + token)) == 230
    else:
        assert reply_code(send_command(sock, buffer, "USER gridflux")) == 331
        assert reply_code(send_command(sock, buffer, "PASS gridflux")) == 230
    assert reply_code(send_command(sock, buffer, "TYPE I")) == 200
    return sock, buffer


def run_remote_client(args: argparse.Namespace, source: str, data_port: int, transfer_id: str,
                      *, resume: bool, max_chunks: int | None) -> subprocess.CompletedProcess[str]:
    remote_client = f"{args.remote_build_dir.rstrip('/')}/gridflux-file-client"
    pieces = [
        shlex.quote(remote_client),
        "--host",
        shlex.quote(args.server_host),
        "--port",
        str(data_port),
        "--input",
        shlex.quote(source),
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
        shlex.quote(transfer_id),
    ]
    if resume:
        pieces.append("--resume")
    if max_chunks is not None:
        pieces.extend(["--max-chunks", str(max_chunks)])
    return run_ssh(args.remote, " ".join(pieces), check=False)


def run_control_stor(args: argparse.Namespace, source: str, remote_sha: str, name: str) -> str:
    sock, buffer = login_and_type(args.server_host, args.port, args)
    with sock:
        epsv = send_command(sock, buffer, "EPSV")
        assert reply_code(epsv) == 229, epsv
        data_port = parse_epsv_port(epsv)
        stor = send_command(sock, buffer, f"STOR {name}")
        assert reply_code(stor) == 150, stor
        transfer_id = parse_transfer_id(stor)
        client = run_remote_client(args, source, data_port, transfer_id, resume=False, max_chunks=None)
        assert client.returncode == 0, client.stdout + client.stderr
        complete = read_reply(sock, buffer)
        assert reply_code(complete) == 226, complete
        assert reply_code(send_command(sock, buffer, "QUIT")) == 221
    dest = Path(args.root) / name
    if remote_sha != sha256_file(dest):
        raise RuntimeError(f"sha256 mismatch for {name}")
    return transfer_id


def run_control_resume(args: argparse.Namespace, source: str, remote_sha: str, name: str) -> str:
    sock, buffer = login_and_type(args.server_host, args.port, args)
    with sock:
        epsv = send_command(sock, buffer, "EPSV")
        assert reply_code(epsv) == 229, epsv
        data_port = parse_epsv_port(epsv)
        stor = send_command(sock, buffer, f"STOR {name}")
        assert reply_code(stor) == 150, stor
        transfer_id = parse_transfer_id(stor)
        partial = run_remote_client(args, source, data_port, transfer_id, resume=False,
                                    max_chunks=args.max_chunks)
        assert partial.returncode != 0, partial.stdout + partial.stderr
        failed = read_reply(sock, buffer)
        assert reply_code(failed) == 550, failed

    dest = Path(args.root) / name
    manifest = Path(f"{dest}.gridflux.manifest")
    partial_path = Path(f"{dest}.part.{transfer_id}")
    assert not dest.exists()
    assert manifest.exists()
    assert partial_path.exists()

    sock, buffer = login_and_type(args.server_host, args.port, args)
    with sock:
        rest = send_command(sock, buffer, f"REST GFID:{transfer_id}")
        assert reply_code(rest) == 350, rest
        epsv = send_command(sock, buffer, "EPSV")
        assert reply_code(epsv) == 229, epsv
        data_port = parse_epsv_port(epsv)
        stor = send_command(sock, buffer, f"STOR {name}")
        assert reply_code(stor) == 150, stor
        assert parse_transfer_id(stor) == transfer_id
        resumed = run_remote_client(args, source, data_port, transfer_id, resume=True, max_chunks=None)
        assert resumed.returncode == 0, resumed.stdout + resumed.stderr
        complete = read_reply(sock, buffer)
        assert reply_code(complete) == 226, complete
        assert reply_code(send_command(sock, buffer, "QUIT")) == 221
    if remote_sha != sha256_file(dest):
        raise RuntimeError(f"sha256 mismatch for resumed {name}")
    return transfer_id


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private GridFTP control STOR/resume smoke.")
    parser.add_argument("--remote", default="root@<redacted>")
    parser.add_argument("--server-host", default="<redacted>")
    parser.add_argument("--local-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--remote-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--root", default="/tmp/gridflux-gridftp-private-root")
    parser.add_argument("--port", type=int, default=2121)
    parser.add_argument("--data-port-base", type=int, default=20300)
    parser.add_argument("--connections", type=int, default=4)
    parser.add_argument("--bytes", type=int, default=64 * 1024 * 1024)
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--checksum", choices=["crc32c", "none"], default="crc32c")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    parser.add_argument("--auth-mode", choices=["anonymous", "token"], default="anonymous")
    parser.add_argument("--auth-token-file", default="")
    parser.add_argument("--max-chunks", type=int, default=4)
    parser.add_argument("--output-dir", default="tools/perf/results")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    log_path = output_dir / f"{timestamp}_gridftp_control_private.log"
    server_bin = f"{args.local_build_dir.rstrip('/')}/gridflux-gridftp-server"
    remote_source = f"/tmp/{timestamp}_gridftp_control_private.src"

    Path(args.root).mkdir(parents=True, exist_ok=True)
    for path in Path(args.root).glob("private-*"):
        if path.is_file():
            path.unlink()
    make_remote_file(args.remote, remote_source, args.bytes)
    remote_sha = remote_sha256(args.remote, remote_source)

    server_cmd = [
        server_bin,
        "--host",
        args.server_host,
        "--port",
        str(args.port),
        "--root",
        args.root,
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
    with log_path.open("w", encoding="utf-8") as log_handle:
        server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)
    try:
        full_id = run_control_stor(args, remote_source, remote_sha, "private-stor.bin")
        resume_id = run_control_resume(args, remote_source, remote_sha, "private-resume.bin")
        text = log_path.read_text(encoding="utf-8")
        if "skipped_bytes=" not in text or "verified_bytes=" not in text:
            raise RuntimeError(f"private server log missing resume stats: {log_path}")
        print(f"gridftp private STOR passed transfer_id={full_id}")
        print(f"gridftp private REST resume passed transfer_id={resume_id}")
        print(f"server_log={log_path}")
        print(f"source_sha256={remote_sha}")
        return 0
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        run_ssh(args.remote, f"rm -f {shlex.quote(remote_source)}", check=False)


if __name__ == "__main__":
    sys.exit(main())
