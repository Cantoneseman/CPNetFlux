#!/usr/bin/env python3
"""Loopback smoke for Phase 6C control-plane TLS alpha."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from gridftp_port_window import assert_epsv_port_in_window, clamp_passive_data_port_base


def make_file(path: Path, total_bytes: int) -> None:
    block = bytes((index * 19) % 251 for index in range(1024 * 1024))
    remaining = total_bytes
    with path.open("wb") as handle:
        while remaining:
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


def read_line(sock: socket.socket | ssl.SSLSocket, buffer: bytearray) -> str:
    while b"\n" not in buffer:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("control connection closed")
        buffer.extend(chunk)
    index = buffer.index(ord("\n"))
    line = bytes(buffer[: index + 1]).decode("utf-8", errors="replace").rstrip("\r\n")
    del buffer[: index + 1]
    return line


def read_reply(sock: socket.socket | ssl.SSLSocket, buffer: bytearray) -> list[str]:
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


def send_command(sock: socket.socket | ssl.SSLSocket, buffer: bytearray, command: str) -> list[str]:
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


def generate_cert(cert: Path, key: Path) -> bool:
    openssl = shutil.which("openssl")
    if not openssl:
        return False
    command = [
        openssl,
        "req",
        "-x509",
        "-newkey",
        "rsa:2048",
        "-keyout",
        str(key),
        "-out",
        str(cert),
        "-sha256",
        "-days",
        "1",
        "-nodes",
        "-subj",
        "/CN=localhost",
    ]
    subprocess.run(command, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    return True


def wait_for_server(port: int, *, tls_required: bool, cafile: Path | None = None) -> None:
    deadline = time.monotonic() + 10.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            raw = socket.create_connection(("127.0.0.1", port), timeout=2.0)
            if tls_required:
                assert cafile is not None
                context = ssl.create_default_context(cafile=str(cafile))
                context.check_hostname = False
                with context.wrap_socket(raw, server_hostname="localhost") as sock:
                    buffer = bytearray()
                    greeting = read_reply(sock, buffer)
                    if reply_code(greeting) == 220:
                        return
            else:
                raw.close()
                return
        except (OSError, ssl.SSLError, RuntimeError) as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"server did not become ready: {last_error}")


def tls_connect(port: int, cafile: Path) -> tuple[ssl.SSLSocket, bytearray, list[str]]:
    raw = socket.create_connection(("127.0.0.1", port), timeout=5.0)
    context = ssl.create_default_context(cafile=str(cafile))
    context.check_hostname = False
    sock = context.wrap_socket(raw, server_hostname="localhost")
    buffer = bytearray()
    greeting = read_reply(sock, buffer)
    return sock, buffer, greeting


def plaintext_must_fail(port: int) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=3.0) as sock:
        sock.sendall(b"USER cpnetflux\r\n")
        try:
            data = sock.recv(128)
        except ConnectionResetError:
            return
        if data.startswith(b"220") or data.startswith(b"331"):
            raise RuntimeError(f"plaintext control unexpectedly succeeded: {data!r}")


def run_smoke(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir)
    server_bin = build_dir / "cpnetflux-gridftp-server"
    upload_bin = build_dir / "cpnetflux-file-client"
    download_bin = build_dir / "cpnetflux-file-download-client"
    if not server_bin.exists() or not upload_bin.exists() or not download_bin.exists():
        raise FileNotFoundError(f"missing CPNetFlux binaries in {build_dir}")

    with tempfile.TemporaryDirectory(prefix="cpnetflux-gridftp-tls.") as temp_text:
        temp_dir = Path(temp_text)
        cert = temp_dir / "cert.pem"
        key = temp_dir / "key.pem"
        if not generate_cert(cert, key):
            print("openssl CLI unavailable; TLS smoke skipped")
            return 0

        missing_key = temp_dir / "missing-key.pem"
        missing_probe = subprocess.run(
            [
                str(server_bin),
                "--root",
                str(temp_dir),
                "--tls-mode",
                "required",
                "--tls-cert-file",
                str(cert),
                "--tls-key-file",
                str(missing_key),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if missing_probe.returncode == 0:
            raise RuntimeError("TLS missing key probe unexpectedly succeeded")
        if "PRIVATE KEY" in (missing_probe.stdout + missing_probe.stderr):
            raise RuntimeError("TLS missing key probe leaked key material")
        if "TLS" not in (missing_probe.stdout + missing_probe.stderr):
            # A build without OpenSSL may still return a clear unavailable/config error.
            raise RuntimeError("TLS missing key probe did not mention TLS")

        root = temp_dir / "root"
        root.mkdir()
        source = temp_dir / "source.bin"
        make_file(source, args.bytes)
        expected_sha = sha256_file(source)
        event_log = temp_dir / "tls-events.jsonl"
        server_log = temp_dir / "gridftp-tls.log"
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
            "--connections",
            "1",
            "--checksum",
            "crc32c",
            "--tls-mode",
            "required",
            "--tls-cert-file",
            str(cert),
            "--tls-key-file",
            str(key),
            "--event-log",
            str(event_log),
        ]
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

        try:
            wait_for_server(control_port, tls_required=True, cafile=cert)
            plaintext_must_fail(control_port)

            sock, buffer, greeting = tls_connect(control_port, cert)
            with sock:
                assert reply_code(greeting) == 220, greeting
                assert reply_code(send_command(sock, buffer, "USER cpnetflux")) == 331
                assert reply_code(send_command(sock, buffer, "PASS cpnetflux")) == 230
                assert reply_code(send_command(sock, buffer, "TYPE I")) == 200
                size_missing = send_command(sock, buffer, "SIZE uploaded.bin")
                assert reply_code(size_missing) == 550, size_missing

                epsv = send_command(sock, buffer, "EPSV")
                assert reply_code(epsv) == 229, epsv
                data_port = parse_epsv_port(epsv)
                assert_epsv_port_in_window(data_port, data_port_base)
                stor = send_command(sock, buffer, "STOR uploaded.bin")
                assert reply_code(stor) == 150, stor
                transfer_id = parse_transfer_id(stor)
                subprocess.run(
                    [
                        str(upload_bin),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(data_port),
                        "--input",
                        str(source),
                        "--connections",
                        "1",
                        "--checksum",
                        "crc32c",
                        "--transfer-id",
                        transfer_id,
                    ],
                    check=True,
                )
                assert reply_code(read_reply(sock, buffer)) == 226

                output = temp_dir / "downloaded.bin"
                epsv = send_command(sock, buffer, "EPSV")
                assert reply_code(epsv) == 229, epsv
                data_port = parse_epsv_port(epsv)
                assert_epsv_port_in_window(data_port, data_port_base)
                retr = send_command(sock, buffer, "RETR uploaded.bin")
                assert reply_code(retr) == 150, retr
                transfer_id = parse_transfer_id(retr)
                subprocess.run(
                    [
                        str(download_bin),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(data_port),
                        "--output",
                        str(output),
                        "--connections",
                        "1",
                        "--checksum",
                        "crc32c",
                        "--transfer-id",
                        transfer_id,
                    ],
                    check=True,
                )
                assert reply_code(read_reply(sock, buffer)) == 226
                assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            if sha256_file(output) != expected_sha:
                raise RuntimeError("TLS control smoke download hash mismatch")
            text = server_log.read_text(encoding="utf-8", errors="replace")
            events = event_log.read_text(encoding="utf-8", errors="replace")
            for forbidden in ("PRIVATE KEY", "BEGIN PRIVATE KEY"):
                if forbidden in text or forbidden in events:
                    raise RuntimeError("TLS smoke leaked private key material")
            parsed_events = [json.loads(line) for line in events.splitlines() if line.strip()]
            if not any(event.get("event") == "tls_handshake_success" for event in parsed_events):
                raise RuntimeError(f"missing tls handshake event: {events}")
            print("gridftp control TLS smoke passed")
            print(f"server_log={server_log}")
            print(f"event_log={event_log}")
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
    parser = argparse.ArgumentParser(description="Run CPNetFlux control TLS alpha smoke.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=int, default=1024 * 1024)
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
