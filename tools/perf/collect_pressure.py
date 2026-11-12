#!/usr/bin/env python3
"""Collect optional, run-scoped host pressure observations as JSONL.

The collector is deliberately read-only and best-effort. Missing /proc files or
counters are represented explicitly as unavailable instead of failing a transfer
run. It does not claim that the samples explain throughput by themselves.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def unavailable(reason: str) -> dict[str, Any]:
    return {"available": False, "reason": reason}


def read_cpu() -> dict[str, Any]:
    try:
        line = Path("/proc/stat").read_text(encoding="utf-8").splitlines()[0]
    except (FileNotFoundError, PermissionError, IndexError) as exc:
        return unavailable(f"proc_stat_unavailable:{type(exc).__name__}")
    fields = line.split()
    if len(fields) < 5 or fields[0] != "cpu":
        return unavailable("proc_stat_format")
    try:
        counters = [int(value) for value in fields[1:]]
    except ValueError:
        return unavailable("proc_stat_non_numeric")
    return {
        "available": True,
        "user_jiffies": counters[0],
        "system_jiffies": counters[2] if len(counters) > 2 else 0,
        "idle_jiffies": counters[3] if len(counters) > 3 else 0,
        "iowait_jiffies": counters[4] if len(counters) > 4 else 0,
        "total_jiffies": sum(counters),
    }


def read_disk() -> dict[str, Any]:
    try:
        lines = Path("/proc/diskstats").read_text(encoding="utf-8").splitlines()
    except (FileNotFoundError, PermissionError) as exc:
        return unavailable(f"diskstats_unavailable:{type(exc).__name__}")
    reads = writes = read_sectors = write_sectors = io_ms = 0
    devices = 0
    try:
        for line in lines:
            fields = line.split()
            if len(fields) < 14:
                continue
            device = fields[2]
            if device.startswith(("loop", "ram", "zram", "fd")):
                continue
            reads += int(fields[3])
            read_sectors += int(fields[5])
            writes += int(fields[7])
            write_sectors += int(fields[9])
            io_ms += int(fields[12])
            devices += 1
    except ValueError:
        return unavailable("diskstats_non_numeric")
    return {
        "available": True,
        "devices": devices,
        "reads_completed": reads,
        "writes_completed": writes,
        "sectors_read": read_sectors,
        "sectors_written": write_sectors,
        "io_ms": io_ms,
    }


def read_psi(path: str) -> dict[str, Any]:
    try:
        lines = Path(path).read_text(encoding="utf-8").splitlines()
    except (FileNotFoundError, PermissionError) as exc:
        return unavailable(f"psi_unavailable:{type(exc).__name__}")
    result: dict[str, Any] = {"available": True}
    try:
        for line in lines:
            fields = line.split()
            if not fields:
                continue
            group = fields[0]
            values: dict[str, float | int] = {}
            for item in fields[1:]:
                key, value = item.split("=", 1)
                values[key] = float(value) if "." in value else int(value)
            result[group] = values
    except (ValueError, IndexError):
        return unavailable("psi_format")
    return result


def read_pressures() -> dict[str, Any]:
    return {
        "cpu": read_psi("/proc/pressure/cpu"),
        "io": read_psi("/proc/pressure/io"),
        "memory": read_psi("/proc/pressure/memory"),
    }


def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed < 1:
        raise argparse.ArgumentTypeError("must be >= 1")
    return parsed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="JSONL output path")
    parser.add_argument("--run-id", default="", help="identifier to correlate with a transfer run")
    parser.add_argument("--duration-ms", type=positive_int, default=1000)
    parser.add_argument("--interval-ms", type=positive_int, default=100)
    args = parser.parse_args()
    if args.interval_ms > args.duration_ms:
        parser.error("--interval-ms cannot exceed --duration-ms")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    header = {
        "schema": "cpnetflux.pressure.v1",
        "record": "header",
        "run_id": args.run_id,
        "hostname": socket.gethostname(),
        "started_at": utc_now(),
        "duration_ms": args.duration_ms,
        "interval_ms": args.interval_ms,
        "pid": os.getpid(),
    }
    samples = max(1, (args.duration_ms + args.interval_ms - 1) // args.interval_ms)
    with args.output.open("w", encoding="utf-8") as stream:
        stream.write(json.dumps(header, sort_keys=True) + "\n")
        for index in range(samples):
            elapsed_ms = int((time.monotonic() - started) * 1000)
            record = {
                "schema": "cpnetflux.pressure.v1",
                "record": "sample",
                "run_id": args.run_id,
                "sample_index": index,
                "sampled_at": utc_now(),
                "elapsed_ms": elapsed_ms,
                "cpu": read_cpu(),
                "disk": read_disk(),
                "psi": read_pressures(),
            }
            stream.write(json.dumps(record, sort_keys=True) + "\n")
            stream.flush()
            if index + 1 < samples:
                time.sleep(args.interval_ms / 1000.0)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
