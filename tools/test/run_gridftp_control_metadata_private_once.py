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


def read_data(host: str, port: int) -> str:
    with socket.create_connection((host, port), timeout=5.0) as sock:
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    return b"".join(chunks).decode("utf-8", errors="replace")


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


def login(host: str, port: int) -> tuple[socket.socket, bytearray]:
    sock, buffer, greeting = connect_control(host, port)
    assert reply_code(greeting) == 220, greeting
    assert reply_code(send_command(sock, buffer, "USER cpnetflux")) == 331
    assert reply_code(send_command(sock, buffer, "PASS cpnetflux")) == 230
    return sock, buffer


def run_listing(sock: socket.socket, buffer: bytearray, host: str, command: str,
                data_port_base: int) -> str:
    epsv = send_command(sock, buffer, "EPSV")
    assert reply_code(epsv) == 229, epsv
    port = parse_epsv_port(epsv)
    assert_epsv_port_in_window(port, data_port_base)
    sock.sendall((command + "\r\n").encode("utf-8"))
    opening = read_reply(sock, buffer)
    assert reply_code(opening) == 150, opening
    payload = read_data(host, port)
    complete = read_reply(sock, buffer)
    assert reply_code(complete) == 226, complete
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private GridFTP control metadata/list smoke.")
    parser.add_argument("--remote", default="root@<redacted>")
    parser.add_argument("--server-host", default="<redacted>")
    parser.add_argument("--control-port", type=int, default=2121)
    parser.add_argument("--root", default="/tmp/cpnetflux-gridftp-metadata-private-root")
    parser.add_argument("--local-build-dir", default="/root/projects/CPNetFlux/build")
    parser.add_argument("--data-port-base", type=int, default=20300)
    parser.add_argument("--output-dir", default="tools/perf/results")
    args = parser.parse_args()
    args.data_port_base = clamp_passive_data_port_base(args.data_port_base)

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    server_log = output_dir / f"{timestamp}_gridftp_control_metadata_private.log"

    server_bin = f"{args.local_build_dir.rstrip('/')}/cpnetflux-gridftp-server"
    root = Path(args.root)
    subprocess.run(["rm", "-rf", str(root)], check=True)
    (root / "subdir").mkdir(parents=True)
    (root / "alpha.bin").write_bytes(b"alpha")
    (root / "subdir" / "nested.txt").write_bytes(b"nested")

    run_remote(args.remote, "true")
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
    ]
    with server_log.open("w", encoding="utf-8") as log_handle:
        server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

    try:
        sock, buffer = login(args.server_host, args.control_port)
        with sock:
            size = send_command(sock, buffer, "SIZE alpha.bin")
            assert reply_code(size) == 213, size
            assert size[0].endswith("5"), size

            mdtm = send_command(sock, buffer, "MDTM alpha.bin")
            assert reply_code(mdtm) == 213, mdtm
            assert re.match(r"213 \d{14}$", mdtm[0]), mdtm

            assert reply_code(send_command(sock, buffer, "CWD subdir")) == 250
            pwd = send_command(sock, buffer, "PWD")
            assert reply_code(pwd) == 257 and '"/subdir"' in pwd[0], pwd
            assert reply_code(send_command(sock, buffer, "CDUP")) == 250

            nlst = run_listing(sock, buffer, args.server_host, "NLST", args.data_port_base)
            assert nlst.splitlines() == ["alpha.bin", "subdir"], nlst
            listing = run_listing(sock, buffer, args.server_host, "LIST", args.data_port_base)
            assert " alpha.bin" in listing and " subdir" in listing, listing
            assert str(root) not in listing, listing
            assert reply_code(send_command(sock, buffer, "QUIT")) == 221

        print("gridftp private metadata/list smoke passed")
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
