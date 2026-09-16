#!/usr/bin/env python3
"""Private-network STOR/RETR framed data TLS smoke."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shlex
import shutil
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path

from gridftp_port_window import assert_epsv_port_in_window, clamp_passive_data_port_base


def ssh_prefix(remote: str) -> list[str]:
    if os.environ.get("CPNETFLUX_SSH_PASSWORD") or os.environ.get("SSHPASS"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no", remote]
    return ["ssh", "-o", "StrictHostKeyChecking=no", remote]


def run_remote(remote: str, command: str, *, input_text: str | None = None, check: bool = True) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env.get("CPNETFLUX_SSH_PASSWORD") and not env.get("SSHPASS"):
        env["SSHPASS"] = env["CPNETFLUX_SSH_PASSWORD"]
    completed = subprocess.run(
        ssh_prefix(remote) + [command],
        input=input_text,
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )
    if check and completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return completed


def write_remote_text(remote: str, path: str, text: str) -> None:
    command = f"umask 077 && mkdir -p {shlex.quote(str(Path(path).parent))} && cat > {shlex.quote(path)}"
    run_remote(remote, command, input_text=text)


def make_file(path: Path, total_bytes: int) -> None:
    block = bytes((index * 29) % 251 for index in range(1024 * 1024))
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


def generate_cert(cert: Path, key: Path) -> None:
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("openssl CLI is required for private data TLS smoke")
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


def tls_connect(host: str, port: int, cafile: Path) -> tuple[ssl.SSLSocket, bytearray, list[str]]:
    raw = socket.create_connection((host, port), timeout=5.0)
    context = ssl.create_default_context(cafile=str(cafile))
    context.check_hostname = False
    sock = context.wrap_socket(raw, server_hostname="localhost")
    buffer = bytearray()
    greeting = read_reply(sock, buffer)
    return sock, buffer, greeting


def wait_tls(host: str, port: int, cafile: Path) -> None:
    deadline = time.monotonic() + 15.0
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            sock, _, greeting = tls_connect(host, port, cafile)
            with sock:
                if reply_code(greeting) == 220:
                    return
        except (OSError, ssl.SSLError, RuntimeError) as error:
            last_error = error
            time.sleep(0.1)
    raise RuntimeError(f"failed to connect TLS control server: {last_error}")


def login_type_i(host: str, port: int, cafile: Path) -> tuple[ssl.SSLSocket, bytearray]:
    sock, buffer, greeting = tls_connect(host, port, cafile)
    assert reply_code(greeting) == 220, greeting
    assert reply_code(send_command(sock, buffer, "USER cpnetflux")) == 331
    assert reply_code(send_command(sock, buffer, "PASS cpnetflux")) == 230
    assert reply_code(send_command(sock, buffer, "TYPE I")) == 200
    return sock, buffer


def run_smoke(args: argparse.Namespace) -> int:
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    server_log = output_dir / f"{timestamp}_gridftp-data-tls-private.log"
    cert = output_dir / f"{timestamp}_data-tls-cert.pem"
    key = output_dir / f"{timestamp}_data-tls-key.pem"
    remote_ca = f"/tmp/cpnetflux-data-tls-private-{timestamp}-{os.getpid()}-ca.pem"
    remote_source = f"/tmp/cpnetflux-data-tls-private-{timestamp}-{os.getpid()}-source.bin"
    remote_download = f"/tmp/cpnetflux-data-tls-private-{timestamp}-{os.getpid()}-download.bin"
    generate_cert(cert, key)
    write_remote_text(args.remote, remote_ca, cert.read_text(encoding="utf-8"))
    args.data_port_base = clamp_passive_data_port_base(args.data_port_base)

    root = Path(args.root)
    subprocess.run(["rm", "-rf", str(root)], check=True)
    root.mkdir(parents=True)
    local_source = output_dir / f"{timestamp}_data-tls-source.bin"
    make_file(local_source, args.bytes)
    source_sha = sha256_file(local_source)

    remote_client = f"{args.remote_build_dir.rstrip('/')}/cpnetflux-file-client"
    remote_download_client = f"{args.remote_build_dir.rstrip('/')}/cpnetflux-file-download-client"
    run_remote(args.remote, f"test -x {shlex.quote(remote_client)} && test -x {shlex.quote(remote_download_client)}")
    run_remote(
        args.remote,
        f"python3 - <<'PY'\n"
        f"from pathlib import Path\n"
        f"block = bytes((i * 29) % 251 for i in range(1024 * 1024))\n"
        f"remaining = {args.bytes}\n"
        f"path = Path({remote_source!r})\n"
        f"with path.open('wb') as handle:\n"
        f"    while remaining:\n"
        f"        size = min(remaining, len(block))\n"
        f"        handle.write(block[:size])\n"
        f"        remaining -= size\n"
        f"PY\n"
        f"rm -f {shlex.quote(remote_download)} {shlex.quote(remote_download)}.part.* "
        f"{shlex.quote(remote_download)}.cpnetflux.download.manifest",
    )
    remote_source_sha = run_remote(args.remote, f"sha256sum {shlex.quote(remote_source)}").stdout.split()[0]
    if remote_source_sha != source_sha:
        raise RuntimeError("remote source generation hash mismatch")

    server_cmd = [
        str(Path(args.local_build_dir) / "cpnetflux-gridftp-server"),
        "--host",
        "0.0.0.0",
        "--port",
        str(args.control_port),
        "--root",
        str(root),
        "--data-port-base",
        str(args.data_port_base),
        "--connections",
        str(args.connections),
        "--chunk-size",
        str(args.chunk_size),
        "--buffer-size",
        str(args.buffer_size),
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
    ]
    with server_log.open("w", encoding="utf-8") as log_handle:
        server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

    try:
        wait_tls(args.server_host, args.control_port, cert)
        sock, buffer = login_type_i(args.server_host, args.control_port, cert)
        with sock:
            epsv = send_command(sock, buffer, "EPSV")
            assert reply_code(epsv) == 229, epsv
            data_port = parse_epsv_port(epsv)
            assert_epsv_port_in_window(data_port, args.data_port_base)
            stor = send_command(sock, buffer, "STOR private-data-tls-upload.bin")
            assert reply_code(stor) == 150, stor
            transfer_id = parse_transfer_id(stor)
            run_remote(
                args.remote,
                f"{shlex.quote(remote_client)} --host {shlex.quote(args.server_host)} "
                f"--port {data_port} --input {shlex.quote(remote_source)} "
                f"--connections {args.connections} --chunk-size {args.chunk_size} "
                f"--buffer-size {args.buffer_size} --checksum crc32c --transfer-id {transfer_id} "
                f"--data-tls-mode required --tls-ca-file {shlex.quote(remote_ca)}",
            )
            complete = read_reply(sock, buffer)
            assert reply_code(complete) == 226, complete
            assert reply_code(send_command(sock, buffer, "QUIT")) == 221

        uploaded_sha = sha256_file(root / "private-data-tls-upload.bin")
        if uploaded_sha != source_sha:
            raise RuntimeError("private data TLS STOR hash mismatch")

        sock, buffer = login_type_i(args.server_host, args.control_port, cert)
        with sock:
            epsv = send_command(sock, buffer, "EPSV")
            assert reply_code(epsv) == 229, epsv
            data_port = parse_epsv_port(epsv)
            assert_epsv_port_in_window(data_port, args.data_port_base)
            retr = send_command(sock, buffer, "RETR private-data-tls-upload.bin")
            assert reply_code(retr) == 150, retr
            transfer_id = parse_transfer_id(retr)
            run_remote(
                args.remote,
                f"{shlex.quote(remote_download_client)} --host {shlex.quote(args.server_host)} "
                f"--port {data_port} --output {shlex.quote(remote_download)} "
                f"--connections {args.connections} --buffer-size {args.buffer_size} "
                f"--checksum crc32c --transfer-id {transfer_id} "
                f"--data-tls-mode required --tls-ca-file {shlex.quote(remote_ca)}",
            )
            complete = read_reply(sock, buffer)
            assert reply_code(complete) == 226, complete
            assert reply_code(send_command(sock, buffer, "QUIT")) == 221

        remote_download_sha = run_remote(args.remote, f"sha256sum {shlex.quote(remote_download)}").stdout.split()[0]
        if remote_download_sha != source_sha:
            raise RuntimeError("private data TLS RETR hash mismatch")
        text = server_log.read_text(encoding="utf-8", errors="replace")
        if "PRIVATE KEY" in text or "BEGIN PRIVATE KEY" in text:
            raise RuntimeError("private data TLS server log leaked private key material")
        print("gridftp private data TLS STOR/RETR smoke passed")
        print(f"server_log={server_log}")
        print(f"source_sha256={source_sha}")
        print(f"dest_sha256={remote_download_sha}")
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
        run_remote(
            args.remote,
            f"rm -f {shlex.quote(remote_ca)} {shlex.quote(remote_source)} "
            f"{shlex.quote(remote_download)} {shlex.quote(remote_download)}.part.* "
            f"{shlex.quote(remote_download)}.cpnetflux.download.manifest",
            check=False,
        )
        for path in (cert, key, local_source):
            try:
                path.unlink()
            except FileNotFoundError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private GridFTP framed data TLS smoke.")
    parser.add_argument("--remote", required=True)
    parser.add_argument("--server-host", required=True)
    parser.add_argument("--control-port", type=int, default=2121)
    parser.add_argument("--data-port-base", type=int, default=20300)
    parser.add_argument("--root", default="/tmp/cpnetflux-gridftp-data-tls-private-root")
    parser.add_argument("--local-build-dir", default="/root/projects/CPNetFlux/build")
    parser.add_argument("--remote-build-dir", default="/root/projects/CPNetFlux/build")
    parser.add_argument("--connections", type=int, default=1)
    parser.add_argument("--bytes", type=int, default=1024 * 1024)
    parser.add_argument("--chunk-size", type=int, default=1024 * 1024)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--output-dir", "--results-dir", dest="output_dir", default="tools/perf/results")
    args = parser.parse_args()
    return run_smoke(args)


if __name__ == "__main__":
    sys.exit(main())
