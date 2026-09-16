#!/usr/bin/env python3
import argparse
import hashlib
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from gridftp_port_window import assert_epsv_port_in_window, clamp_passive_data_port_base


def make_file(path: Path, total_bytes: int) -> None:
    block = bytes((index * 23) % 251 for index in range(1024 * 1024))
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


def login_and_epsv(control_port: int) -> tuple[socket.socket, bytearray, int]:
    sock, buffer, greeting = connect_control(control_port)
    assert reply_code(greeting) == 220, greeting
    assert reply_code(send_command(sock, buffer, "USER cpnetflux")) == 331
    assert reply_code(send_command(sock, buffer, "PASS cpnetflux")) == 230
    assert reply_code(send_command(sock, buffer, "TYPE I")) == 200
    epsv = send_command(sock, buffer, "EPSV")
    assert reply_code(epsv) == 229, epsv
    return sock, buffer, parse_epsv_port(epsv)


def run_smoke(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir)
    server_bin = build_dir / "cpnetflux-gridftp-server"
    client_bin = build_dir / "cpnetflux-file-download-client"

    with tempfile.TemporaryDirectory(prefix="cpnetflux-gridftp-retr-corrupt.") as temp_text:
        temp_dir = Path(temp_text)
        root = temp_dir / "root"
        root.mkdir()
        source = root / "source.bin"
        output = temp_dir / "downloaded.bin"
        make_file(source, args.bytes)
        expected_sha = sha256_file(source)

        control_port = free_port()
        data_port_base = clamp_passive_data_port_base(free_port())
        server_log = temp_dir / "gridftp-retr-corrupt.log"
        server_cmd = [
            str(server_bin),
            "--host",
            "0.0.0.0",
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
            "crc32c",
            "--checksum-backend",
            args.checksum_backend,
        ]
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)
        try:
            sock, buffer, data_port = login_and_epsv(control_port)
            assert_epsv_port_in_window(data_port, data_port_base)
            with sock:
                retr = send_command(sock, buffer, "RETR source.bin")
                assert reply_code(retr) == 150, retr
                transfer_id = parse_transfer_id(retr)
                partial_cmd = [
                    str(client_bin),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(data_port),
                    "--output",
                    str(output),
                    "--connections",
                    str(args.connections),
                    "--buffer-size",
                    str(args.buffer_size),
                    "--checksum",
                    "crc32c",
                    "--checksum-backend",
                    args.checksum_backend,
                    "--transfer-id",
                    transfer_id,
                    "--max-chunks",
                    str(args.max_chunks),
                ]
                partial = subprocess.run(partial_cmd, text=True, capture_output=True, check=False)
                if partial.returncode == 0:
                    raise RuntimeError("partial download unexpectedly succeeded")
                failed = read_reply(sock, buffer)
                assert reply_code(failed) == 550, failed

            part_path = Path(str(output) + f".part.{transfer_id}")
            assert part_path.exists()
            with part_path.open("r+b") as handle:
                handle.seek(17)
                original = handle.read(1)
                handle.seek(17)
                handle.write(bytes([original[0] ^ 0x5A]))

            sock, buffer, data_port = login_and_epsv(control_port)
            assert_epsv_port_in_window(data_port, data_port_base)
            with sock:
                assert reply_code(send_command(sock, buffer, f"REST GFID:{transfer_id}")) == 350
                retr = send_command(sock, buffer, "RETR source.bin")
                assert reply_code(retr) == 150, retr
                resume_cmd = [
                    str(client_bin),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(data_port),
                    "--output",
                    str(output),
                    "--connections",
                    str(args.connections),
                    "--buffer-size",
                    str(args.buffer_size),
                    "--checksum",
                    "crc32c",
                    "--checksum-backend",
                    args.checksum_backend,
                    "--transfer-id",
                    transfer_id,
                    "--resume",
                ]
                resumed = subprocess.run(resume_cmd, text=True, capture_output=True, check=True)
                complete = read_reply(sock, buffer)
                assert reply_code(complete) == 226, complete
                assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            actual_sha = sha256_file(output)
            if expected_sha != actual_sha:
                raise RuntimeError(f"sha256 mismatch: {expected_sha} != {actual_sha}")
            if "removed_corrupt_chunks=1" not in (resumed.stdout + resumed.stderr):
                raise RuntimeError(f"missing corrupt chunk repair stat: {resumed.stdout}")
            print(f"gridftp control RETR corrupt resume smoke passed transfer_id={transfer_id}")
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
    parser = argparse.ArgumentParser(
        description="Run CPNetFlux GridFTP control RETR corrupt resume smoke."
    )
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=int, default=8 * 1024 * 1024)
    parser.add_argument("--connections", type=int, default=2)
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    parser.add_argument("--max-chunks", type=int, default=2)
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
