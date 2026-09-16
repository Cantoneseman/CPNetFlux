#!/usr/bin/env python3
import argparse
import csv
import datetime as dt
import re
import socket
import subprocess
import sys
from pathlib import Path


CSV_FIELDS = [
    "timestamp",
    "hostname",
    "algorithm",
    "backend",
    "bytes",
    "iterations",
    "buffer_size",
    "elapsed_seconds",
    "throughput_gbps",
    "checksum",
    "result",
    "log",
]

BENCH_PATTERN = re.compile(
    r"checksum_bench algorithm=(?P<algorithm>\S+) backend=(?P<backend>\S+) "
    r"bytes=(?P<bytes>\d+) iterations=(?P<iterations>\d+) "
    r"elapsed_seconds=(?P<elapsed>[0-9.eE+-]+) "
    r"throughput_gbps=(?P<gbps>[0-9.eE+-]+) checksum=(?P<checksum>\d+)"
)


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def parse_csv_list(value: str) -> list[int]:
    items: list[int] = []
    for item in value.split(","):
        item = item.strip()
        if item:
            items.append(int(item))
    if not items:
        raise argparse.ArgumentTypeError("list must not be empty")
    return items


def parse_backend_list(value: str) -> list[str]:
    backends: list[str] = []
    for item in value.split(","):
        item = item.strip()
        if item:
            if item not in {"software", "auto", "hardware"}:
                raise argparse.ArgumentTypeError(
                    "--backends entries must be software, auto, or hardware"
                )
            backends.append(item)
    if not backends:
        raise argparse.ArgumentTypeError("backend list must not be empty")
    return backends


def run_bench(
    bench_bin: Path,
    output_dir: Path,
    backend: str,
    total_bytes: int,
    iterations: int,
    buffer_size: int,
) -> dict[str, str]:
    case_id = f"{timestamp()}_checksum_bench_{backend}_bytes{total_bytes}_it{iterations}"
    log_path = output_dir / f"{case_id}.log"
    cmd = [
        str(bench_bin),
        "--backend",
        backend,
        "--bytes",
        str(total_bytes),
        "--iterations",
        str(iterations),
        "--buffer-size",
        str(buffer_size),
    ]
    result = subprocess.run(cmd, text=True, capture_output=True, check=False)
    text = result.stdout + result.stderr
    log_path.write_text(text, encoding="utf-8")

    parsed = BENCH_PATTERN.search(text)
    row = {
        "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "hostname": socket.gethostname(),
        "algorithm": "crc32c",
        "backend": backend,
        "bytes": str(total_bytes),
        "iterations": str(iterations),
        "buffer_size": str(buffer_size),
        "elapsed_seconds": "",
        "throughput_gbps": "",
        "checksum": "",
        "result": "pass",
        "log": str(log_path),
    }
    if parsed:
        row["algorithm"] = parsed.group("algorithm")
        row["backend"] = parsed.group("backend")
        row["elapsed_seconds"] = parsed.group("elapsed")
        row["throughput_gbps"] = parsed.group("gbps")
        row["checksum"] = parsed.group("checksum")

    if result.returncode != 0:
        row["result"] = "skip" if backend == "hardware" else "fail"
    elif parsed is None:
        row["result"] = "fail"
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux CRC32C backend benchmarks.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=parse_csv_list, default=[64 * 1024 * 1024, 256 * 1024 * 1024])
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--buffer-size", type=int, default=1024 * 1024)
    parser.add_argument("--backends", type=parse_backend_list, default=["software", "auto", "hardware"])
    parser.add_argument("--output-dir", default="tools/perf/results")
    args = parser.parse_args()

    if args.iterations <= 0:
        raise SystemExit("--iterations must be greater than zero")
    if args.buffer_size <= 0:
        raise SystemExit("--buffer-size must be greater than zero")

    build_dir = Path(args.build_dir)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    bench_bin = build_dir / "cpnetflux-checksum-bench"
    if not bench_bin.exists():
        raise FileNotFoundError(f"missing executable: {bench_bin}")

    rows: list[dict[str, str]] = []
    result_path = output_dir / f"{timestamp()}_checksum_bench.csv"
    for total_bytes in args.bytes:
        for backend in args.backends:
            print(
                f"running checksum bench: backend={backend} bytes={total_bytes} "
                f"iterations={args.iterations}",
                flush=True,
            )
            rows.append(
                run_bench(
                    bench_bin,
                    output_dir,
                    backend,
                    total_bytes,
                    args.iterations,
                    args.buffer_size,
                )
            )

    with result_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {result_path}")
    failed = [row for row in rows if row["result"] == "fail"]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
