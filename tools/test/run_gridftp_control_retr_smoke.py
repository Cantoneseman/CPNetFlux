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
    block = bytes((index * 17) % 251 for index in range(1024 * 1024))
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


def run_retr_case(args: argparse.Namespace, checksum: str, temp_dir: Path) -> None:
    build_dir = Path(args.build_dir)
    server_bin = build_dir / "cpnetflux-gridftp-server"
    client_bin = build_dir / "cpnetflux-file-download-client"
    if not server_bin.exists() or not client_bin.exists():
        raise FileNotFoundError(f"missing gridftp server or download client in {build_dir}")

    root = temp_dir / f"root-{checksum}"
    root.mkdir()
    source = root / "source.bin"
    output = temp_dir / f"downloaded-{checksum}.bin"
    make_file(source, args.bytes)
    expected_sha = sha256_file(source)

    control_port = free_port()
    data_port_base = clamp_passive_data_port_base(free_port())
    server_log = temp_dir / f"gridftp-retr-{checksum}.log"
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
        checksum,
        "--checksum-backend",
        args.checksum_backend,
    ]

    with server_log.open("w", encoding="utf-8") as log_handle:
        server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)
    try:
        sock, buffer, greeting = connect_control(control_port)
        with sock:
            assert reply_code(greeting) == 220, greeting
            assert reply_code(send_command(sock, buffer, "USER cpnetflux")) == 331
            assert reply_code(send_command(sock, buffer, "PASS cpnetflux")) == 230
            assert reply_code(send_command(sock, buffer, "TYPE I")) == 200
            epsv = send_command(sock, buffer, "EPSV")
            assert reply_code(epsv) == 229, epsv
            data_port = parse_epsv_port(epsv)
            assert_epsv_port_in_window(data_port, data_port_base)
            retr = send_command(sock, buffer, "RETR source.bin")
            assert reply_code(retr) == 150, retr
            transfer_id = parse_transfer_id(retr)

            client_cmd = [
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
                checksum,
                "--checksum-backend",
                args.checksum_backend,
                "--transfer-id",
                transfer_id,
            ]
            subprocess.run(client_cmd, check=True)
            complete = read_reply(sock, buffer)
            assert reply_code(complete) == 226, complete
            assert reply_code(send_command(sock, buffer, "QUIT")) == 221

        actual_sha = sha256_file(output)
        if expected_sha != actual_sha:
            raise RuntimeError(f"sha256 mismatch for {checksum}: {expected_sha} != {actual_sha}")
        text = server_log.read_text(encoding="utf-8")
        if "file_download_sender" not in text:
            raise RuntimeError(f"server log missing download sender stats: {text}")
        print(f"gridftp control RETR {checksum} smoke passed transfer_id={transfer_id}")
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        if server.returncode not in (0, -15, -9):
            print(server_log.read_text(encoding="utf-8"), file=sys.stderr)


def run_smoke(args: argparse.Namespace) -> int:
    with tempfile.TemporaryDirectory(prefix="cpnetflux-gridftp-retr.") as temp_text:
        temp_dir = Path(temp_text)
        run_retr_case(args, "crc32c", temp_dir)
        run_retr_case(args, "none", temp_dir)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux GridFTP control RETR smoke.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=int, default=4 * 1024 * 1024)
    parser.add_argument("--connections", type=int, default=2)
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
