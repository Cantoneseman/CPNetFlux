#!/usr/bin/env python3
import hashlib
import os
import socket
import subprocess
import time
from pathlib import Path

from gridftp_port_window import clamp_passive_data_port_base


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def make_tree(root: Path) -> None:
    (root / "nested" / "deeper").mkdir(parents=True)
    (root / "empty.bin").write_bytes(b"")
    (root / "alpha.txt").write_bytes(b"alpha\n")
    (root / "nested" / "beta.bin").write_bytes(bytes(index % 251 for index in range(131072)))
    (root / "nested" / "deeper" / "gamma.bin").write_bytes(
        bytes((index * 17) % 251 for index in range(1024 * 1024 + 17))
    )


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_hash(root: Path) -> tuple[str, int, int]:
    digest = hashlib.sha256()
    file_count = 0
    total_bytes = 0
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        relative = path.relative_to(root).as_posix()
        if ".cpnetflux." in relative or ".part." in relative:
            continue
        size = path.stat().st_size
        file_count += 1
        total_bytes += size
        digest.update(relative.encode("utf-8") + b"\0")
        digest.update(str(size).encode("ascii") + b"\0")
        digest.update(file_sha256(path).encode("ascii") + b"\0")
    return digest.hexdigest(), file_count, total_bytes


def start_server(
    build_dir: Path,
    root: Path,
    control_port: int,
    data_port_base: int,
    log: Path,
    *,
    extra_env: dict[str, str] | None = None,
):
    data_port_base = clamp_passive_data_port_base(data_port_base)
    cmd = [
        str(build_dir / "cpnetflux-gridftp-server"),
        "--host",
        "0.0.0.0",
        "--port",
        str(control_port),
        "--root",
        str(root),
        "--data-port-base",
        str(data_port_base),
        "--connections",
        "2",
        "--chunk-size",
        "1048576",
        "--buffer-size",
        "65536",
        "--checksum",
        "crc32c",
        "--checksum-backend",
        "auto",
    ]
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    handle = log.open("w", encoding="utf-8")
    process = subprocess.Popen(cmd, stdout=handle, stderr=subprocess.STDOUT, env=env)
    handle.close()
    wait_for_control(control_port)
    return process


def wait_for_control(port: int) -> None:
    deadline = time.monotonic() + 10
    last_error: Exception | None = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1.0) as sock:
                sock.recv(512)
                return
        except OSError as error:
            last_error = error
            time.sleep(0.05)
    raise RuntimeError(f"control server did not start: {last_error}")


def stop_server(process: subprocess.Popen, log: Path) -> None:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
    if process.returncode not in (0, -15, -9):
        raise RuntimeError(log.read_text(encoding="utf-8", errors="replace"))


def run_checked(cmd: list[str], *, expect_success: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(cmd, text=True, capture_output=True, check=False)
    if expect_success and completed.returncode != 0:
        raise RuntimeError(
            "$ " + " ".join(cmd) + "\n" + completed.stdout + completed.stderr
        )
    if not expect_success and completed.returncode == 0:
        raise RuntimeError("command unexpectedly succeeded: " + " ".join(cmd))
    return completed


def clean_cpnetflux_tree_artifacts(root: Path) -> None:
    for path in root.rglob("*"):
        if path.is_file() and (".cpnetflux." in path.name or ".part." in path.name):
            path.unlink()


def env_without_password() -> dict[str, str]:
    env = os.environ.copy()
    env.pop("CPNETFLUX_SSH_PASSWORD", None)
    env.pop("SSHPASS", None)
    return env
