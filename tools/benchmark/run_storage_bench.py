#!/usr/bin/env python3
"""Run native CPNetFlux storage benchmarks locally and/or remotely."""

from __future__ import annotations

import argparse
import csv
import os
import re
import shlex
import subprocess
import sys
import time
import statistics
from pathlib import Path


CSV_FIELDS = [
    "timestamp",
    "side",
    "operation",
    "bytes",
    "iterations",
    "buffer_size",
    "preallocate",
    "file_io_backend",
    "file_io_buffer_size",
    "file_io_queue_depth",
    "file_io_batch_size",
    "file_io_advice",
    "posix_write_strategy",
    "posix_write_strategy_effective",
    "iteration",
    "aggregate",
    "elapsed_seconds",
    "throughput_gbps",
    "read_call_count",
    "write_call_count",
    "write_syscall_count",
    "write_retry_count",
    "write_short_count",
    "write_zero_count",
    "write_total_bytes",
    "avg_read_bytes_per_call",
    "avg_write_bytes_per_call",
    "write_avg_bytes_per_syscall",
    "file_io_wait_seconds",
    "io_uring_submit_count",
    "io_uring_wait_count",
    "io_uring_completion_count",
    "io_uring_sqe_count",
    "io_uring_partial_completion_count",
    "io_uring_retry_count",
    "io_uring_avg_bytes_per_sqe",
    "hostname",
    "kernel",
    "fs_type",
    "free_bytes",
    "path",
    "log",
    "result",
    "error",
]

SUMMARY_FIELDS = [
    "side",
    "operation",
    "bytes",
    "buffer_size",
    "preallocate",
    "file_io_backend",
    "file_io_buffer_size",
    "file_io_queue_depth",
    "file_io_batch_size",
    "file_io_advice",
    "posix_write_strategy",
    "posix_write_strategy_effective",
    "case_count",
    "pass_count",
    "fail_count",
    "throughput_gbps_min",
    "throughput_gbps_median",
    "throughput_gbps_max",
    "elapsed_min",
    "elapsed_median",
    "elapsed_max",
    "io_uring_submit_count_min",
    "io_uring_submit_count_median",
    "io_uring_submit_count_max",
    "io_uring_wait_count_min",
    "io_uring_wait_count_median",
    "io_uring_wait_count_max",
    "io_uring_completion_count_min",
    "io_uring_completion_count_median",
    "io_uring_completion_count_max",
    "io_uring_sqe_count_min",
    "io_uring_sqe_count_median",
    "io_uring_sqe_count_max",
    "io_uring_partial_completion_count_min",
    "io_uring_partial_completion_count_median",
    "io_uring_partial_completion_count_max",
    "io_uring_retry_count_min",
    "io_uring_retry_count_median",
    "io_uring_retry_count_max",
    "io_uring_avg_bytes_per_sqe_min",
    "io_uring_avg_bytes_per_sqe_median",
    "io_uring_avg_bytes_per_sqe_max",
    "write_syscall_count_min",
    "write_syscall_count_median",
    "write_syscall_count_max",
    "write_retry_count_min",
    "write_retry_count_median",
    "write_retry_count_max",
    "write_short_count_min",
    "write_short_count_median",
    "write_short_count_max",
    "write_zero_count_min",
    "write_zero_count_median",
    "write_zero_count_max",
    "write_total_bytes_min",
    "write_total_bytes_median",
    "write_total_bytes_max",
    "write_avg_bytes_per_syscall_min",
    "write_avg_bytes_per_syscall_median",
    "write_avg_bytes_per_syscall_max",
]

IO_URING_SUMMARY_FIELDS = [
    "io_uring_submit_count",
    "io_uring_wait_count",
    "io_uring_completion_count",
    "io_uring_sqe_count",
    "io_uring_partial_completion_count",
    "io_uring_retry_count",
    "io_uring_avg_bytes_per_sqe",
    "write_syscall_count",
    "write_retry_count",
    "write_short_count",
    "write_zero_count",
    "write_total_bytes",
    "write_avg_bytes_per_syscall",
]

SUMMARY_GROUP_FIELD_COUNT = 12


def compact_timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def timestamp_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def ssh_prefix(remote: str) -> list[str]:
    if os.environ.get("CPNETFLUX_SSH_PASSWORD"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no", remote]
    return ["ssh", "-o", "StrictHostKeyChecking=no", remote]


def run_local(command: list[str], *, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, text=True, capture_output=True, check=False, timeout=timeout)


def run_remote(remote: str, command: str, *, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    if env.get("CPNETFLUX_SSH_PASSWORD") and not env.get("SSHPASS"):
        env["SSHPASS"] = env["CPNETFLUX_SSH_PASSWORD"]
    return subprocess.run(
        ssh_prefix(remote) + [command],
        text=True,
        capture_output=True,
        check=False,
        timeout=timeout,
        env=env,
    )


def parse_size_token(token: str) -> int:
    normalized = token.strip().lower()
    suffixes = [
        ("gib", 1024**3),
        ("gb", 1000**3),
        ("g", 1024**3),
        ("mib", 1024**2),
        ("mb", 1000**2),
        ("m", 1024**2),
        ("kib", 1024),
        ("kb", 1000),
        ("k", 1024),
    ]
    for suffix, multiplier in suffixes:
        if normalized.endswith(suffix):
            return int(normalized[: -len(suffix)]) * multiplier
    return int(normalized)


def parse_int_list(text: str) -> list[int]:
    return [parse_size_token(part) for part in text.split(",") if part.strip()]


def parse_choice_list(text: str, choices: set[str], name: str) -> list[str]:
    values = [part.strip().lower() for part in text.split(",") if part.strip()]
    invalid = [value for value in values if value not in choices]
    if invalid:
        raise argparse.ArgumentTypeError(f"invalid {name}: {','.join(invalid)}")
    return values


def is_valid_write_strategy_case(strategy: str, file_io_buffer_size: int) -> bool:
    return not (strategy == "coalesced" and file_io_buffer_size == 0)


def command_output(command: list[str]) -> str:
    completed = run_local(command)
    return completed.stdout.strip() if completed.returncode == 0 else ""


def remote_output(remote: str, command: str) -> str:
    completed = run_remote(remote, command, timeout=30)
    return completed.stdout.strip() if completed.returncode == 0 else ""


def fs_snapshot(path: str) -> tuple[str, str]:
    completed = run_local(["df", "-PT", path])
    lines = completed.stdout.strip().splitlines()
    if len(lines) < 2:
        return "", ""
    parts = lines[1].split()
    return parts[1] if len(parts) > 1 else "", str(int(parts[4]) * 1024) if len(parts) > 4 else ""


def remote_fs_snapshot(remote: str, path: str) -> tuple[str, str]:
    output = remote_output(remote, f"df -PT {shlex.quote(path)} | tail -n 1")
    parts = output.split()
    return parts[1] if len(parts) > 1 else "", str(int(parts[4]) * 1024) if len(parts) > 4 else ""


def parse_metric_lines(text: str) -> list[dict[str, str]]:
    result: list[dict[str, str]] = []
    for line in text.splitlines():
        if line.startswith("storage_bench "):
            result.append(dict(re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^ \n]+)", line)))
    return result


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def base_row(side: str, path: str, log: Path, fs_type: str, free_bytes: str, remote: str | None) -> dict[str, str]:
    hostname = remote_output(remote, "hostname") if remote else command_output(["hostname"])
    kernel = remote_output(remote, "uname -r") if remote else command_output(["uname", "-r"])
    return {
        "timestamp": timestamp_utc(),
        "side": side,
        "operation": "",
        "bytes": "",
        "iterations": "",
        "buffer_size": "",
        "preallocate": "",
        "file_io_backend": "",
        "file_io_buffer_size": "",
        "file_io_queue_depth": "",
        "file_io_batch_size": "",
        "file_io_advice": "",
        "posix_write_strategy": "",
        "posix_write_strategy_effective": "",
        "iteration": "",
        "aggregate": "",
        "elapsed_seconds": "",
        "throughput_gbps": "",
        "read_call_count": "",
        "write_call_count": "",
        "write_syscall_count": "",
        "write_retry_count": "",
        "write_short_count": "",
        "write_zero_count": "",
        "write_total_bytes": "",
        "avg_read_bytes_per_call": "",
        "avg_write_bytes_per_call": "",
        "write_avg_bytes_per_syscall": "",
        "file_io_wait_seconds": "",
        "io_uring_submit_count": "",
        "io_uring_wait_count": "",
        "io_uring_completion_count": "",
        "io_uring_sqe_count": "",
        "io_uring_partial_completion_count": "",
        "io_uring_retry_count": "",
        "io_uring_avg_bytes_per_sqe": "",
        "hostname": hostname,
        "kernel": kernel,
        "fs_type": fs_type,
        "free_bytes": free_bytes,
        "path": path,
        "log": str(log),
        "result": "fail",
        "error": "",
    }


def run_case(
    args: argparse.Namespace,
    *,
    side: str,
    remote: str | None,
    bench_bin: str,
    path: str,
    mode: str,
    bytes_count: int,
    buffer_size: int,
    preallocate: str,
    file_io_backend: str,
    file_io_buffer_size: int,
    file_io_queue_depth: int,
    file_io_batch_size: int,
    file_io_advice: str,
    posix_write_strategy: str,
    fs_type: str,
    free_bytes: str,
    output_dir: Path,
) -> list[dict[str, str]]:
    log = output_dir / (
        f"{compact_timestamp()}_storage_{side}_{mode}_bytes{bytes_count}_buf{buffer_size}_"
        f"fiobuf{file_io_buffer_size}_{preallocate}_{file_io_backend}_qd{file_io_queue_depth}_"
        f"bs{file_io_batch_size}_{file_io_advice}_pws{posix_write_strategy}.log"
    )
    command = [
        bench_bin,
        "--path",
        path,
        "--mode",
        mode,
        "--bytes",
        str(bytes_count),
        "--buffer-size",
        str(buffer_size),
        "--iterations",
        str(args.iterations),
        "--preallocate",
        preallocate,
        "--file-io-backend",
        file_io_backend,
        "--file-io-buffer-size",
        str(file_io_buffer_size),
        "--file-io-queue-depth",
        str(file_io_queue_depth),
        "--file-io-batch-size",
        str(file_io_batch_size),
        "--file-io-advice",
        file_io_advice,
        "--posix-write-strategy",
        posix_write_strategy,
    ]
    command.append("--keep-file")

    if remote:
        completed = run_remote(remote, " ".join(shlex.quote(part) for part in command), timeout=args.timeout)
    else:
        completed = run_local(command, timeout=args.timeout)
    text = completed.stdout + completed.stderr
    write_text(log, text)

    metric_lines = parse_metric_lines(text)
    rows: list[dict[str, str]] = []
    for metrics in metric_lines:
        row = base_row(side, path, log, fs_type, free_bytes, remote)
        for key in (
            "operation",
            "bytes",
            "iterations",
            "buffer_size",
            "preallocate",
            "file_io_backend",
            "file_io_buffer_size",
            "file_io_queue_depth",
            "file_io_batch_size",
            "file_io_advice",
            "posix_write_strategy",
            "posix_write_strategy_effective",
            "iteration",
            "aggregate",
            "elapsed_seconds",
            "throughput_gbps",
            "read_call_count",
            "write_call_count",
            "write_syscall_count",
            "write_retry_count",
            "write_short_count",
            "write_zero_count",
            "write_total_bytes",
            "avg_read_bytes_per_call",
            "avg_write_bytes_per_call",
            "write_avg_bytes_per_syscall",
            "file_io_wait_seconds",
            "io_uring_submit_count",
            "io_uring_wait_count",
            "io_uring_completion_count",
            "io_uring_sqe_count",
            "io_uring_partial_completion_count",
            "io_uring_retry_count",
            "io_uring_avg_bytes_per_sqe",
            "result",
        ):
            row[key] = metrics.get(key, row[key])
        if completed.returncode != 0 or row["result"] != "pass":
            row["result"] = "fail"
            row["error"] = metrics.get("error", text.replace("\n", " ")[:1000])
        rows.append(row)
    if not rows:
        row = base_row(side, path, log, fs_type, free_bytes, remote)
        row["result"] = "fail"
        row["error"] = text.replace("\n", " ")[:1000]
        rows.append(row)
    return rows


def float_values(rows: list[dict[str, str]], field: str) -> list[float]:
    values: list[float] = []
    for row in rows:
        try:
            values.append(float(row[field]))
        except (KeyError, TypeError, ValueError):
            continue
    return values


def summarize_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = {}
    for row in rows:
        if row.get("aggregate") == "true":
            continue
        key = (
            row["side"],
            row["operation"],
            row["bytes"],
            row["buffer_size"],
            row["preallocate"],
            row["file_io_backend"],
            row["file_io_buffer_size"],
            row["file_io_queue_depth"],
            row["file_io_batch_size"],
            row["file_io_advice"],
            row["posix_write_strategy"],
            row["posix_write_strategy_effective"],
        )
        groups.setdefault(key, []).append(row)

    summaries: list[dict[str, str]] = []
    for key, grouped_rows in sorted(groups.items()):
        throughput = float_values(grouped_rows, "throughput_gbps")
        elapsed = float_values(grouped_rows, "elapsed_seconds")
        pass_count = sum(1 for row in grouped_rows if row["result"] == "pass")
        summary = dict(zip(SUMMARY_FIELDS[:SUMMARY_GROUP_FIELD_COUNT], key, strict=True))
        summary.update(
            {
                "case_count": str(len(grouped_rows)),
                "pass_count": str(pass_count),
                "fail_count": str(len(grouped_rows) - pass_count),
                "throughput_gbps_min": f"{min(throughput):.6f}" if throughput else "",
                "throughput_gbps_median": f"{statistics.median(throughput):.6f}"
                if throughput
                else "",
                "throughput_gbps_max": f"{max(throughput):.6f}" if throughput else "",
                "elapsed_min": f"{min(elapsed):.6f}" if elapsed else "",
                "elapsed_median": f"{statistics.median(elapsed):.6f}" if elapsed else "",
                "elapsed_max": f"{max(elapsed):.6f}" if elapsed else "",
            }
        )
        for field in IO_URING_SUMMARY_FIELDS:
            values = float_values(grouped_rows, field)
            summary[f"{field}_min"] = f"{min(values):.6f}" if values else ""
            summary[f"{field}_median"] = f"{statistics.median(values):.6f}" if values else ""
            summary[f"{field}_max"] = f"{max(values):.6f}" if values else ""
        summaries.append(summary)
    return summaries


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux native storage benchmark.")
    parser.add_argument("--side", choices=["local", "remote", "both"], default="local")
    parser.add_argument("--remote", default="root@<redacted>")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--remote-build-dir", default="/root/projects/CPNetFlux/build")
    parser.add_argument("--output-dir", default="tools/perf/results")
    parser.add_argument("--path", default="")
    parser.add_argument("--remote-path", default="")
    parser.add_argument("--bytes", default="1073741824")
    parser.add_argument("--modes", default="write,read")
    parser.add_argument("--buffer-sizes", default="1048576")
    parser.add_argument("--preallocates", default="off,full")
    parser.add_argument("--file-io-backends", default="posix")
    parser.add_argument("--file-io-buffer-sizes", default="0")
    parser.add_argument("--file-io-queue-depths", default="1")
    parser.add_argument("--file-io-batch-sizes", default="")
    parser.add_argument("--file-io-advices", default="off")
    parser.add_argument("--posix-write-strategies", default="auto")
    parser.add_argument("--iterations", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=1800)
    parser.add_argument("--keep-file", action="store_true")
    args = parser.parse_args()

    if args.iterations <= 0:
        raise SystemExit("--iterations must be greater than zero")

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    csv_path = output_dir / f"{compact_timestamp()}_storage-bench.csv"
    byte_values = parse_int_list(args.bytes)
    buffer_sizes = parse_int_list(args.buffer_sizes)
    modes = parse_choice_list(args.modes, {"write", "read", "rewrite", "all"}, "mode")
    preallocates = parse_choice_list(args.preallocates, {"off", "full"}, "preallocate")
    file_io_backends = parse_choice_list(
        args.file_io_backends, {"posix", "io_uring"}, "file IO backend"
    )
    file_io_buffer_sizes = parse_int_list(args.file_io_buffer_sizes)
    if any(value < 0 or value > 64 * 1024 * 1024 for value in file_io_buffer_sizes):
        raise SystemExit("--file-io-buffer-sizes values must be in range 0..67108864")
    file_io_queue_depths = parse_int_list(args.file_io_queue_depths)
    if any(value <= 0 or value > 256 for value in file_io_queue_depths):
        raise SystemExit("--file-io-queue-depths values must be in range 1..256")
    file_io_batch_sizes = parse_int_list(args.file_io_batch_sizes) if args.file_io_batch_sizes else None
    if file_io_batch_sizes is not None and any(value <= 0 or value > 256 for value in file_io_batch_sizes):
        raise SystemExit("--file-io-batch-sizes values must be in range 1..256")
    file_io_advices = parse_choice_list(
        args.file_io_advices,
        {"off", "sequential", "noreuse", "dontneed", "sequential_dontneed"},
        "file IO advice",
    )
    posix_write_strategies = parse_choice_list(
        args.posix_write_strategies,
        {"auto", "direct", "coalesced"},
        "posix write strategy",
    )
    summary_path = output_dir / f"{compact_timestamp()}_storage-bench-summary.csv"

    rows: list[dict[str, str]] = []
    local_path = args.path or f"/tmp/cpnetflux-storage-bench-local-{os.getpid()}.bin"
    remote_path = args.remote_path or f"/tmp/cpnetflux-storage-bench-remote-{os.getpid()}.bin"
    run_local_side = args.side in ("local", "both")
    run_remote_side = args.side in ("remote", "both")
    local_fs = ""
    local_free = ""
    remote_fs = ""
    remote_free = ""
    if run_local_side:
        local_fs, local_free = fs_snapshot(str(Path(local_path).parent))
    if run_remote_side:
        remote_fs, remote_free = remote_fs_snapshot(args.remote, str(Path(remote_path).parent))

    sides: list[tuple[str, str | None, str, str, str, str]] = []
    if run_local_side:
        sides.append(("local", None, str(Path(args.build_dir) / "cpnetflux-storage-bench"), local_path, local_fs, local_free))
    if run_remote_side:
        sides.append(("remote", args.remote, f"{args.remote_build_dir.rstrip('/')}/cpnetflux-storage-bench", remote_path, remote_fs, remote_free))

    for side, remote, bench_bin, path, fs_type, free_bytes in sides:
        for bytes_count in byte_values:
            for buffer_size in buffer_sizes:
                for preallocate in preallocates:
                    for file_io_backend in file_io_backends:
                        for file_io_buffer_size in file_io_buffer_sizes:
                            for posix_write_strategy in posix_write_strategies:
                                if not is_valid_write_strategy_case(
                                    posix_write_strategy, file_io_buffer_size
                                ):
                                    continue
                                for file_io_queue_depth in file_io_queue_depths:
                                    batch_sizes = (
                                        file_io_batch_sizes
                                        if file_io_batch_sizes is not None
                                        else [file_io_queue_depth]
                                    )
                                    for file_io_batch_size in batch_sizes:
                                        for file_io_advice in file_io_advices:
                                            for mode in modes:
                                                case_rows = run_case(
                                                    args,
                                                    side=side,
                                                    remote=remote,
                                                    bench_bin=bench_bin,
                                                    path=path,
                                                    mode=mode,
                                                    bytes_count=bytes_count,
                                                    buffer_size=buffer_size,
                                                    preallocate=preallocate,
                                                    file_io_backend=file_io_backend,
                                                    file_io_buffer_size=file_io_buffer_size,
                                                    file_io_queue_depth=file_io_queue_depth,
                                                    file_io_batch_size=file_io_batch_size,
                                                    file_io_advice=file_io_advice,
                                                    posix_write_strategy=posix_write_strategy,
                                                    fs_type=fs_type,
                                                    free_bytes=free_bytes,
                                                    output_dir=output_dir,
                                                )
                                                rows.extend(case_rows)
                                            with csv_path.open("w", newline="", encoding="utf-8") as handle:
                                                writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
                                                writer.writeheader()
                                                writer.writerows(rows)
                                            with summary_path.open("w", newline="", encoding="utf-8") as handle:
                                                writer = csv.DictWriter(handle, fieldnames=SUMMARY_FIELDS)
                                                writer.writeheader()
                                                writer.writerows(summarize_rows(rows))
                                            aggregate = next((row for row in case_rows if row.get("aggregate") == "true"), case_rows[-1])
                                            print(
                                                f"{side} {mode} bytes={bytes_count} buffer={buffer_size} "
                                                f"preallocate={preallocate} backend={file_io_backend} "
                                                f"file_io_buffer={file_io_buffer_size} "
                                                f"strategy={posix_write_strategy} "
                                                f"queue_depth={file_io_queue_depth} batch_size={file_io_batch_size} "
                                                f"advice={file_io_advice} result={aggregate['result']} "
                                                f"throughput_gbps={aggregate['throughput_gbps']}",
                                                flush=True,
                                            )

    if not args.keep_file:
        if run_local_side:
            Path(local_path).unlink(missing_ok=True)
        if run_remote_side:
            run_remote(args.remote, f"rm -f {shlex.quote(remote_path)}", timeout=30)

    failures = [row for row in rows if row["result"] != "pass"]
    print(f"csv={csv_path}")
    print(f"summary_csv={summary_path}")
    print(f"cases={len(rows)} failures={len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
