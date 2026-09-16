#!/usr/bin/env python3
import argparse
import csv
import datetime as dt
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path


CSV_FIELDS = [
    "timestamp",
    "hostname",
    "mode",
    "host",
    "port",
    "connections",
    "buffer_size",
    "bytes",
    "client_elapsed_seconds",
    "client_throughput_gbps",
    "server_elapsed_seconds",
    "server_throughput_gbps",
    "client_bytes",
    "server_bytes",
    "result",
]

METRIC_PATTERN = re.compile(
    r"(?P<role>client|server) "
    r"(?P<byte_key>sent_bytes|received_bytes)=(?P<bytes>\d+) "
    r"elapsed_seconds=(?P<elapsed>[0-9.]+) "
    r"throughput_gbps=(?P<gbps>[0-9.]+)"
)


def parse_csv_list(value: str) -> list[int]:
    items: list[int] = []
    for item in value.split(","):
        item = item.strip()
        if not item:
            continue
        items.append(int(item))
    if not items:
        raise argparse.ArgumentTypeError("list must not be empty")
    return items


def parse_metrics(text: str, role: str) -> dict[str, str]:
    for line in text.splitlines():
        match = METRIC_PATTERN.search(line.strip())
        if match and match.group("role") == role:
            return {
                "bytes": match.group("bytes"),
                "elapsed": match.group("elapsed"),
                "gbps": match.group("gbps"),
            }
    return {"bytes": "", "elapsed": "", "gbps": ""}


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def run_case(
    build_dir: Path,
    output_dir: Path,
    mode: str,
    host: str,
    port: int,
    connections: int,
    buffer_size: int,
    total_bytes: int,
) -> dict[str, str]:
    server_bin = build_dir / "cpnetflux-server"
    client_bin = build_dir / "cpnetflux-client"
    if not server_bin.exists() or not client_bin.exists():
        raise FileNotFoundError(f"missing cpnetflux-server/client in {build_dir}")

    case_id = f"{timestamp()}_{mode}_c{connections}_b{buffer_size}_bytes{total_bytes}_p{port}"
    server_log = output_dir / f"{case_id}_server.log"
    client_log = output_dir / f"{case_id}_client.log"

    server_cmd = [
        str(server_bin),
        "--host",
        host,
        "--port",
        str(port),
        "--connections",
        str(connections),
        "--bytes",
        str(total_bytes),
        "--buffer-size",
        str(buffer_size),
    ]
    client_cmd = [
        str(client_bin),
        "--host",
        host,
        "--port",
        str(port),
        "--connections",
        str(connections),
        "--bytes",
        str(total_bytes),
        "--buffer-size",
        str(buffer_size),
    ]

    with server_log.open("w", encoding="utf-8") as server_file:
        server = subprocess.Popen(server_cmd, stdout=server_file, stderr=subprocess.STDOUT)

    time.sleep(0.3)
    client_result = subprocess.run(client_cmd, text=True, capture_output=True, check=False)
    client_log.write_text(client_result.stdout + client_result.stderr, encoding="utf-8")

    try:
        server_return = server.wait(timeout=30)
    except subprocess.TimeoutExpired:
        server.kill()
        server_return = server.wait()

    server_text = server_log.read_text(encoding="utf-8")
    client_text = client_log.read_text(encoding="utf-8")
    server_metrics = parse_metrics(server_text, "server")
    client_metrics = parse_metrics(client_text, "client")

    result = "pass" if client_result.returncode == 0 and server_return == 0 else "fail"
    return {
        "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "hostname": socket.gethostname(),
        "mode": mode,
        "host": host,
        "port": str(port),
        "connections": str(connections),
        "buffer_size": str(buffer_size),
        "bytes": str(total_bytes),
        "client_elapsed_seconds": client_metrics["elapsed"],
        "client_throughput_gbps": client_metrics["gbps"],
        "server_elapsed_seconds": server_metrics["elapsed"],
        "server_throughput_gbps": server_metrics["gbps"],
        "client_bytes": client_metrics["bytes"],
        "server_bytes": server_metrics["bytes"],
        "result": result,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux loopback performance matrix.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=int)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--connections", type=parse_csv_list)
    parser.add_argument("--buffer-sizes", type=parse_csv_list)
    parser.add_argument("--output-dir", default="tools/perf/results")
    parser.add_argument("--port-base", type=int, default=19000)
    parser.add_argument("--host", default="127.0.0.1")
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.smoke:
        matrix = [(1, 65536), (4, 262144)]
        total_bytes = args.bytes if args.bytes is not None else 8 * 1024 * 1024
        mode = "loopback-smoke"
    else:
        connections = args.connections or [1, 4, 8, 16, 32]
        buffer_sizes = args.buffer_sizes or [65536, 262144, 1048576, 4194304]
        matrix = [(conn, buf) for conn in connections for buf in buffer_sizes]
        total_bytes = args.bytes if args.bytes is not None else 1024 * 1024 * 1024
        mode = "loopback-full"

    result_path = output_dir / f"{timestamp()}_{mode}.csv"
    rows = []
    for index, (connections, buffer_size) in enumerate(matrix):
        port = args.port_base + index
        print(
            f"running {mode}: host={args.host} port={port} "
            f"connections={connections} buffer_size={buffer_size} bytes={total_bytes}",
            flush=True,
        )
        rows.append(
            run_case(build_dir, output_dir, mode, args.host, port, connections, buffer_size, total_bytes)
        )

    with result_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {result_path}")
    failed = [row for row in rows if row["result"] != "pass"]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
