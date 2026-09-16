#!/usr/bin/env python3
"""Loopback smoke for GridFTP-style token auth.

This module also provides helper functions imported by
run_gridftp_event_log_smoke.py.
"""

from __future__ import annotations

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


def assert_reply(command: str, lines: list[str], expected: int) -> None:
    actual = reply_code(lines)
    if actual != expected:
        raise RuntimeError(f"{command} expected {expected}, got {actual}: {lines!r}")


def assert_no_secret_leak(*paths: Path) -> None:
    pass_pattern = re.compile(r"PASS\s+\S+", re.IGNORECASE)
    for path in paths:
        if not path.exists():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if TOKEN in text or "wrong-token" in text or pass_pattern.search(text):
            raise RuntimeError(f"secret leaked in {path}")


def run_smoke(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir)
    server_bin = build_dir / "cpnetflux-gridftp-server"
    upload_bin = build_dir / "cpnetflux-file-client"
    download_bin = build_dir / "cpnetflux-file-download-client"
    if not server_bin.exists() or not upload_bin.exists() or not download_bin.exists():
        raise FileNotFoundError(f"missing CPNetFlux binaries in {build_dir}")

    with tempfile.TemporaryDirectory(prefix="cpnetflux-gridftp-token.") as temp_text:
        temp = Path(temp_text)
        root = temp / "root"
        root.mkdir()
        source = temp / "source.bin"
        make_file(source, args.bytes)
        expected_sha = sha256_file(source)
        output = temp / "downloaded.bin"
        token_file = temp / "token.txt"
        token_file.write_text(TOKEN + "\n", encoding="utf-8")
        token_file.chmod(0o600)
        server_log = temp / "server.log"
        event_log = temp / "events.jsonl"
        control_port = free_port()
        data_port_base = clamp_passive_data_port_base(free_port())
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
            sock, buffer, greeting = connect_control(control_port)
            with sock:
                assert_reply("greeting", greeting, 220)
                assert_reply("SIZE unauthenticated", send_command(sock, buffer, "SIZE uploaded.bin"), 530)
                assert_reply("STOR unauthenticated", send_command(sock, buffer, "STOR uploaded.bin"), 530)
                assert_reply("RETR unauthenticated", send_command(sock, buffer, "RETR uploaded.bin"), 530)
                assert_reply("USER token", send_command(sock, buffer, "USER token"), 331)
                assert_reply("PASS wrong-token", send_command(sock, buffer, "PASS wrong-token"), 530)
                assert_reply("USER token retry", send_command(sock, buffer, "USER token"), 331)
                assert_reply("PASS token", send_command(sock, buffer, "PASS " + TOKEN), 230)
                assert_reply("TYPE I", send_command(sock, buffer, "TYPE I"), 200)

                epsv = send_command(sock, buffer, "EPSV")
                assert_reply("EPSV for STOR", epsv, 229)
                assert_epsv_port_in_window(parse_epsv_port(epsv), data_port_base)
                stor = send_command(sock, buffer, "STOR uploaded.bin")
                assert_reply("STOR uploaded.bin", stor, 150)
                upload_transfer_id = parse_transfer_id(stor)
                subprocess.run(
                    [
                        str(upload_bin),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(parse_epsv_port(epsv)),
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
                        upload_transfer_id,
                        "--event-log",
                        str(event_log),
                    ],
                    check=True,
                )
                assert_reply("STOR complete", read_reply(sock, buffer), 226)

                epsv = send_command(sock, buffer, "EPSV")
                assert_reply("EPSV for RETR", epsv, 229)
                assert_epsv_port_in_window(parse_epsv_port(epsv), data_port_base)
                retr = send_command(sock, buffer, "RETR uploaded.bin")
                assert_reply("RETR uploaded.bin", retr, 150)
                download_transfer_id = parse_transfer_id(retr)
                subprocess.run(
                    [
                        str(download_bin),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(parse_epsv_port(epsv)),
                        "--output",
                        str(output),
                        "--connections",
                        str(args.connections),
                        "--buffer-size",
                        str(args.buffer_size),
                        "--checksum",
                        args.checksum,
                        "--checksum-backend",
                        args.checksum_backend,
                        "--transfer-id",
                        download_transfer_id,
                        "--event-log",
                        str(event_log),
                    ],
                    check=True,
                )
                assert_reply("RETR complete", read_reply(sock, buffer), 226)
                assert_reply("QUIT", send_command(sock, buffer, "QUIT"), 221)

            uploaded = root / "uploaded.bin"
            if sha256_file(uploaded) != expected_sha:
                raise RuntimeError("uploaded SHA256 mismatch")
            if sha256_file(output) != expected_sha:
                raise RuntimeError("downloaded SHA256 mismatch")
            assert_no_secret_leak(server_log, event_log)
            print(
                "gridftp control token smoke passed "
                f"upload_transfer_id={upload_transfer_id} "
                f"download_transfer_id={download_transfer_id}"
            )
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


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux GridFTP token auth smoke.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=int, default=256 * 1024)
    parser.add_argument("--connections", type=int, default=1)
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--checksum", choices=["crc32c", "none"], default="crc32c")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
