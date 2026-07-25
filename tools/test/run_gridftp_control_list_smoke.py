#!/usr/bin/env python3
import argparse
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


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


def read_data(host: str, port: int) -> str:
    with socket.create_connection((host, port), timeout=5.0) as sock:
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    return b"".join(chunks).decode("utf-8", errors="replace")


def run_listing(sock: socket.socket, buffer: bytearray, command: str) -> tuple[list[str], str, list[str]]:
    epsv = send_command(sock, buffer, "EPSV")
    assert reply_code(epsv) == 229, epsv
    data_port = parse_epsv_port(epsv)
    sock.sendall((command + "\r\n").encode("utf-8"))
    opening = read_reply(sock, buffer)
    assert reply_code(opening) == 150, opening
    payload = read_data("127.0.0.1", data_port)
    complete = read_reply(sock, buffer)
    return opening, payload, complete


def run_smoke(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir)
    server_bin = build_dir / "gridflux-gridftp-server"
    if not server_bin.exists():
        raise FileNotFoundError(f"missing gridftp server in {build_dir}")

    with tempfile.TemporaryDirectory(prefix="gridflux-gridftp-list.") as temp_text:
        temp_dir = Path(temp_text)
        root = temp_dir / "root"
        (root / "subdir").mkdir(parents=True)
        alpha = root / "alpha.bin"
        alpha.write_bytes(b"alpha")
        nested = root / "subdir" / "nested.txt"
        nested.write_bytes(b"nested")

        control_port = free_port()
        data_port_base = free_port()
        server_log = temp_dir / "gridftp-list.log"
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
        ]
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

        try:
            sock, buffer, greeting = connect_control(control_port)
            with sock:
                assert reply_code(greeting) == 220, greeting
                assert reply_code(send_command(sock, buffer, "USER gridflux")) == 331
                assert reply_code(send_command(sock, buffer, "PASS gridflux")) == 230
                assert reply_code(send_command(sock, buffer, "LIST")) == 550
                assert reply_code(send_command(sock, buffer, "NLST")) == 550

                opening, payload, complete = run_listing(sock, buffer, "NLST")
                assert reply_code(opening) == 150, opening
                assert reply_code(complete) == 226, complete
                names = [line for line in payload.splitlines() if line]
                assert names == ["alpha.bin", "subdir"], names
                assert str(root) not in payload, payload

                _, payload, complete = run_listing(sock, buffer, "LIST")
                assert reply_code(complete) == 226, complete
                assert "- 5 " in payload and " alpha.bin" in payload, payload
                assert "d 0 " in payload and " subdir" in payload, payload
                assert str(root) not in payload, payload

                _, payload, complete = run_listing(sock, buffer, "NLST subdir")
                assert reply_code(complete) == 226, complete
                assert payload.splitlines() == ["nested.txt"], payload

                epsv = send_command(sock, buffer, "EPSV")
                assert reply_code(epsv) == 229, epsv
                denied = send_command(sock, buffer, "LIST ../")
                assert reply_code(denied) == 550, denied
                assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            print("gridftp control LIST/NLST smoke passed")
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
    parser = argparse.ArgumentParser(description="Run GridFlux GridFTP LIST/NLST smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
