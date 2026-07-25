#!/usr/bin/env python3
"""Private-network control-plane TLS metadata smoke."""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import socket
import ssl
import subprocess
import sys
import time
from pathlib import Path


def ssh_prefix(remote: str) -> list[str]:
    if os.environ.get("GRIDFLUX_SSH_PASSWORD") or os.environ.get("SSHPASS"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no", remote]
    return ["ssh", "-o", "StrictHostKeyChecking=no", remote]


def run_remote(
    remote: str,
    command: str,
    *,
    input_text: str | None = None,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env.get("GRIDFLUX_SSH_PASSWORD") and not env.get("SSHPASS"):
        env["SSHPASS"] = env["GRIDFLUX_SSH_PASSWORD"]
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


def generate_cert(cert: Path, key: Path) -> None:
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("openssl CLI is required for private TLS smoke")
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


def plaintext_must_fail(host: str, port: int) -> None:
    with socket.create_connection((host, port), timeout=3.0) as sock:
        sock.sendall(b"USER gridflux\r\n")
        try:
            data = sock.recv(128)
        except ConnectionResetError:
            return
        if data.startswith(b"220") or data.startswith(b"331"):
            raise RuntimeError(f"plaintext control unexpectedly succeeded: {data!r}")


def run_remote_tls_client(remote: str, host: str, port: int, remote_ca: str) -> None:
    script = r'''
import socket
import ssl
import sys
import time

host = sys.argv[1]
port = int(sys.argv[2])
cafile = sys.argv[3]

def read_line(sock, buffer):
    while b"\n" not in buffer:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("control connection closed")
        buffer.extend(chunk)
    index = buffer.index(ord("\n"))
    line = bytes(buffer[: index + 1]).decode("utf-8", errors="replace").rstrip("\r\n")
    del buffer[: index + 1]
    return line

def read_reply(sock, buffer):
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

def code(lines):
    return int(lines[0][:3]) if lines and len(lines[0]) >= 3 and lines[0][:3].isdigit() else 0

def send(sock, buffer, command):
    sock.sendall((command + "\r\n").encode("utf-8"))
    return read_reply(sock, buffer)

def plaintext_must_fail():
    try:
        with socket.create_connection((host, port), timeout=3.0) as sock:
            sock.sendall(b"USER gridflux\r\n")
            data = sock.recv(128)
            if data.startswith(b"220") or data.startswith(b"331"):
                raise RuntimeError("plaintext control unexpectedly succeeded")
    except (ConnectionResetError, OSError, TimeoutError):
        return

def tls_connect():
    context = ssl.create_default_context(cafile=cafile)
    context.check_hostname = False
    raw = socket.create_connection((host, port), timeout=5.0)
    sock = context.wrap_socket(raw, server_hostname="localhost")
    buffer = bytearray()
    greeting = read_reply(sock, buffer)
    return sock, buffer, greeting

plaintext_must_fail()
deadline = time.monotonic() + 15.0
last_error = None
while time.monotonic() < deadline:
    try:
        sock, buffer, greeting = tls_connect()
        break
    except (OSError, ssl.SSLError, RuntimeError) as error:
        last_error = error
        time.sleep(0.1)
else:
    raise RuntimeError(f"failed to connect TLS control server from remote: {last_error}")

with sock:
    assert code(greeting) == 220, greeting
    assert code(send(sock, buffer, "USER gridflux")) == 331
    assert code(send(sock, buffer, "PASS gridflux")) == 230
    size = send(sock, buffer, "SIZE alpha.bin")
    assert code(size) == 213, size
    assert code(send(sock, buffer, "QUIT")) == 221

print("remote_tls_control_ok")
'''
    command = f"python3 - {shlex.quote(host)} {port} {shlex.quote(remote_ca)}"
    run_remote(remote, command, input_text=script)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private GridFTP TLS metadata smoke.")
    parser.add_argument("--remote", required=True)
    parser.add_argument("--server-host", required=True)
    parser.add_argument("--control-port", type=int, default=2121)
    parser.add_argument("--root", default="/tmp/gridflux-gridftp-tls-private-root")
    parser.add_argument("--local-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--data-port-base", type=int, default=20300)
    parser.add_argument("--output-dir", "--results-dir", dest="output_dir", default="tools/perf/results")
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    server_log = output_dir / f"{timestamp}_gridftp-tls-private.log"
    cert = output_dir / f"{timestamp}_tls-cert.pem"
    key = output_dir / f"{timestamp}_tls-key.pem"
    remote_ca = f"/tmp/gridflux-tls-private-{timestamp}-{os.getpid()}-ca.pem"
    generate_cert(cert, key)
    write_remote_text(args.remote, remote_ca, cert.read_text(encoding="utf-8"))

    root = Path(args.root)
    subprocess.run(["rm", "-rf", str(root)], check=True)
    root.mkdir(parents=True)
    (root / "alpha.bin").write_bytes(b"private tls metadata smoke")
    run_remote(args.remote, "true")
    server_cmd = [
        str(Path(args.local_build_dir) / "gridflux-gridftp-server"),
        "--host",
        args.server_host,
        "--port",
        str(args.control_port),
        "--root",
        str(root),
        "--data-port-base",
        str(args.data_port_base),
        "--tls-mode",
        "required",
        "--tls-cert-file",
        str(cert),
        "--tls-key-file",
        str(key),
    ]
    with server_log.open("w", encoding="utf-8") as log_handle:
        server = subprocess.Popen(server_cmd, stdout=log_handle, stderr=subprocess.STDOUT)

    try:
        wait_tls(args.server_host, args.control_port, cert)
        plaintext_must_fail(args.server_host, args.control_port)
        run_remote_tls_client(args.remote, args.server_host, args.control_port, remote_ca)
        sock, buffer, greeting = tls_connect(args.server_host, args.control_port, cert)
        with sock:
            assert reply_code(greeting) == 220, greeting
            assert reply_code(send_command(sock, buffer, "USER gridflux")) == 331
            assert reply_code(send_command(sock, buffer, "PASS gridflux")) == 230
            size = send_command(sock, buffer, "SIZE alpha.bin")
            assert reply_code(size) == 213, size
            assert reply_code(send_command(sock, buffer, "QUIT")) == 221
        text = server_log.read_text(encoding="utf-8", errors="replace")
        if "PRIVATE KEY" in text or "BEGIN PRIVATE KEY" in text:
            raise RuntimeError("server log leaked private key material")
        print("gridftp private TLS metadata smoke passed")
        print(f"server_log={server_log}")
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
        for path in (cert, key):
            try:
                path.unlink()
            except FileNotFoundError:
                pass
        run_remote(args.remote, f"rm -f {shlex.quote(remote_ca)}", check=False)


if __name__ == "__main__":
    raise SystemExit(main())
