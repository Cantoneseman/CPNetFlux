#!/usr/bin/env python3
"""Loopback smoke for CPNetFlux mutual TLS and token authentication."""
from __future__ import annotations

import argparse
import shutil
import socket
import ssl
import subprocess
import tempfile
import time
from pathlib import Path


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def openssl_run(openssl: str, args: list[str]) -> None:
    subprocess.run([openssl, *args], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def make_credentials(root: Path) -> tuple[Path, Path, Path, Path, Path, Path]:
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("openssl CLI is required for mTLS smoke")
    ca_key, ca_cert = root / "ca.key", root / "ca.pem"
    server_key, server_csr, server_cert = root / "server.key", root / "server.csr", root / "server.pem"
    client_key, client_csr, client_cert = root / "client.key", root / "client.csr", root / "client.pem"
    openssl_run(openssl, ["req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                          "-subj", "/CN=CPNetFlux Test CA", "-keyout", str(ca_key), "-out", str(ca_cert)])
    for key, csr, subject in [(server_key, server_csr, "/CN=localhost"),
                              (client_key, client_csr, "/CN=CPNetFlux Test Client")]:
        openssl_run(openssl, ["req", "-newkey", "rsa:2048", "-nodes", "-subj", subject,
                              "-keyout", str(key), "-out", str(csr)])
    for key in (ca_key, server_key, client_key):
        key.chmod(0o600)
    openssl_run(openssl, ["x509", "-req", "-in", str(server_csr), "-CA", str(ca_cert),
                          "-CAkey", str(ca_key), "-CAcreateserial", "-days", "1", "-out", str(server_cert)])
    openssl_run(openssl, ["x509", "-req", "-in", str(client_csr), "-CA", str(ca_cert),
                          "-CAkey", str(ca_key), "-CAcreateserial", "-days", "1", "-out", str(client_cert)])
    return ca_cert, server_cert, server_key, client_cert, client_key, ca_key


def read_line(sock: ssl.SSLSocket) -> str:
    data = bytearray()
    while b"\n" not in data:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("control connection closed")
        data.extend(chunk)
    return bytes(data[: data.index(b"\n") + 1]).decode(errors="replace").rstrip("\r\n")


def command(sock: ssl.SSLSocket, text: str) -> int:
    sock.sendall((text + "\r\n").encode())
    line = read_line(sock)
    return int(line[:3]) if line[:3].isdigit() else 0


def connect(port: int, ca: Path, cert: Path | None, key: Path | None,
            hostname: str = "localhost") -> ssl.SSLSocket:
    context = ssl.create_default_context(cafile=str(ca))
    if cert is not None and key is not None:
        context.load_cert_chain(certfile=str(cert), keyfile=str(key))
    raw = socket.create_connection(("127.0.0.1", port), timeout=5)
    return context.wrap_socket(raw, server_hostname=hostname)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    args = parser.parse_args()
    server_bin = Path(args.build_dir) / "cpnetflux-gridftp-server"
    if not server_bin.exists():
        raise FileNotFoundError(server_bin)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-mtls-smoke.") as text:
        root = Path(text)
        data_root = root / "data"
        data_root.mkdir()
        ca, server_cert, server_key, client_cert, client_key, _ = make_credentials(root)
        token = root / "token"
        token.write_text("loopback-token\n")
        token.chmod(0o600)
        port = free_port()
        command_line = [str(server_bin), "--host", "127.0.0.1", "--port", str(port),
                        "--root", str(data_root), "--auth-mode", "token", "--auth-token-file", str(token),
                        "--tls-mode", "required", "--tls-cert-file", str(server_cert),
                        "--tls-key-file", str(server_key), "--tls-ca-file", str(ca),
                        "--tls-client-auth", "required", "--access-policy", "read-only"]
        server = subprocess.Popen(command_line, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if server.poll() is not None:
                    raise RuntimeError("mTLS server exited before accepting connections")
                try:
                    with connect(port, ca, client_cert, client_key) as sock:
                        if read_line(sock) == "220 CPNetFlux GridFTP control ready":
                            break
                except (ssl.SSLError, OSError, RuntimeError):
                    time.sleep(0.05)
            else:
                raise RuntimeError("mTLS server did not become ready")

            try:
                with connect(port, ca, None, None) as sock:
                    greeting = read_line(sock)
                    if greeting == "220 CPNetFlux GridFTP control ready":
                        raise RuntimeError("TLS connection without client certificate unexpectedly succeeded")
            except (ssl.SSLError, OSError, RuntimeError) as error:
                if str(error) == "TLS connection without client certificate unexpectedly succeeded":
                    raise

            try:
                with connect(port, ca, client_cert, client_key, "wrong.example"):
                    raise RuntimeError("TLS connection with wrong server hostname unexpectedly succeeded")
            except ssl.SSLError:
                pass

            with connect(port, ca, client_cert, client_key) as sock:
                assert read_line(sock) == "220 CPNetFlux GridFTP control ready"
                user_code = command(sock, "USER token")
                assert user_code == 331, f"USER token returned {user_code}"
                assert command(sock, "PASS wrong-token") == 530
                assert command(sock, "PASS loopback-token") == 230
                assert command(sock, "TYPE I") == 200
                assert command(sock, "EPSV") == 229
                assert command(sock, "STOR denied.bin") == 550
                assert command(sock, "XDIR PUT denied-directory") == 550
        finally:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)

        write_only_port = free_port()
        write_only_command = command_line.copy()
        write_only_command[write_only_command.index("--port") + 1] = str(write_only_port)
        write_only_command[write_only_command.index("--access-policy") + 1] = "write-only"
        write_only_server = subprocess.Popen(
            write_only_command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if write_only_server.poll() is not None:
                    raise RuntimeError("write-only mTLS server exited before accepting connections")
                try:
                    with connect(write_only_port, ca, client_cert, client_key) as sock:
                        if read_line(sock) == "220 CPNetFlux GridFTP control ready":
                            break
                except (ssl.SSLError, OSError, RuntimeError):
                    time.sleep(0.05)
            else:
                raise RuntimeError("write-only mTLS server did not become ready")
            with connect(write_only_port, ca, client_cert, client_key) as sock:
                assert read_line(sock) == "220 CPNetFlux GridFTP control ready"
                assert command(sock, "USER token") == 331
                assert command(sock, "PASS loopback-token") == 230
                assert command(sock, "SIZE hidden.bin") == 550
                assert command(sock, "XDIR GET hidden-directory") == 550
        finally:
            write_only_server.terminate()
            try:
                write_only_server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                write_only_server.kill()
                write_only_server.wait(timeout=5)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
