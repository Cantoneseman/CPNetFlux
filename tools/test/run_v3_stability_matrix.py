#!/usr/bin/env python3
"""Loopback multi-condition V3 stability regression matrix."""

from __future__ import annotations

import argparse
import re
import shutil
import signal
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.experiments.gridftp_compare.dataset import materialize_dataset, tree_hash
from gridftp_port_window import clamp_passive_data_port_base


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def make_cert(temp: Path) -> tuple[Path, Path]:
    cert, key = temp / "cert.pem", temp / "key.pem"
    openssl = shutil.which("openssl")
    if not openssl:
        raise RuntimeError("openssl CLI is required for TLS stability matrix")
    subprocess.run(
        [
            openssl, "req", "-x509", "-newkey", "rsa:2048",
            "-keyout", str(key), "-out", str(cert),
            "-sha256", "-days", "1", "-nodes", "-subj", "/CN=localhost",
        ],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
    )
    key.chmod(0o600)
    return cert, key


def wait_tcp(port: int, timeout: float = 8.0) -> None:
    deadline = time.monotonic() + timeout
    last: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return
        except OSError as error:
            last = error
            time.sleep(0.03)
    raise RuntimeError(f"server did not listen on {port}: {last}")


def tls_connect(port: int, cert: Path) -> ssl.SSLSocket:
    raw = socket.create_connection(("127.0.0.1", port), timeout=2)
    ctx = ssl.create_default_context(cafile=str(cert))
    ctx.check_hostname = False
    return ctx.wrap_socket(raw, server_hostname="localhost")


def rst_close(sock: socket.socket) -> None:
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    sock.close()


def drain_greeting(sock: ssl.SSLSocket) -> None:
    sock.settimeout(2)
    data = sock.recv(512)
    if not data.startswith(b"220"):
        raise RuntimeError(f"unexpected TLS greeting: {data!r}")


def server_alive(process: subprocess.Popen[bytes], label: str) -> None:
    if process.poll() is not None:
        raise AssertionError(f"{label}: server exited with {process.returncode}")


def run_server(binary: Path, root: Path, cert: Path, key: Path,
                control_port: int, data_port_base: int, data_tls: bool,
                log: Path) -> subprocess.Popen[bytes]:
    command = [
        str(binary), "--host", "127.0.0.1", "--port", str(control_port),
        "--root", str(root), "--data-port-base", str(data_port_base),
        "--connections", "1", "--checksum", "crc32c",
        "--tls-mode", "required", "--tls-cert-file", str(cert),
        "--tls-key-file", str(key),
    ]
    if data_tls:
        command += ["--data-tls-mode", "required"]
    handle = log.open("wb")
    process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT)
    process._stability_log_handle = handle  # type: ignore[attr-defined]
    wait_tcp(control_port)
    return process


def stop_server(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)
    handle = getattr(process, "_stability_log_handle", None)
    if handle is not None:
        handle.close()


def read_reply(sock: ssl.SSLSocket) -> bytes:
    data = bytearray()
    while b"\n" not in data:
        chunk = sock.recv(512)
        if not chunk:
            break
        data.extend(chunk)
    return bytes(data)


def login(sock: ssl.SSLSocket) -> None:
    if not read_reply(sock).startswith(b"220"):
        raise RuntimeError("missing greeting")
    sock.sendall(b"USER cpnetflux\r\n")
    if not read_reply(sock).startswith(b"331"):
        raise RuntimeError("USER failed")
    sock.sendall(b"PASS cpnetflux\r\n")
    if not read_reply(sock).startswith(b"230"):
        raise RuntimeError("PASS failed")
    sock.sendall(b"TYPE I\r\n")
    if not read_reply(sock).startswith(b"200"):
        raise RuntimeError("TYPE I failed")


def control_abrupt_rst(binary: Path, temp: Path, rounds: int) -> None:
    control = free_port()
    data = free_port()
    root = temp / "control-root"
    root.mkdir()
    cert, key = make_cert(temp)
    process = run_server(binary, root, cert, key, control, data, False, temp / "control.log")
    try:
        for _ in range(rounds):
            sock = tls_connect(control, cert)
            drain_greeting(sock)
            sock.sendall(b"NOOP\r\n")
            if not read_reply(sock).startswith(b"200"):
                raise RuntimeError("NOOP failed")
            rst_close(sock)
            time.sleep(0.03)
            server_alive(process, "control RST")
        healthy = tls_connect(control, cert)
        try:
            login(healthy)
        finally:
            healthy.close()
        server_alive(process, "control healthy reconnect")
    finally:
        stop_server(process)


def handshake_abrupt_rst(binary: Path, temp: Path, rounds: int) -> None:
    control = free_port()
    data = free_port()
    root = temp / "handshake-root"
    root.mkdir()
    cert, key = make_cert(temp)
    process = run_server(binary, root, cert, key, control, data, False, temp / "handshake.log")
    try:
        for _ in range(rounds):
            raw = socket.create_connection(("127.0.0.1", control), timeout=2)
            rst_close(raw)
            time.sleep(0.03)
            server_alive(process, "TLS handshake RST")
        healthy = tls_connect(control, cert)
        try:
            login(healthy)
        finally:
            healthy.close()
        server_alive(process, "handshake healthy reconnect")
    finally:
        stop_server(process)


def data_abrupt_rst(binary: Path, temp: Path, rounds: int) -> None:
    control = free_port()
    data = free_port()
    root = temp / "data-root"
    root.mkdir()
    cert, key = make_cert(temp)
    process = run_server(binary, root, cert, key, control, data, True, temp / "data.log")
    try:
        for index in range(rounds):
            sock = tls_connect(control, cert)
            try:
                login(sock)
                sock.sendall(b"EPSV\r\n")
                reply = read_reply(sock)
                if not reply.startswith(b"229"):
                    raise RuntimeError(f"EPSV failed: {reply!r}")
                match = re.search(rb"\(\|\|\|(\d+)\|\)", reply)
                if not match:
                    raise RuntimeError(f"EPSV port missing: {reply!r}")
                data_port = int(match.group(1))
                sock.sendall(f"STOR abrupt-{index}.bin\r\n".encode())
                opening = read_reply(sock)
                if not opening.startswith(b"150"):
                    raise RuntimeError(f"STOR failed: {opening!r}")
                data_sock = tls_connect(data_port, cert)
                rst_close(data_sock)
                rst_close(sock)
            finally:
                if sock.fileno() >= 0:
                    sock.close()
            time.sleep(0.08)
            server_alive(process, "data TLS RST")
        healthy = tls_connect(control, cert)
        try:
            login(healthy)
        finally:
            healthy.close()
        server_alive(process, "data healthy reconnect")
    finally:
        stop_server(process)


def corrupt_range_resume(binary: Path) -> None:
    smoke = Path(__file__).with_name("run_gridftp_control_retr_corrupt_resume_smoke.py")
    result = subprocess.run(
        [
            sys.executable, str(smoke), "--build-dir", str(binary.parent),
            "--bytes", str(129 * 1024 * 1024),
            "--connections", "4", "--chunk-size", str(1024 * 1024),
            "--max-chunks", "4",
        ],
        text=True, capture_output=True, check=False,
    )
    if result.returncode != 0:
        raise AssertionError(
            "range corrupt-resume matrix failed\n"
            + result.stdout + result.stderr
        )


def run_checked(command: list[str]) -> None:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        raise AssertionError(
            "directory transfer failed: " + " ".join(command) + "\n"
            + result.stdout + result.stderr
        )


def directory_matrix(build_dir: Path, temp: Path) -> None:
    control_port = free_port()
    data_port = clamp_passive_data_port_base(free_port())
    server_root = temp / "directory-server-root"
    server_root.mkdir()
    log = (temp / "directory-server.log").open("wb")
    process = subprocess.Popen(
        [
            str(build_dir / "cpnetflux-gridftp-server"),
            "--host", "127.0.0.1", "--port", str(control_port),
            "--root", str(server_root), "--data-port-base", str(data_port),
            "--connections", "4", "--chunk-size", "1048576",
            "--buffer-size", "65536", "--checksum", "crc32c",
        ],
        stdout=log, stderr=subprocess.STDOUT,
    )
    process._stability_log_handle = log  # type: ignore[attr-defined]
    try:
        wait_tcp(control_port)
        for profile in ("tree_dense_128MiB", "tree_mixed_256MiB"):
            profile_root = temp / profile
            materialize_dataset(profile_root, profile=profile, seed=20261011)
            source = profile_root / (
                "tree-dense" if profile == "tree_dense_128MiB" else "tree-mixed"
            )
            expected = tree_hash(source)
            common = [
                "--host", "127.0.0.1", "--port", str(control_port),
                "--connections", "4", "--file-parallelism", "4",
                "--chunk-size", "1048576", "--buffer-size", "65536",
                "--checksum", "crc32c", "--data-session-reuse", "tree",
                "--file-scheduling", "dynamic", "--data-pending-window", "4",
            ]
            run_checked([
                str(build_dir / "cpnetflux-tree-upload-client"), *common,
                "--source-dir", str(source), "--dest-dir", profile,
            ])
            server_alive(process, f"{profile} upload")
            uploaded = tree_hash(server_root / profile)
            if uploaded != expected:
                raise AssertionError(f"{profile} upload hash/count/size mismatch")
            destination = temp / (profile + "-download")
            run_checked([
                str(build_dir / "cpnetflux-tree-download-client"), *common,
                "--source-dir", profile, "--dest-dir", str(destination),
            ])
            server_alive(process, f"{profile} download")
            downloaded = tree_hash(destination)
            if downloaded != expected:
                raise AssertionError(f"{profile} download hash/count/size mismatch")
            print(
                f"{profile} passed: files={expected[1]} bytes={expected[2]} "
                f"tree_sha256={expected[0]}"
            )
    finally:
        stop_server(process)

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True)
    parser.add_argument("--rounds", type=int, default=8)
    args = parser.parse_args()
    if args.rounds < 2 or args.rounds > 32:
        raise SystemExit("--rounds must be in 2..32")
    binary = Path(args.build_dir) / "cpnetflux-gridftp-server"
    if not binary.exists():
        raise FileNotFoundError(binary)
    corrupt_range_resume(binary)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-v3-stability.") as text:
        temp = Path(text)
        (temp / "control-case").mkdir()
        (temp / "handshake-case").mkdir()
        (temp / "data-case").mkdir()
        directory_matrix(Path(args.build_dir), temp)
        control_abrupt_rst(binary, temp / "control-case", args.rounds)
        handshake_abrupt_rst(binary, temp / "handshake-case", args.rounds)
        data_abrupt_rst(binary, temp / "data-case", args.rounds)
    print(f"V3 stability matrix passed: TLS control/data/handshake RST x {args.rounds} rounds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
