#!/usr/bin/env python3
import argparse
import csv
import datetime as dt
import hashlib
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path


CSV_FIELDS = [
    "timestamp",
    "hostname",
    "mode",
    "host",
    "port",
    "connections",
    "chunk_size",
    "buffer_size",
    "bytes",
    "checksum_enabled",
    "checksum_algorithm",
    "checksum_backend",
    "skipped_bytes",
    "resent_bytes",
    "verified_bytes",
    "manifest_flush_policy",
    "manifest_flush_count",
    "elapsed",
    "throughput_gbps",
    "client_elapsed_seconds",
    "client_throughput_gbps",
    "server_elapsed_seconds",
    "server_throughput_gbps",
    "source_sha256",
    "dest_sha256",
    "result",
    "server_log",
    "client_log",
]

METRIC_PATTERN = re.compile(
    r"(?P<role>file_client|file_server) "
    r"(?P<byte_key>sent_bytes|received_bytes)=(?P<bytes>\d+) "
    r"elapsed_seconds=(?P<elapsed>[0-9.]+) "
    r"throughput_gbps=(?P<gbps>[0-9.]+)"
)


def parse_csv_list(value: str) -> list[int]:
    items: list[int] = []
    for item in value.split(","):
        item = item.strip()
        if item:
            items.append(int(item))
    if not items:
        raise argparse.ArgumentTypeError("list must not be empty")
    return items


def timestamp() -> str:
    return dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def make_file(path: Path, total_bytes: int) -> None:
    block = bytes(index % 251 for index in range(1024 * 1024))
    remaining = total_bytes
    with path.open("wb") as handle:
        while remaining > 0:
            size = min(remaining, len(block))
            handle.write(block[:size])
            remaining -= size


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_metrics(text: str, role: str) -> dict[str, str]:
    for line in text.splitlines():
        match = METRIC_PATTERN.search(line.strip())
        if match and match.group("role") == role:
            return {"elapsed": match.group("elapsed"), "gbps": match.group("gbps")}
    return {"elapsed": "", "gbps": ""}


def parse_key_metric(text: str, role: str, key: str) -> str:
    prefix = f"{role} "
    pattern = re.compile(rf"(?:^|\s){re.escape(key)}=([^\s]+)")
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped.startswith(prefix):
            continue
        match = pattern.search(stripped)
        if match:
            return match.group(1)
    return ""


def run_case(
    build_dir: Path,
    output_dir: Path,
    mode: str,
    host: str,
    port: int,
    connections: int,
    chunk_size: int,
    buffer_size: int,
    total_bytes: int,
    checksum: str,
    checksum_backend: str,
) -> dict[str, str]:
    server_bin = build_dir / "gridflux-file-server"
    client_bin = build_dir / "gridflux-file-client"
    if not server_bin.exists() or not client_bin.exists():
        raise FileNotFoundError(f"missing gridflux-file-server/client in {build_dir}")

    case_id = (
        f"{timestamp()}_{mode}_c{connections}_chunk{chunk_size}_"
        f"buf{buffer_size}_bytes{total_bytes}_{checksum}_{checksum_backend}_p{port}"
    )
    server_log = output_dir / f"{case_id}_server.log"
    client_log = output_dir / f"{case_id}_client.log"

    with tempfile.TemporaryDirectory(prefix="gridflux-file-loopback.") as temp_dir_text:
        temp_dir = Path(temp_dir_text)
        source = temp_dir / "source.bin"
        dest = temp_dir / "dest.bin"
        make_file(source, total_bytes)
        source_sha = sha256_file(source)

        server_cmd = [
            str(server_bin),
            "--host",
            host,
            "--port",
            str(port),
            "--output",
            str(dest),
            "--connections",
            str(connections),
            "--buffer-size",
            str(buffer_size),
            "--checksum",
            checksum,
            "--checksum-backend",
            checksum_backend,
        ]
        client_cmd = [
            str(client_bin),
            "--host",
            host,
            "--port",
            str(port),
            "--input",
            str(source),
            "--connections",
            str(connections),
            "--chunk-size",
            str(chunk_size),
            "--buffer-size",
            str(buffer_size),
            "--checksum",
            checksum,
            "--checksum-backend",
            checksum_backend,
        ]

        with server_log.open("w", encoding="utf-8") as server_file:
            server = subprocess.Popen(server_cmd, stdout=server_file, stderr=subprocess.STDOUT)

        time.sleep(0.3)
        client_result = subprocess.run(client_cmd, text=True, capture_output=True, check=False)
        client_log.write_text(client_result.stdout + client_result.stderr, encoding="utf-8")

        try:
            server_return = server.wait(timeout=60)
        except subprocess.TimeoutExpired:
            server.kill()
            server_return = server.wait()

        dest_sha = sha256_file(dest) if dest.exists() else ""

    server_text = server_log.read_text(encoding="utf-8")
    client_text = client_log.read_text(encoding="utf-8")
    server_metrics = parse_metrics(server_text, "file_server")
    client_metrics = parse_metrics(client_text, "file_client")
    actual_backend = (
        parse_key_metric(client_text, "file_client", "checksum_backend")
        or parse_key_metric(server_text, "file_server", "checksum_backend")
        or ("none" if checksum == "none" else checksum_backend)
    )
    skipped_bytes = parse_key_metric(client_text, "file_client", "skipped_bytes")
    resent_bytes = parse_key_metric(client_text, "file_client", "resent_bytes")
    verified_bytes = (
        parse_key_metric(client_text, "file_client", "verified_bytes")
        or parse_key_metric(server_text, "file_server", "verified_bytes")
    )
    manifest_flush_policy = parse_key_metric(server_text, "file_server", "manifest_flush_policy")
    manifest_flush_count = parse_key_metric(server_text, "file_server", "manifest_flush_count")
    result = (
        "pass"
        if client_result.returncode == 0
        and server_return == 0
        and source_sha == dest_sha
        else "fail"
    )
    elapsed = server_metrics["elapsed"] or client_metrics["elapsed"]
    gbps = server_metrics["gbps"] or client_metrics["gbps"]

    return {
        "timestamp": dt.datetime.now(dt.timezone.utc).isoformat(timespec="seconds"),
        "hostname": socket.gethostname(),
        "mode": mode,
        "host": host,
        "port": str(port),
        "connections": str(connections),
        "chunk_size": str(chunk_size),
        "buffer_size": str(buffer_size),
        "bytes": str(total_bytes),
        "checksum_enabled": "false" if checksum == "none" else "true",
        "checksum_algorithm": checksum,
        "checksum_backend": actual_backend,
        "skipped_bytes": skipped_bytes,
        "resent_bytes": resent_bytes,
        "verified_bytes": verified_bytes,
        "manifest_flush_policy": manifest_flush_policy,
        "manifest_flush_count": manifest_flush_count,
        "elapsed": elapsed,
        "throughput_gbps": gbps,
        "client_elapsed_seconds": client_metrics["elapsed"],
        "client_throughput_gbps": client_metrics["gbps"],
        "server_elapsed_seconds": server_metrics["elapsed"],
        "server_throughput_gbps": server_metrics["gbps"],
        "source_sha256": source_sha,
        "dest_sha256": dest_sha,
        "result": result,
        "server_log": str(server_log),
        "client_log": str(client_log),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run GridFlux file loopback matrix.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--bytes", type=int)
    parser.add_argument("--smoke", action="store_true")
    parser.add_argument("--connections", type=parse_csv_list)
    parser.add_argument("--chunk-sizes", type=parse_csv_list)
    parser.add_argument("--buffer-sizes", type=parse_csv_list)
    parser.add_argument("--output-dir", default="tools/perf/results")
    parser.add_argument("--port-base", type=int, default=19500)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--checksum", choices=["crc32c", "none"], default="crc32c")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    args = parser.parse_args()

    build_dir = Path(args.build_dir)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    if args.smoke:
        total_bytes = args.bytes if args.bytes is not None else 64 * 1024 * 1024
        mode = "file-loopback-smoke"
        connections = args.connections or [1, 4, 8]
        chunk_sizes = args.chunk_sizes or [1024 * 1024, 4 * 1024 * 1024]
        buffer_sizes = args.buffer_sizes or [65536]
    else:
        total_bytes = args.bytes if args.bytes is not None else 1024 * 1024 * 1024
        mode = "file-loopback-full"
        connections = args.connections or [1, 4, 8, 16, 32]
        chunk_sizes = args.chunk_sizes or [1024 * 1024, 4 * 1024 * 1024]
        buffer_sizes = args.buffer_sizes or [65536, 262144, 1048576]

    matrix = [
        (connection, chunk_size, buffer_size)
        for connection in connections
        for chunk_size in chunk_sizes
        for buffer_size in buffer_sizes
    ]

    result_path = output_dir / f"{timestamp()}_{mode}.csv"
    rows = []
    for index, (connection, chunk_size, buffer_size) in enumerate(matrix):
        port = args.port_base + index
        print(
            f"running {mode}: host={args.host} port={port} connections={connection} "
            f"chunk_size={chunk_size} buffer_size={buffer_size} bytes={total_bytes} "
            f"checksum={args.checksum} checksum_backend={args.checksum_backend}",
            flush=True,
        )
        rows.append(
            run_case(
                build_dir,
                output_dir,
                mode,
                args.host,
                port,
                connection,
                chunk_size,
                buffer_size,
                total_bytes,
                args.checksum,
                args.checksum_backend,
            )
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
