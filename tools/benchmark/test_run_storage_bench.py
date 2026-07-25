#!/usr/bin/env python3
"""Regression tests for run_storage_bench.py wrapper behavior."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def write_executable(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    path.chmod(0o755)


def fake_bench_text() -> str:
    return """#!/usr/bin/env python3
import sys

def arg(name, default=""):
    try:
        return sys.argv[sys.argv.index(name) + 1]
    except ValueError:
        return default

mode = arg("--mode", "write")
bytes_value = arg("--bytes", "1024")
buffer_size = arg("--buffer-size", "1024")
iterations = arg("--iterations", "1")
preallocate = arg("--preallocate", "off")
backend = arg("--file-io-backend", "posix")
queue_depth = arg("--file-io-queue-depth", "1")
batch_size = arg("--file-io-batch-size", "1")
advice = arg("--file-io-advice", "off")
file_io_buffer = arg("--file-io-buffer-size", "0")
strategy = arg("--posix-write-strategy", "auto")
effective = "coalesced" if strategy == "coalesced" or (strategy == "auto" and file_io_buffer != "0") else "direct"
for iteration in range(int(iterations)):
    print(
        "storage_bench "
        f"operation={mode} bytes={bytes_value} iterations={iterations} "
        f"buffer_size={buffer_size} preallocate={preallocate} "
        f"file_io_backend={backend} file_io_buffer_size={file_io_buffer} "
        f"file_io_queue_depth={queue_depth} file_io_batch_size={batch_size} "
        f"file_io_advice={advice} posix_write_strategy={strategy} "
        f"posix_write_strategy_effective={effective} "
        f"iteration={iteration} aggregate=false elapsed_seconds=0.010000 "
        "throughput_gbps=1.000000 read_call_count=1 write_call_count=1 "
        "write_syscall_count=1 write_retry_count=0 write_short_count=0 "
        "write_zero_count=0 write_total_bytes=1024 "
        "avg_read_bytes_per_call=1024 avg_write_bytes_per_call=1024 "
        "write_avg_bytes_per_syscall=1024 "
        "file_io_wait_seconds=0.001000 io_uring_submit_count=2 "
        "io_uring_wait_count=2 io_uring_completion_count=2 io_uring_sqe_count=2 "
        "io_uring_partial_completion_count=0 io_uring_retry_count=0 "
        "io_uring_avg_bytes_per_sqe=512 result=pass"
    )
print(
    "storage_bench "
    f"operation={mode} bytes={bytes_value} iterations={iterations} "
    f"buffer_size={buffer_size} preallocate={preallocate} "
    f"file_io_backend={backend} file_io_buffer_size={file_io_buffer} "
    f"file_io_queue_depth={queue_depth} file_io_batch_size={batch_size} "
    f"file_io_advice={advice} posix_write_strategy={strategy} "
    f"posix_write_strategy_effective={effective} "
    "iteration=aggregate aggregate=true elapsed_seconds=0.010000 "
    "throughput_gbps=1.000000 read_call_count=1 write_call_count=1 "
    "write_syscall_count=1 write_retry_count=0 write_short_count=0 "
    "write_zero_count=0 write_total_bytes=1024 "
    "avg_read_bytes_per_call=1024 avg_write_bytes_per_call=1024 "
    "write_avg_bytes_per_syscall=1024 "
    "file_io_wait_seconds=0.001000 io_uring_submit_count=2 "
    "io_uring_wait_count=2 io_uring_completion_count=2 io_uring_sqe_count=2 "
    "io_uring_partial_completion_count=0 io_uring_retry_count=0 "
    "io_uring_avg_bytes_per_sqe=512 result=pass"
)
"""


def fake_ssh_text(log_path: Path) -> str:
    return f"""#!/usr/bin/env python3
import sys
from pathlib import Path

Path({str(log_path)!r}).parent.mkdir(parents=True, exist_ok=True)
with Path({str(log_path)!r}).open("a", encoding="utf-8") as handle:
    handle.write(" ".join(sys.argv[1:]) + "\\n")

command = sys.argv[-1] if len(sys.argv) > 1 else ""
if "df -PT" in command:
    print("/dev/fake ext4 1048576 1 1048575 1% /tmp")
elif command == "hostname":
    print("fake-remote")
elif command == "uname -r":
    print("5.15.0-fake")
elif "gridflux-storage-bench" in command:
    print("storage_bench operation=write bytes=1024 iterations=1 buffer_size=1024 preallocate=off file_io_backend=posix file_io_buffer_size=0 file_io_queue_depth=1 file_io_batch_size=1 file_io_advice=off posix_write_strategy=auto posix_write_strategy_effective=direct iteration=0 aggregate=false elapsed_seconds=0.010000 throughput_gbps=1.000000 read_call_count=1 write_call_count=1 write_syscall_count=1 write_retry_count=0 write_short_count=0 write_zero_count=0 write_total_bytes=1024 avg_read_bytes_per_call=1024 avg_write_bytes_per_call=1024 write_avg_bytes_per_syscall=1024 file_io_wait_seconds=0.001000 io_uring_submit_count=2 io_uring_wait_count=2 io_uring_completion_count=2 io_uring_sqe_count=2 io_uring_partial_completion_count=0 io_uring_retry_count=0 io_uring_avg_bytes_per_sqe=512 result=pass")
    print("storage_bench operation=write bytes=1024 iterations=1 buffer_size=1024 preallocate=off file_io_backend=posix file_io_buffer_size=0 file_io_queue_depth=1 file_io_batch_size=1 file_io_advice=off posix_write_strategy=auto posix_write_strategy_effective=direct iteration=aggregate aggregate=true elapsed_seconds=0.010000 throughput_gbps=1.000000 read_call_count=1 write_call_count=1 write_syscall_count=1 write_retry_count=0 write_short_count=0 write_zero_count=0 write_total_bytes=1024 avg_read_bytes_per_call=1024 avg_write_bytes_per_call=1024 write_avg_bytes_per_syscall=1024 file_io_wait_seconds=0.001000 io_uring_submit_count=2 io_uring_wait_count=2 io_uring_completion_count=2 io_uring_sqe_count=2 io_uring_partial_completion_count=0 io_uring_retry_count=0 io_uring_avg_bytes_per_sqe=512 result=pass")
sys.exit(0)
"""


def run_wrapper(script: Path, args: list[str], *, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(script)] + args,
        text=True,
        capture_output=True,
        check=False,
        env=env,
        timeout=30,
    )


def assert_success(completed: subprocess.CompletedProcess[str]) -> None:
    if completed.returncode != 0:
        raise AssertionError(
            f"command failed with {completed.returncode}\nstdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )


def test_local_side_does_not_call_ssh(script: Path, temp: Path) -> None:
    build_dir = temp / "build"
    write_executable(build_dir / "gridflux-storage-bench", fake_bench_text())
    ssh_log = temp / "ssh.log"
    fake_bin = temp / "bin"
    write_executable(
        fake_bin / "ssh",
        f"#!/bin/sh\necho \"$@\" >> {ssh_log}\nexit 91\n",
    )

    env = os.environ.copy()
    env.pop("GRIDFLUX_SSH_PASSWORD", None)
    env.pop("SSHPASS", None)
    env["PATH"] = f"{fake_bin}:{env['PATH']}"
    completed = run_wrapper(
        script,
        [
            "--side",
            "local",
            "--remote",
            "no-such-remote",
            "--build-dir",
            str(build_dir),
            "--output-dir",
            str(temp / "results-local"),
            "--bytes",
            "1024",
            "--modes",
            "write",
            "--preallocates",
            "off",
            "--buffer-sizes",
            "1024",
            "--iterations",
            "1",
        ],
        env=env,
    )
    assert_success(completed)
    if ssh_log.exists():
        raise AssertionError(f"--side local unexpectedly invoked ssh: {ssh_log.read_text(encoding='utf-8')}")


def test_remote_side_uses_ssh(script: Path, temp: Path) -> None:
    ssh_log = temp / "ssh-remote.log"
    fake_bin = temp / "bin-remote"
    write_executable(fake_bin / "ssh", fake_ssh_text(ssh_log))

    env = os.environ.copy()
    env.pop("GRIDFLUX_SSH_PASSWORD", None)
    env.pop("SSHPASS", None)
    env["PATH"] = f"{fake_bin}:{env['PATH']}"
    completed = run_wrapper(
        script,
        [
            "--side",
            "remote",
            "--remote",
            "fake@remote",
            "--remote-build-dir",
            "/remote/build",
            "--output-dir",
            str(temp / "results-remote"),
            "--bytes",
            "1024",
            "--modes",
            "write",
            "--preallocates",
            "off",
            "--buffer-sizes",
            "1024",
            "--iterations",
            "1",
        ],
        env=env,
    )
    assert_success(completed)
    ssh_text = ssh_log.read_text(encoding="utf-8")
    if "df -PT" not in ssh_text or "gridflux-storage-bench" not in ssh_text:
        raise AssertionError(f"--side remote did not exercise expected remote commands:\n{ssh_text}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", default="")
    args = parser.parse_args()

    script = Path(args.script) if args.script else Path(__file__).with_name("run_storage_bench.py")
    with tempfile.TemporaryDirectory(prefix="gridflux-storage-wrapper-test-") as tmp:
        temp = Path(tmp)
        test_local_side_does_not_call_ssh(script, temp)
        test_remote_side_uses_ssh(script, temp)
    print("storage bench wrapper tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
