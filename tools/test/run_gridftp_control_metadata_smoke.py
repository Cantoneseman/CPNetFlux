#!/usr/bin/env python3
import argparse
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from gridftp_port_window import clamp_passive_data_port_base


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
    server_bin = build_dir / "cpnetflux-gridftp-server"
    if not server_bin.exists():
        raise FileNotFoundError(f"missing gridftp server in {build_dir}")

    with tempfile.TemporaryDirectory(prefix="cpnetflux-gridftp-metadata.") as temp_text:
        temp_dir = Path(temp_text)
        root = temp_dir / "root"
        (root / "subdir").mkdir(parents=True)
        source = root / "subdir" / "source.bin"
        payload = b"cpnetflux metadata smoke\n"
        source.write_bytes(payload)
        os.utime(source, (0, 0))

        control_port = free_port()
        data_port_base = clamp_passive_data_port_base(free_port())
        server_log = temp_dir / "gridftp-metadata.log"
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
        ]
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

        try:
            sock, buffer, greeting = connect_control(control_port)
            with sock:
                assert reply_code(greeting) == 220, greeting
                assert reply_code(send_command(sock, buffer, "SIZE subdir/source.bin")) == 530
                assert reply_code(send_command(sock, buffer, "USER cpnetflux")) == 331
                assert reply_code(send_command(sock, buffer, "PASS cpnetflux")) == 230

                feat = send_command(sock, buffer, "FEAT")
                assert reply_code(feat) == 211, feat
                feat_text = "\n".join(feat)
                for token in ("SIZE", "MDTM", "LIST", "NLST", "CWD", "CDUP"):
                    assert token in feat_text, feat_text

                pwd = send_command(sock, buffer, "PWD")
                assert reply_code(pwd) == 257, pwd
                assert '"/"' in pwd[0], pwd

                size = send_command(sock, buffer, "SIZE subdir/source.bin")
                assert reply_code(size) == 213, size
                assert size[0].endswith(str(len(payload))), size

                mdtm = send_command(sock, buffer, "MDTM subdir/source.bin")
                assert reply_code(mdtm) == 213, mdtm
                assert re.match(r"213 \d{14}$", mdtm[0]), mdtm

                assert reply_code(send_command(sock, buffer, "SIZE subdir")) == 550
                assert reply_code(send_command(sock, buffer, "CWD subdir/source.bin")) == 550
                assert reply_code(send_command(sock, buffer, "CWD ../")) == 550

                cwd = send_command(sock, buffer, "CWD subdir")
                assert reply_code(cwd) == 250, cwd
                pwd = send_command(sock, buffer, "PWD")
                assert reply_code(pwd) == 257, pwd
                assert '"/subdir"' in pwd[0], pwd

                size = send_command(sock, buffer, "SIZE source.bin")
                assert reply_code(size) == 213, size
                assert size[0].endswith(str(len(payload))), size

                cdup = send_command(sock, buffer, "CDUP")
                assert reply_code(cdup) == 250, cdup
                pwd = send_command(sock, buffer, "PWD")
                assert reply_code(pwd) == 257, pwd
                assert '"/"' in pwd[0], pwd
                assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            print("gridftp control metadata smoke passed")
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
    parser = argparse.ArgumentParser(description="Run CPNetFlux GridFTP metadata command smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
