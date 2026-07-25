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


def make_file(path: Path, total_bytes: int) -> None:
    block = bytes(index % 251 for index in range(1024 * 1024))
    remaining = total_bytes
    with path.open("wb") as handle:
        while remaining > 0:
            size = min(remaining, len(block))
            handle.write(block[:size])
            remaining -= size


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


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
    if not lines or len(lines[0]) < 3 or not lines[0][:3].isdigit():
        return 0
    return int(lines[0][:3])


def send_command(sock: socket.socket, buffer: bytearray, command: str) -> list[str]:
    sock.sendall((command + "\r\n").encode("utf-8"))
    return read_reply(sock, buffer)


def parse_epsv_port(lines: list[str]) -> int:
    text = "\n".join(lines)
    match = re.search(r"\(\|\|\|(\d+)\|\)", text)
    if not match:
        raise RuntimeError(f"failed to parse EPSV port from {text!r}")
    return int(match.group(1))


def parse_transfer_id(lines: list[str]) -> str:
    text = "\n".join(lines)
    match = re.search(r"transfer_id=GFID:([A-Za-z0-9._-]+)", text)
    if not match:
        raise RuntimeError(f"failed to parse transfer id from {text!r}")
    return match.group(1)


def connect_control(port: int) -> tuple[socket.socket, bytearray, list[str]]:
    deadline = time.monotonic() + 10.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            sock = socket.create_connection(("127.0.0.1", port), timeout=2.0)
            buffer = bytearray()
            greeting = read_reply(sock, buffer)
            return sock, buffer, greeting
        except OSError as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"failed to connect control server: {last_error}")


def run_smoke(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir)
    server_bin = build_dir / "gridflux-gridftp-server"
    client_bin = build_dir / "gridflux-file-client"
    if not server_bin.exists() or not client_bin.exists():
        raise FileNotFoundError(f"missing gridftp server or file client in {build_dir}")

    control_port = free_port()
    data_port_base = free_port()
    with tempfile.TemporaryDirectory(prefix="gridflux-gridftp-stor.") as temp_text:
        temp_dir = Path(temp_text)
        root = temp_dir / "root"
        root.mkdir()
        source = temp_dir / "source.bin"
        make_file(source, args.bytes)
        expected_sha = sha256_file(source)
        server_log = temp_dir / "gridftp-server.log"

        server_cmd = [
            str(server_bin),
            "--host",
            "127.0.0.1",
            "--port",
            str(control_port),
            "--root",
            str(root),
            "--data-port-base",
            str(data_port_base),
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
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

        try:
            sock, buffer, greeting = connect_control(control_port)
            with sock:
                assert reply_code(greeting) == 220, greeting
                assert reply_code(send_command(sock, buffer, "USER gridflux")) == 331
                assert reply_code(send_command(sock, buffer, "PASS gridflux")) == 230
                assert reply_code(send_command(sock, buffer, "TYPE I")) == 200
                epsv = send_command(sock, buffer, "EPSV")
                assert reply_code(epsv) == 229, epsv
                data_port = parse_epsv_port(epsv)
                stor = send_command(sock, buffer, "STOR uploaded.bin")
                assert reply_code(stor) == 150, stor
                transfer_id = parse_transfer_id(stor)

                client_cmd = [
                    str(client_bin),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(data_port),
                    "--input",
                    str(source),
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
                    "--transfer-id",
                    transfer_id,
                ]
                subprocess.run(client_cmd, check=True)
                complete = read_reply(sock, buffer)
                assert reply_code(complete) == 226, complete
                assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            dest = root / "uploaded.bin"
            actual_sha = sha256_file(dest)
            if expected_sha != actual_sha:
                raise RuntimeError(f"sha256 mismatch: {expected_sha} != {actual_sha}")
            print(f"gridftp control STOR smoke passed transfer_id={transfer_id}")
            return 0
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            if server.returncode not in (0, -15, -9):
                print(server_log.read_text(encoding="utf-8"), file=sys.stderr)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run GridFlux GridFTP control STOR smoke.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=int, default=4 * 1024 * 1024)
    parser.add_argument("--connections", type=int, default=2)
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--checksum", choices=["crc32c", "none"], default="crc32c")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
