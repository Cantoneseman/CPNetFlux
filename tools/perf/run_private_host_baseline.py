#!/usr/bin/env python3
"""Collect private host/link baselines for GridFlux Phase 4B.

The script runs on machine one. It never installs packages. If iperf3 or fio is
missing, it falls back to the existing GridFlux memory sink and a small Python
sequential IO probe.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import shlex
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


CSV_FIELDS = [
    "timestamp",
    "side",
    "category",
    "tool",
    "bytes",
    "elapsed_seconds",
    "throughput_gbps",
    "checksum_backend",
    "hostname",
    "kernel",
    "cpu_flags",
    "fs_type",
    "free_bytes",
    "log",
    "result",
    "error",
]


def compact_timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def timestamp_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def ssh_prefix(remote: str) -> list[str]:
    if os.environ.get("GRIDFLUX_SSH_PASSWORD"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no", remote]
    return ["ssh", "-o", "StrictHostKeyChecking=no", remote]


def run_local(command: list[str], *, check: bool = False, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=True, check=check, timeout=timeout)


def run_remote(remote: str, command: str, *, check: bool = False, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env.get("GRIDFLUX_SSH_PASSWORD") and not env.get("SSHPASS"):
        env["SSHPASS"] = env["GRIDFLUX_SSH_PASSWORD"]
    completed = subprocess.run(
        ssh_prefix(remote) + [command],
        text=True,
        capture_output=True,
        check=False,
        timeout=timeout,
        env=env,
    )
    if check and completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return completed


def command_exists(command: str) -> bool:
    return run_local(["bash", "-lc", f"command -v {shlex.quote(command)} >/dev/null 2>&1"]).returncode == 0


def remote_command_exists(remote: str, command: str) -> bool:
    return run_remote(remote, f"command -v {shlex.quote(command)} >/dev/null 2>&1").returncode == 0


def parse_size(text: str) -> int:
    value = text.strip().lower()
    suffixes = [("gib", 1024**3), ("g", 1024**3), ("mib", 1024**2), ("m", 1024**2)]
    for suffix, multiplier in suffixes:
        if value.endswith(suffix):
            return int(value[: -len(suffix)]) * multiplier
    return int(value)


def write_log(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def local_env(path: str) -> dict[str, str]:
    flags = ""
    try:
        with open("/proc/cpuinfo", "r", encoding="utf-8") as handle:
            for line in handle:
                if line.lower().startswith("flags"):
                    flags = line.split(":", 1)[1].strip()
                    break
    except OSError:
        pass
    df = run_local(["df", "-PT", path]).stdout.strip().splitlines()
    fs_type = ""
    free_bytes = ""
    if len(df) >= 2:
        parts = df[1].split()
        fs_type = parts[1] if len(parts) > 1 else ""
        free_bytes = str(int(parts[4]) * 1024) if len(parts) > 4 and parts[4].isdigit() else ""
    return {
        "hostname": run_local(["hostname"]).stdout.strip(),
        "kernel": run_local(["uname", "-r"]).stdout.strip(),
        "cpu_flags": flags,
        "fs_type": fs_type,
        "free_bytes": free_bytes,
    }


def remote_env(remote: str, path: str) -> dict[str, str]:
    script = (
        "printf 'hostname='; hostname; "
        "printf 'kernel='; uname -r; "
        "printf 'cpu_flags='; awk -F: '/^flags/ {gsub(/^ /, \"\", $2); print $2; exit}' /proc/cpuinfo; "
        f"printf 'df='; df -PT {shlex.quote(path)} | tail -n 1"
    )
    output = run_remote(remote, script, timeout=30).stdout
    result = {"hostname": "", "kernel": "", "cpu_flags": "", "fs_type": "", "free_bytes": ""}
    for line in output.splitlines():
        if line.startswith("hostname="):
            result["hostname"] = line.split("=", 1)[1]
        elif line.startswith("kernel="):
            result["kernel"] = line.split("=", 1)[1]
        elif line.startswith("cpu_flags="):
            result["cpu_flags"] = line.split("=", 1)[1]
        elif line.startswith("df="):
            parts = line.split("=", 1)[1].split()
            result["fs_type"] = parts[1] if len(parts) > 1 else ""
            result["free_bytes"] = (
                str(int(parts[4]) * 1024) if len(parts) > 4 and parts[4].isdigit() else ""
            )
    return result


def base_row(side: str, category: str, tool: str, bytes_count: int, env: dict[str, str], log: Path) -> dict[str, str]:
    row = {
        "timestamp": timestamp_utc(),
        "side": side,
        "category": category,
        "tool": tool,
        "bytes": str(bytes_count),
        "elapsed_seconds": "",
        "throughput_gbps": "",
        "checksum_backend": "",
        "hostname": env.get("hostname", ""),
        "kernel": env.get("kernel", ""),
        "cpu_flags": env.get("cpu_flags", ""),
        "fs_type": env.get("fs_type", ""),
        "free_bytes": env.get("free_bytes", ""),
        "log": str(log),
        "result": "fail",
        "error": "",
    }
    return row


def throughput_gbps(bytes_count: int, elapsed: float) -> str:
    if elapsed <= 0:
        return ""
    return f"{(bytes_count * 8.0 / elapsed) / 1_000_000_000.0:.6f}"


def run_python_io_probe(path: str, bytes_count: int) -> str:
    script = r"""
import os
import sys
import time
path = sys.argv[1]
bytes_count = int(sys.argv[2])
block = bytes((index * 17 + 3) % 251 for index in range(1024 * 1024))
os.makedirs(os.path.dirname(path), exist_ok=True)
start = time.monotonic()
remaining = bytes_count
with open(path, "wb") as handle:
    while remaining > 0:
        size = min(remaining, len(block))
        handle.write(block[:size])
        remaining -= size
    handle.flush()
    os.fsync(handle.fileno())
write_elapsed = time.monotonic() - start
start = time.monotonic()
read_bytes = 0
with open(path, "rb") as handle:
    while True:
        chunk = handle.read(1024 * 1024)
        if not chunk:
            break
        read_bytes += len(chunk)
read_elapsed = time.monotonic() - start
os.unlink(path)
print(f"write_elapsed_seconds={write_elapsed}")
print(f"read_elapsed_seconds={read_elapsed}")
print(f"read_bytes={read_bytes}")
"""
    return script


def run_disk_fallback_local(args: argparse.Namespace, env: dict[str, str], output_dir: Path) -> list[dict[str, str]]:
    log = output_dir / f"{compact_timestamp()}_local-disk-python.log"
    path = f"/tmp/gridflux-phase4b-local-disk-{os.getpid()}.bin"
    # The script is passed through stdin to avoid leaving helper files behind.
    completed = subprocess.run(
        ["python3", "-", path, str(args.bytes)],
        input=run_python_io_probe(path, args.bytes),
        text=True,
        capture_output=True,
        timeout=args.timeout,
        check=False,
    )
    write_log(log, completed.stdout + completed.stderr)
    return disk_rows_from_probe("server", "python", args.bytes, env, log, completed)


def run_disk_fallback_remote(args: argparse.Namespace, env: dict[str, str], output_dir: Path) -> list[dict[str, str]]:
    log = output_dir / f"{compact_timestamp()}_remote-disk-python.log"
    path = f"/tmp/gridflux-phase4b-remote-disk-{os.getpid()}.bin"
    ssh_command = ssh_prefix(args.remote) + [f"python3 - {shlex.quote(path)} {args.bytes}"]
    env_vars = os.environ.copy()
    if env_vars.get("GRIDFLUX_SSH_PASSWORD") and not env_vars.get("SSHPASS"):
        env_vars["SSHPASS"] = env_vars["GRIDFLUX_SSH_PASSWORD"]
    completed = subprocess.run(
        ssh_command,
        input=run_python_io_probe(path, args.bytes),
        text=True,
        capture_output=True,
        timeout=args.timeout,
        check=False,
        env=env_vars,
    )
    write_log(log, completed.stdout + completed.stderr)
    return disk_rows_from_probe("client", "python", args.bytes, env, log, completed)


def disk_rows_from_probe(
    side: str,
    tool: str,
    bytes_count: int,
    env: dict[str, str],
    log: Path,
    completed: subprocess.CompletedProcess[str],
) -> list[dict[str, str]]:
    metrics = dict(line.split("=", 1) for line in completed.stdout.splitlines() if "=" in line)
    rows: list[dict[str, str]] = []
    for category, key in (("disk_write", "write_elapsed_seconds"), ("disk_read", "read_elapsed_seconds")):
        row = base_row(side, category, tool, bytes_count, env, log)
        if completed.returncode == 0 and key in metrics:
            row["elapsed_seconds"] = metrics[key]
            row["throughput_gbps"] = throughput_gbps(bytes_count, float(metrics[key]))
            row["result"] = "pass"
        else:
            row["error"] = (completed.stdout + completed.stderr).replace("\n", " ")[:1000]
        rows.append(row)
    return rows


def fio_row_from_output(
    side: str,
    category: str,
    bytes_count: int,
    env: dict[str, str],
    log: Path,
    completed: subprocess.CompletedProcess[str],
) -> dict[str, str]:
    row = base_row(side, category, "fio", bytes_count, env, log)
    text = completed.stdout + completed.stderr
    try:
        json_start = text.index("{")
        data = json.loads(text[json_start:])
        job = data["jobs"][0]
        section = "write" if category == "disk_write" else "read"
        metrics = job[section]
        elapsed = float(metrics.get("runtime", 0)) / 1000.0
        io_bytes = int(metrics.get("io_bytes", bytes_count))
        bw_bytes = float(metrics.get("bw_bytes", 0))
        row["bytes"] = str(io_bytes)
        row["elapsed_seconds"] = f"{elapsed:.6f}"
        row["throughput_gbps"] = f"{(bw_bytes * 8.0) / 1_000_000_000.0:.6f}"
        row["result"] = "pass" if completed.returncode == 0 else "fail"
    except (ValueError, KeyError, IndexError, TypeError, json.JSONDecodeError) as error:
        row["error"] = str(error)
    if row["result"] != "pass" and not row["error"]:
        row["error"] = text.replace("\n", " ")[:1000]
    return row


def run_disk_fio_local(args: argparse.Namespace, env: dict[str, str], output_dir: Path) -> list[dict[str, str]]:
    path = f"/tmp/gridflux-phase4b-local-fio-{os.getpid()}.bin"
    rows: list[dict[str, str]] = []
    for category, rw in (("disk_write", "write"), ("disk_read", "read")):
        log = output_dir / f"{compact_timestamp()}_local-disk-fio-{rw}.log"
        command = [
            "fio",
            "--name=gridflux-phase4b",
            f"--filename={path}",
            f"--rw={rw}",
            "--bs=1M",
            f"--size={args.bytes}",
            "--ioengine=sync",
            "--direct=0",
            "--numjobs=1",
            "--output-format=json",
        ]
        completed = run_local(command, timeout=args.timeout)
        write_log(log, completed.stdout + completed.stderr)
        rows.append(fio_row_from_output("server", category, args.bytes, env, log, completed))
    Path(path).unlink(missing_ok=True)
    return rows


def run_disk_fio_remote(args: argparse.Namespace, env: dict[str, str], output_dir: Path) -> list[dict[str, str]]:
    path = f"/tmp/gridflux-phase4b-remote-fio-{os.getpid()}.bin"
    rows: list[dict[str, str]] = []
    for category, rw in (("disk_write", "write"), ("disk_read", "read")):
        log = output_dir / f"{compact_timestamp()}_remote-disk-fio-{rw}.log"
        command = (
            "fio --name=gridflux-phase4b "
            f"--filename={shlex.quote(path)} --rw={rw} --bs=1M --size={args.bytes} "
            "--ioengine=sync --direct=0 --numjobs=1 --output-format=json"
        )
        completed = run_remote(args.remote, command, timeout=args.timeout)
        write_log(log, completed.stdout + completed.stderr)
        rows.append(fio_row_from_output("client", category, args.bytes, env, log, completed))
    run_remote(args.remote, f"rm -f {shlex.quote(path)}", timeout=30)
    return rows


def run_memory_network(args: argparse.Namespace, env_server: dict[str, str], output_dir: Path) -> dict[str, str]:
    port = args.memory_sink_port
    server_log = output_dir / f"{compact_timestamp()}_memory-sink-server.log"
    client_log = output_dir / f"{compact_timestamp()}_memory-sink-client.log"
    server_bin = Path(args.local_build_dir) / "gridflux-server"
    client_bin = f"{args.remote_build_dir.rstrip('/')}/gridflux-client"
    server_cmd = [
        str(server_bin),
        "--host",
        args.server_host,
        "--port",
        str(port),
        "--connections",
        "4",
        "--bytes",
        str(args.bytes),
        "--buffer-size",
        "262144",
    ]
    with server_log.open("w", encoding="utf-8") as handle:
        server = subprocess.Popen(server_cmd, stdout=handle, stderr=subprocess.STDOUT, text=True, start_new_session=True)
    try:
        time.sleep(1.0)
        remote_cmd = (
            f"{shlex.quote(client_bin)} --host {shlex.quote(args.server_host)} --port {port} "
            f"--connections 4 --bytes {args.bytes} --buffer-size 262144"
        )
        client = run_remote(args.remote, remote_cmd, check=False, timeout=args.timeout)
        write_log(client_log, client.stdout + client.stderr)
        try:
            server.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            os.killpg(server.pid, signal.SIGKILL)
            server.wait(timeout=5)
        text = server_log.read_text(encoding="utf-8", errors="replace") + "\n" + client_log.read_text(
            encoding="utf-8", errors="replace"
        )
    finally:
        if server.poll() is None:
            os.killpg(server.pid, signal.SIGTERM)
            server.wait(timeout=5)
    row = base_row("link", "network", "gridflux_memory_sink", args.bytes, env_server, server_log)
    row["log"] = f"{server_log};{client_log}"
    match = None
    for line in text.splitlines():
        if line.startswith("client "):
            match = line
    if match is None:
        for line in text.splitlines():
            if line.startswith("server "):
                match = line
    if match is not None:
        values = dict(part.split("=", 1) for part in match.split() if "=" in part)
        row["elapsed_seconds"] = values.get("elapsed_seconds", "")
        row["throughput_gbps"] = values.get("throughput_gbps", "")
        row["result"] = "pass"
    else:
        row["error"] = text.replace("\n", " ")[:1000]
    return row


def run_iperf3(args: argparse.Namespace, env_server: dict[str, str], output_dir: Path) -> dict[str, str] | None:
    if not command_exists("iperf3") or not remote_command_exists(args.remote, "iperf3"):
        return None
    port = args.iperf_port
    log = output_dir / f"{compact_timestamp()}_iperf3.json"
    server = subprocess.Popen(
        ["iperf3", "-s", "-1", "-B", args.server_host, "-p", str(port), "-J"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    try:
        time.sleep(1.0)
        client = run_remote(args.remote, f"iperf3 -c {shlex.quote(args.server_host)} -p {port} -t 10 -J", timeout=60)
        server_text = server.communicate(timeout=20)[0]
    finally:
        if server.poll() is None:
            os.killpg(server.pid, signal.SIGTERM)
            server.wait(timeout=5)
    text = (client.stdout or "") + "\n" + (server_text or "")
    write_log(log, text)
    row = base_row("link", "network", "iperf3", 0, env_server, log)
    try:
        data = json.loads(client.stdout)
        bits_per_second = float(data["end"]["sum_received"]["bits_per_second"])
        seconds = float(data["end"]["sum_received"]["seconds"])
        row["bytes"] = str(int(bits_per_second * seconds / 8.0))
        row["elapsed_seconds"] = f"{seconds:.6f}"
        row["throughput_gbps"] = f"{bits_per_second / 1_000_000_000.0:.6f}"
        row["result"] = "pass"
    except (KeyError, ValueError, json.JSONDecodeError) as error:
        row["error"] = str(error)
    return row


def run_checksum_bench(side: str, command: str, env: dict[str, str], output_dir: Path, args: argparse.Namespace) -> dict[str, str]:
    log = output_dir / f"{compact_timestamp()}_{side}-checksum.log"
    if side == "server":
        completed = run_local(command.split(), timeout=args.timeout)
    else:
        completed = run_remote(args.remote, command, timeout=args.timeout)
    write_log(log, completed.stdout + completed.stderr)
    row = base_row(side, "checksum", "gridflux-checksum-bench", args.bytes, env, log)
    values = {}
    for part in completed.stdout.split():
        if "=" in part:
            key, value = part.split("=", 1)
            values[key] = value
    row["elapsed_seconds"] = values.get("elapsed_seconds", "")
    row["throughput_gbps"] = values.get("throughput_gbps", "")
    row["checksum_backend"] = values.get("backend", "")
    row["result"] = "pass" if completed.returncode == 0 and row["throughput_gbps"] else "fail"
    if row["result"] != "pass":
        row["error"] = (completed.stdout + completed.stderr).replace("\n", " ")[:1000]
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description="Collect GridFlux private host/link baselines.")
    parser.add_argument("--remote", default="root@<redacted>")
    parser.add_argument("--server-host", default="<redacted>")
    parser.add_argument("--local-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--remote-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--bytes", type=parse_size, default=parse_size("1GiB"))
    parser.add_argument("--output-dir", default="tools/perf/results")
    parser.add_argument("--memory-sink-port", type=int, default=21800)
    parser.add_argument("--iperf-port", type=int, default=21810)
    parser.add_argument("--timeout", type=int, default=1800)
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = output_dir / f"{compact_timestamp()}_host-baseline.csv"
    server_env = local_env("/tmp")
    client_env = remote_env(args.remote, "/tmp")

    rows: list[dict[str, str]] = []
    network = run_iperf3(args, server_env, output_dir)
    rows.append(network if network is not None else run_memory_network(args, server_env, output_dir))

    if command_exists("fio"):
        rows.extend(run_disk_fio_local(args, server_env, output_dir))
    else:
        rows.extend(run_disk_fallback_local(args, server_env, output_dir))
    if remote_command_exists(args.remote, "fio"):
        rows.extend(run_disk_fio_remote(args, client_env, output_dir))
    else:
        rows.extend(run_disk_fallback_remote(args, client_env, output_dir))

    checksum_bytes = min(args.bytes, 256 * 1024 * 1024)
    local_bench = f"{Path(args.local_build_dir) / 'gridflux-checksum-bench'} --backend auto --bytes {checksum_bytes} --iterations 3"
    remote_bench = f"{args.remote_build_dir.rstrip('/')}/gridflux-checksum-bench --backend auto --bytes {checksum_bytes} --iterations 3"
    rows.append(run_checksum_bench("server", local_bench, server_env, output_dir, args))
    rows.append(run_checksum_bench("client", remote_bench, client_env, output_dir, args))

    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    failures = [row for row in rows if row["result"] != "pass"]
    print(f"csv={csv_path}")
    print(f"rows={len(rows)} failures={len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
