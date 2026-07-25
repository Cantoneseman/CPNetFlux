#!/usr/bin/env python3
"""Loopback smoke for Phase 6D STOR/RETR framed data-channel TLS alpha."""

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


def make_file(path: Path, total_bytes: int) -> None:
    block = bytes((index * 23) % 251 for index in range(1024 * 1024))
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
    subprocess.run(
        [
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
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    key.chmod(0o600)
    return True


def tls_connect(port: int, cafile: Path) -> tuple[ssl.SSLSocket, bytearray, list[str]]:
    raw = socket.create_connection(("127.0.0.1", port), timeout=5.0)
    context = ssl.create_default_context(cafile=str(cafile))
    context.check_hostname = False
    sock = context.wrap_socket(raw, server_hostname="localhost")
    buffer = bytearray()
    greeting = read_reply(sock, buffer)
    return sock, buffer, greeting


def wait_tls_server(port: int, cafile: Path) -> None:
    deadline = time.monotonic() + 10.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            sock, _, greeting = tls_connect(port, cafile)
            with sock:
                if reply_code(greeting) == 220:
                    return
        except (OSError, ssl.SSLError, RuntimeError) as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"TLS server did not become ready: {last_error}")


def read_data(host: str, port: int) -> str:
    with socket.create_connection((host, port), timeout=5.0) as sock:
        chunks: list[bytes] = []
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            chunks.append(chunk)
    return b"".join(chunks).decode("utf-8", errors="replace")


def run_listing(sock: ssl.SSLSocket, buffer: bytearray, command: str) -> str:
    epsv = send_command(sock, buffer, "EPSV")
    assert reply_code(epsv) == 229, epsv
    data_port = parse_epsv_port(epsv)
    sock.sendall((command + "\r\n").encode("utf-8"))
    opening = read_reply(sock, buffer)
    assert reply_code(opening) == 150, opening
    payload = read_data("127.0.0.1", data_port)
    complete = read_reply(sock, buffer)
    assert reply_code(complete) == 226, complete
    return payload


def run_smoke(args: argparse.Namespace) -> int:
    build_dir = Path(args.build_dir)
    server_bin = build_dir / "gridflux-gridftp-server"
    upload_bin = build_dir / "gridflux-file-client"
    download_bin = build_dir / "gridflux-file-download-client"
    tree_upload_bin = build_dir / "gridflux-tree-upload-client"
    tree_download_bin = build_dir / "gridflux-tree-download-client"
    for binary in (server_bin, upload_bin, download_bin, tree_upload_bin, tree_download_bin):
        if not binary.exists():
            raise FileNotFoundError(f"missing GridFlux binary: {binary}")

    with tempfile.TemporaryDirectory(prefix="gridflux-gridftp-data-tls.") as temp_text:
        temp_dir = Path(temp_text)
        cert = temp_dir / "cert.pem"
        key = temp_dir / "key.pem"
        if not generate_cert(cert, key):
            print("openssl CLI unavailable; data TLS smoke skipped")
            return 0

        root = temp_dir / "root"
        root.mkdir()
        source = temp_dir / "source.bin"
        make_file(source, args.bytes)
        expected_sha = sha256_file(source)
        tree_source = temp_dir / "tree-source"
        (tree_source / "nested").mkdir(parents=True)
        (tree_source / "alpha.txt").write_text("alpha\n", encoding="utf-8")
        (tree_source / "nested" / "beta.bin").write_bytes(bytes(range(64)) * 32)
        event_log = temp_dir / "data-tls-events.jsonl"
        server_log = temp_dir / "gridftp-data-tls.log"
        control_port = free_port()
        data_port_base = free_port()
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
            "1",
            "--checksum",
            "crc32c",
            "--tls-mode",
            "required",
            "--tls-cert-file",
            str(cert),
            "--tls-key-file",
            str(key),
            "--data-tls-mode",
            "required",
            "--event-log",
            str(event_log),
        ]
        with server_log.open("w", encoding="utf-8") as log_handle:
            server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

        try:
            wait_tls_server(control_port, cert)
            sock, buffer, greeting = tls_connect(control_port, cert)
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
                        "--data-tls-mode",
                        "required",
                        "--tls-ca-file",
                        str(cert),
                    ],
                    check=True,
                )
                assert reply_code(read_reply(sock, buffer)) == 226

                output = temp_dir / "downloaded.bin"
                epsv = send_command(sock, buffer, "EPSV")
                assert reply_code(epsv) == 229, epsv
                data_port = parse_epsv_port(epsv)
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
                        "--data-tls-mode",
                        "required",
                        "--tls-ca-file",
                        str(cert),
                    ],
                    check=True,
                )
                assert reply_code(read_reply(sock, buffer)) == 226
                if sha256_file(output) != expected_sha:
                    raise RuntimeError("data TLS RETR hash mismatch")

                epsv = send_command(sock, buffer, "EPSV")
                assert reply_code(epsv) == 229, epsv
                data_port = parse_epsv_port(epsv)
                stor = send_command(sock, buffer, "STOR plaintext-should-fail.bin")
                assert reply_code(stor) == 150, stor
                transfer_id = parse_transfer_id(stor)
                failed = subprocess.run(
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
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    timeout=10,
                )
                if failed.returncode == 0:
                    raise RuntimeError("plaintext data client unexpectedly succeeded")
                assert reply_code(read_reply(sock, buffer)) == 550

                listing = run_listing(sock, buffer, "NLST")
                if "uploaded.bin" not in listing:
                    raise RuntimeError(f"plain LIST/NLST metadata data missing upload: {listing!r}")

                tree_upload_summary = temp_dir / "tree-upload-summary.json"
                subprocess.run(
                    [
                        str(tree_upload_bin),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(control_port),
                        "--source-dir",
                        str(tree_source),
                        "--dest-dir",
                        "tree-uploaded",
                        "--connections",
                        "1",
                        "--file-parallelism",
                        "1",
                        "--checksum",
                        "crc32c",
                        "--tls-mode",
                        "required",
                        "--tls-ca-file",
                        str(cert),
                        "--data-tls-mode",
                        "required",
                        "--json-summary",
                        str(tree_upload_summary),
                    ],
                    check=True,
                )
                uploaded_tree = json.loads(tree_upload_summary.read_text(encoding="utf-8"))
                if uploaded_tree.get("result") != "pass":
                    raise RuntimeError(f"tree upload over data TLS failed: {uploaded_tree}")

                tree_dest = temp_dir / "tree-dest"
                tree_download_summary = temp_dir / "tree-download-summary.json"
                subprocess.run(
                    [
                        str(tree_download_bin),
                        "--host",
                        "127.0.0.1",
                        "--port",
                        str(control_port),
                        "--source-dir",
                        "tree-uploaded",
                        "--dest-dir",
                        str(tree_dest),
                        "--connections",
                        "1",
                        "--file-parallelism",
                        "1",
                        "--checksum",
                        "crc32c",
                        "--tls-mode",
                        "required",
                        "--tls-ca-file",
                        str(cert),
                        "--data-tls-mode",
                        "required",
                        "--json-summary",
                        str(tree_download_summary),
                    ],
                    check=True,
                )
                downloaded_tree = json.loads(tree_download_summary.read_text(encoding="utf-8"))
                if downloaded_tree.get("result") != "pass":
                    raise RuntimeError(f"tree download over data TLS failed: {downloaded_tree}")
                if uploaded_tree.get("tree_hash") != downloaded_tree.get("tree_hash"):
                    raise RuntimeError(
                        "tree data TLS hash mismatch: "
                        f"{uploaded_tree.get('tree_hash')} != {downloaded_tree.get('tree_hash')}"
                    )
                assert reply_code(send_command(sock, buffer, "QUIT")) == 221

            text = server_log.read_text(encoding="utf-8", errors="replace")
            events = event_log.read_text(encoding="utf-8", errors="replace")
            for forbidden in ("PRIVATE KEY", "BEGIN PRIVATE KEY"):
                if forbidden in text or forbidden in events:
                    raise RuntimeError("data TLS smoke leaked private key material")
            parsed_events = [json.loads(line) for line in events.splitlines() if line.strip()]
            if not any(event.get("error_code") == "data_tls_failed" for event in parsed_events):
                raise RuntimeError(f"missing data TLS failure event: {events}")
            print("gridftp data TLS smoke passed")
            print(f"server_log={server_log}")
            print(f"event_log={event_log}")
            return 0
        except BaseException:
            if server_log.exists():
                print(server_log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
            if event_log.exists():
                print(event_log.read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
            raise
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
    parser = argparse.ArgumentParser(description="Run GridFlux framed data TLS smoke.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=int, default=1024 * 1024)
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
