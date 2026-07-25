#!/usr/bin/env python3
"""Run private GridFTP-like tree transfer matrices.

The script runs on machine one. It starts gridflux-gridftp-server locally and
uses SSH to run gridflux-tree-upload-client/download-client on machine two.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import shlex
import signal
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path


CSV_FIELDS = [
    "timestamp",
    "dataset",
    "direction",
    "resume",
    "repeat_index",
    "file_count",
    "total_bytes",
    "file_parallelism",
    "connections",
    "checksum_algorithm",
    "checksum_backend",
    "elapsed_seconds",
    "throughput_gbps",
    "json_summary",
    "completed_files",
    "skipped_files",
    "failed_files",
    "changed_files",
    "bytes_total",
    "bytes_transferred",
    "summary_tree_hash",
    "error_message",
    "source_tree_hash",
    "dest_tree_hash",
    "result",
    "server_log",
    "client_log",
    "control_port",
    "data_port_base",
    "local_root",
    "remote_source",
    "remote_dest",
    "error",
]

SUMMARY_GROUP_FIELDS = [
    "dataset",
    "direction",
    "resume",
    "file_parallelism",
    "connections",
    "checksum_algorithm",
    "checksum_backend",
]

SUMMARY_FIELDS = [
    *SUMMARY_GROUP_FIELDS,
    "repeat_count",
    "pass_count",
    "fail_count",
    "tree_hash_mismatch_count",
    "throughput_gbps_min",
    "throughput_gbps_median",
    "throughput_gbps_max",
    "elapsed_seconds_min",
    "elapsed_seconds_median",
    "elapsed_seconds_max",
    "file_count",
    "total_bytes",
    "completed_files",
    "skipped_files",
    "failed_files",
    "changed_files",
    "bytes_transferred",
]


@dataclass(frozen=True)
class Case:
    index: int
    dataset: str
    direction: str
    resume: bool
    repeat_index: int
    file_parallelism: int
    connections: int
    checksum: str


def compact_timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def timestamp_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def ssh_prefix(remote: str) -> list[str]:
    if os.environ.get("GRIDFLUX_SSH_PASSWORD") or os.environ.get("SSHPASS"):
        return ["sshpass", "-e", "ssh", "-o", "StrictHostKeyChecking=no", remote]
    return ["ssh", "-o", "StrictHostKeyChecking=no", remote]


def run_remote(remote: str, command: str, *, check: bool = True, timeout: int | None = None) -> subprocess.CompletedProcess[str]:
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
        raise RuntimeError("$ " + command + "\n" + completed.stdout + completed.stderr)
    return completed


def parse_csv_list(text: str) -> list[str]:
    return [part.strip() for part in text.split(",") if part.strip()]


def parse_int_list(text: str) -> list[int]:
    return [int(part) for part in parse_csv_list(text)]


def make_bytes(size: int, seed: int) -> bytes:
    return bytes(((index * 31 + seed) % 251 for index in range(size)))


def dataset_specs(dataset: str) -> list[tuple[str, int, int]]:
    if dataset == "small":
        return [(f"small/file-{index:03d}.bin", 4096, index + 1) for index in range(128)]
    if dataset == "mixed":
        specs: list[tuple[str, int, int]] = [("empty.bin", 0, 1)]
        specs.extend((f"tiny/tiny-{index:03d}.bin", 1024 + index, index + 10) for index in range(32))
        specs.extend((f"medium/medium-{index:02d}.bin", 1024 * 1024 + index * 17, index + 80) for index in range(12))
        specs.extend((f"large/large-{index:02d}.bin", 16 * 1024 * 1024 + index * 4096, index + 120) for index in range(4))
        return specs
    if dataset == "large":
        return [(f"large-{index:02d}.bin", 256 * 1024 * 1024, index + 200) for index in range(4)]
    raise ValueError(f"unknown dataset: {dataset}")


def make_local_dataset(root: Path, dataset: str) -> tuple[str, int, int]:
    root.mkdir(parents=True, exist_ok=True)
    for relative, size, seed in dataset_specs(dataset):
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        remaining = size
        with path.open("wb") as handle:
            block = make_bytes(min(1024 * 1024, max(size, 1)), seed)
            while remaining > 0:
                count = min(remaining, len(block))
                handle.write(block[:count])
                remaining -= count
    return tree_hash_local(root)


def run_remote_python(remote: str, script: str, args: list[str], *, timeout: int = 300) -> subprocess.CompletedProcess[str]:
    command = "python3 - " + " ".join(shlex.quote(arg) for arg in args) + " <<'PY'\n" + script + "\nPY"
    return run_remote(remote, command, timeout=timeout)


def make_remote_tree(remote: str, root: str, dataset: str) -> tuple[str, int, int]:
    specs = dataset_specs(dataset)
    script = """
import json
import hashlib
import shutil
import sys
from pathlib import Path
root = Path(sys.argv[1])
specs = json.loads(sys.argv[2])
if root.exists():
    shutil.rmtree(root)
root.mkdir(parents=True)
for rel, size, seed in specs:
    path = root / rel
    path.parent.mkdir(parents=True, exist_ok=True)
    remaining = int(size)
    with path.open('wb') as handle:
        block_len = min(1024 * 1024, max(remaining, 1))
        block = bytes(((index * 31 + int(seed)) % 251 for index in range(block_len)))
        while remaining > 0:
            count = min(remaining, len(block))
            handle.write(block[:count])
            remaining -= count

digest = hashlib.sha256()
count = 0
total = 0
for path in sorted(item for item in root.rglob('*') if item.is_file()):
    rel = path.relative_to(root).as_posix()
    if '.gridflux.' in rel or '.part.' in rel:
        continue
    file_digest = hashlib.sha256(path.read_bytes()).hexdigest()
    size = path.stat().st_size
    count += 1
    total += size
    digest.update(rel.encode() + b'\\0')
    digest.update(str(size).encode() + b'\\0')
    digest.update(file_digest.encode() + b'\\0')
print(json.dumps({'hash': digest.hexdigest(), 'count': count, 'total': total}))
"""
    completed = run_remote_python(remote, script, [root, json.dumps(specs)], timeout=300)
    data = json.loads(completed.stdout.strip())
    return data["hash"], int(data["count"]), int(data["total"])


def tree_hash_local(root: Path) -> tuple[str, int, int]:
    digest = hashlib.sha256()
    count = 0
    total = 0
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        rel = path.relative_to(root).as_posix()
        if ".gridflux." in rel or ".part." in rel:
            continue
        file_digest = hashlib.sha256(path.read_bytes()).hexdigest()
        size = path.stat().st_size
        count += 1
        total += size
        digest.update(rel.encode("utf-8") + b"\0")
        digest.update(str(size).encode("ascii") + b"\0")
        digest.update(file_digest.encode("ascii") + b"\0")
    return digest.hexdigest(), count, total


def tree_hash_remote(remote: str, root: str) -> tuple[str, int, int]:
    script = """
import json
import hashlib
import sys
from pathlib import Path
root = Path(sys.argv[1])
digest = hashlib.sha256()
count = 0
total = 0
for path in sorted(item for item in root.rglob('*') if item.is_file()):
    rel = path.relative_to(root).as_posix()
    if '.gridflux.' in rel or '.part.' in rel:
        continue
    file_digest = hashlib.sha256(path.read_bytes()).hexdigest()
    size = path.stat().st_size
    count += 1
    total += size
    digest.update(rel.encode() + b'\\0')
    digest.update(str(size).encode() + b'\\0')
    digest.update(file_digest.encode() + b'\\0')
print(json.dumps({'hash': digest.hexdigest(), 'count': count, 'total': total}))
"""
    completed = run_remote_python(remote, script, [root], timeout=300)
    data = json.loads(completed.stdout.strip())
    return data["hash"], int(data["count"]), int(data["total"])


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def start_server(args: argparse.Namespace, root: Path, control_port: int, data_port_base: int, log_path: Path, connections: int, checksum: str) -> subprocess.Popen[str]:
    root.mkdir(parents=True, exist_ok=True)
    command = [
        str(Path(args.local_build_dir) / "gridflux-gridftp-server"),
        "--host",
        args.server_host,
        "--port",
        str(control_port),
        "--root",
        str(root),
        "--data-port-base",
        str(data_port_base),
        "--connections",
        str(connections),
        "--chunk-size",
        str(args.chunk_size),
        "--buffer-size",
        str(args.buffer_size),
        "--checksum",
        checksum,
        "--checksum-backend",
        args.checksum_backend,
    ]
    handle = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(command, stdout=handle, stderr=subprocess.STDOUT, text=True)
    handle.close()
    time.sleep(1.0)
    return process


def stop_server(process: subprocess.Popen[str], log_path: Path) -> None:
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)
    if process.returncode not in (0, -signal.SIGTERM, -signal.SIGKILL):
        raise RuntimeError(log_path.read_text(encoding="utf-8", errors="replace"))


def cleanup_remote(remote: str, *paths: str) -> None:
    if not paths:
        return
    command = "rm -rf " + " ".join(shlex.quote(path) for path in paths)
    run_remote(remote, command, check=False, timeout=60)


def fetch_remote_file(remote: str, remote_path: str, local_path: Path) -> None:
    completed = run_remote(remote, "cat " + shlex.quote(remote_path), check=False, timeout=60)
    if completed.returncode == 0:
        write_text(local_path, completed.stdout)


def parse_summary_line(text: str, prefix: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for line in text.splitlines():
        if not line.startswith(prefix):
            continue
        for token in line.split()[1:]:
            if "=" in token:
                key, value = token.split("=", 1)
                result[key] = value
    return result


def load_json_summary(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    result: dict[str, str] = {}
    for key, value in data.items():
        if key == "error":
            if isinstance(value, dict):
                result["error_message"] = str(value.get("message", ""))
            elif value:
                result["error_message"] = str(value)
            continue
        if isinstance(value, bool):
            result[key] = "1" if value else "0"
        elif value is None:
            result[key] = ""
        else:
            result[key] = str(value)
    return result


def summary_from_completed(completed: subprocess.CompletedProcess[str], json_path: Path, prefix: str) -> dict[str, str]:
    parsed = load_json_summary(json_path)
    if parsed:
        return parsed
    return parse_summary_line(completed.stdout, prefix)


def run_tree_command(remote: str, command: list[str], log_path: Path, *, check: bool = True, timeout: int = 900) -> subprocess.CompletedProcess[str]:
    completed = run_remote(remote, " ".join(shlex.quote(part) for part in command), check=False, timeout=timeout)
    write_text(log_path, "$ " + " ".join(command) + "\n\n" + completed.stdout + completed.stderr)
    if check and completed.returncode != 0:
        raise RuntimeError(log_path.read_text(encoding="utf-8", errors="replace"))
    return completed


def control_port(args: argparse.Namespace, case: Case) -> int:
    return args.control_port_base + case.index * args.port_stride


def data_port_base(args: argparse.Namespace, case: Case) -> int:
    return args.data_port_base + case.index * args.port_stride


def build_cases(args: argparse.Namespace) -> list[Case]:
    datasets = parse_csv_list(args.datasets)
    directions = parse_csv_list(args.directions)
    file_parallelisms = parse_int_list(args.file_parallelisms)
    connections = parse_int_list(args.connections)
    checksums = parse_csv_list(args.checksums)
    resumes = [False, True] if args.resume else [False]
    cases: list[Case] = []
    index = 0
    for dataset in datasets:
        if dataset not in {"small", "mixed", "large"}:
            raise SystemExit(f"invalid dataset: {dataset}")
        for direction in directions:
            if direction not in {"upload", "download"}:
                raise SystemExit(f"invalid direction: {direction}")
            for resume in resumes:
                for fp in file_parallelisms:
                    if fp <= 0:
                        raise SystemExit("--file-parallelisms must be positive")
                    for connection in connections:
                        if connection <= 0:
                            raise SystemExit("--connections must be positive")
                        for checksum in checksums:
                            if checksum not in {"crc32c", "none"}:
                                raise SystemExit(f"invalid checksum: {checksum}")
                            for repeat_index in range(args.repeat):
                                cases.append(Case(index, dataset, direction, resume, repeat_index, fp, connection, checksum))
                                index += 1
    return cases


def run_case(args: argparse.Namespace, case: Case, run_id: str, output_dir: Path) -> dict[str, str]:
    case_id = f"{run_id}_case{case.index:04d}_{case.direction}_{case.dataset}_fp{case.file_parallelism}_{case.checksum}_r{case.repeat_index}"
    local_root = Path(f"/tmp/gridflux-tree-matrix-root-{case_id}")
    remote_source = f"/tmp/gridflux-tree-matrix-source-{case_id}"
    remote_dest = f"/tmp/gridflux-tree-matrix-dest-{case_id}"
    server_log = output_dir / f"{case_id}_server.log"
    client_log = output_dir / f"{case_id}_client.log"
    summary_json = output_dir / f"{case_id}_tree_summary.json"
    remote_summary_json = f"/tmp/{case_id}_tree_summary.json"
    row = {field: "" for field in CSV_FIELDS}
    row.update(
        {
            "timestamp": timestamp_utc(),
            "dataset": case.dataset,
            "direction": case.direction,
            "resume": "1" if case.resume else "0",
            "repeat_index": str(case.repeat_index),
            "file_parallelism": str(case.file_parallelism),
            "connections": str(case.connections),
            "checksum_algorithm": case.checksum,
            "checksum_backend": args.checksum_backend,
            "server_log": str(server_log),
            "client_log": str(client_log),
            "json_summary": str(summary_json),
            "control_port": str(control_port(args, case)),
            "data_port_base": str(data_port_base(args, case)),
            "local_root": str(local_root),
            "remote_source": remote_source,
            "remote_dest": remote_dest,
        }
    )
    process: subprocess.Popen[str] | None = None
    try:
        cleanup_remote(args.remote, remote_source, remote_dest, remote_summary_json)
        if local_root.exists():
            subprocess.run(["rm", "-rf", str(local_root)], check=False)
        local_root.mkdir(parents=True)
        process = start_server(args, local_root, control_port(args, case), data_port_base(args, case), server_log, case.connections, case.checksum)
        start = time.monotonic()
        if case.direction == "upload":
            source_hash, file_count, total_bytes = make_remote_tree(args.remote, remote_source, case.dataset)
            command = [
                f"{args.remote_build_dir.rstrip('/')}/gridflux-tree-upload-client",
                "--host",
                args.server_host,
                "--port",
                str(control_port(args, case)),
                "--source-dir",
                remote_source,
                "--dest-dir",
                "dataset",
                "--connections",
                str(case.connections),
                "--file-parallelism",
                str(case.file_parallelism),
                "--checksum",
                case.checksum,
                "--checksum-backend",
                args.checksum_backend,
                "--json-summary",
                remote_summary_json,
            ]
            if case.resume:
                partial = [*command, "--max-files", "1"]
                run_tree_command(args.remote, partial, client_log, check=False, timeout=args.case_timeout)
                command.append("--resume")
            completed = run_tree_command(args.remote, command, client_log, check=True, timeout=args.case_timeout)
            fetch_remote_file(args.remote, remote_summary_json, summary_json)
            dest_hash, _, _ = tree_hash_local(local_root / "dataset")
            summary = summary_from_completed(completed, summary_json, "tree_upload_complete")
        else:
            source_hash, file_count, total_bytes = make_local_dataset(local_root / "dataset", case.dataset)
            command = [
                f"{args.remote_build_dir.rstrip('/')}/gridflux-tree-download-client",
                "--host",
                args.server_host,
                "--port",
                str(control_port(args, case)),
                "--source-dir",
                "dataset",
                "--dest-dir",
                remote_dest,
                "--connections",
                str(case.connections),
                "--file-parallelism",
                str(case.file_parallelism),
                "--checksum",
                case.checksum,
                "--checksum-backend",
                args.checksum_backend,
                "--json-summary",
                remote_summary_json,
            ]
            if case.resume:
                partial = [*command, "--max-files", "1"]
                run_tree_command(args.remote, partial, client_log, check=False, timeout=args.case_timeout)
                command.append("--resume")
            completed = run_tree_command(args.remote, command, client_log, check=True, timeout=args.case_timeout)
            fetch_remote_file(args.remote, remote_summary_json, summary_json)
            dest_hash, _, _ = tree_hash_remote(args.remote, remote_dest)
            summary = summary_from_completed(completed, summary_json, "tree_download_complete")
        elapsed = time.monotonic() - start
        throughput = float(total_bytes) * 8.0 / elapsed / 1_000_000_000.0 if elapsed > 0 else 0.0
        row.update(
            {
                "file_count": str(file_count),
                "total_bytes": str(total_bytes),
                "elapsed_seconds": summary.get("elapsed_seconds", f"{elapsed:.6f}"),
                "throughput_gbps": summary.get("throughput_gbps", f"{throughput:.6f}"),
                "completed_files": summary.get("completed_files", ""),
                "skipped_files": summary.get("skipped_files", ""),
                "failed_files": summary.get("failed_files", ""),
                "changed_files": summary.get("changed_files", ""),
                "bytes_total": summary.get("bytes_total", summary.get("total_bytes", "")),
                "bytes_transferred": summary.get("bytes_transferred", summary.get("transferred_bytes", "")),
                "summary_tree_hash": summary.get("tree_hash", ""),
                "error_message": summary.get("error_message", ""),
                "source_tree_hash": source_hash,
                "dest_tree_hash": dest_hash,
                "result": "pass" if source_hash == dest_hash else "fail",
            }
        )
        if source_hash != dest_hash:
            row["error"] = "tree hash mismatch"
    except Exception as exc:  # noqa: BLE001
        row["result"] = "fail"
        row["error"] = str(exc).replace("\n", " ")[:1000]
        fetch_remote_file(args.remote, remote_summary_json, summary_json)
        summary = load_json_summary(summary_json)
        if summary:
            row["error_message"] = summary.get("error_message", row["error"])
        if not client_log.exists():
            write_text(client_log, row["error"])
    finally:
        if process is not None:
            try:
                stop_server(process, server_log)
            except Exception as exc:  # noqa: BLE001
                row["result"] = "fail"
                row["error"] = (row.get("error", "") + " stop_server=" + str(exc)).strip()
        cleanup_remote(args.remote, remote_source, remote_dest, remote_summary_json)
    return row


def float_values(rows: list[dict[str, str]], field: str) -> list[float]:
    values: list[float] = []
    for row in rows:
        try:
            values.append(float(row.get(field, "")))
        except ValueError:
            continue
    return values


def summarize_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = {}
    for row in rows:
        key = tuple(row[field] for field in SUMMARY_GROUP_FIELDS)
        groups.setdefault(key, []).append(row)
    summaries: list[dict[str, str]] = []
    for key, grouped in sorted(groups.items()):
        summary = dict(zip(SUMMARY_GROUP_FIELDS, key, strict=True))
        pass_count = sum(1 for row in grouped if row.get("result") == "pass")
        mismatch_count = sum(
            1
            for row in grouped
            if row.get("source_tree_hash") and row.get("dest_tree_hash") and row.get("source_tree_hash") != row.get("dest_tree_hash")
        )
        throughput = float_values(grouped, "throughput_gbps")
        elapsed = float_values(grouped, "elapsed_seconds")
        summary.update(
            {
                "repeat_count": str(len(grouped)),
                "pass_count": str(pass_count),
                "fail_count": str(len(grouped) - pass_count),
                "tree_hash_mismatch_count": str(mismatch_count),
                "throughput_gbps_min": f"{min(throughput):.6f}" if throughput else "",
                "throughput_gbps_median": f"{statistics.median(throughput):.6f}" if throughput else "",
                "throughput_gbps_max": f"{max(throughput):.6f}" if throughput else "",
                "elapsed_seconds_min": f"{min(elapsed):.6f}" if elapsed else "",
                "elapsed_seconds_median": f"{statistics.median(elapsed):.6f}" if elapsed else "",
                "elapsed_seconds_max": f"{max(elapsed):.6f}" if elapsed else "",
                "file_count": grouped[0].get("file_count", ""),
                "total_bytes": grouped[0].get("total_bytes", ""),
                "completed_files": grouped[0].get("completed_files", ""),
                "skipped_files": grouped[0].get("skipped_files", ""),
                "failed_files": grouped[0].get("failed_files", ""),
                "changed_files": grouped[0].get("changed_files", ""),
                "bytes_transferred": grouped[0].get("bytes_transferred", ""),
            }
        )
        summaries.append(summary)
    return summaries


def process_check(remote: str) -> tuple[str, str]:
    pattern = "'[g]ridflux-gridftp-server|[g]ridflux-file-'"
    local = subprocess.run(["bash", "-lc", f"pgrep -af {pattern} || true"], text=True, capture_output=True, check=False).stdout.strip()
    remote_text = run_remote(remote, f"pgrep -af {pattern} || true", check=False, timeout=30).stdout.strip()
    return local, remote_text


def main() -> int:
    parser = argparse.ArgumentParser(description="Run private GridFTP-like tree transfer matrix.")
    parser.add_argument("--remote", required=True)
    parser.add_argument("--server-host", required=True)
    parser.add_argument("--local-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--remote-build-dir", default="/root/projects/GridFlux/build")
    parser.add_argument("--output-dir", default="tools/perf/results")
    parser.add_argument("--control-port-base", type=int, default=22310)
    parser.add_argument("--data-port-base", type=int, default=23300)
    parser.add_argument("--port-stride", type=int, default=20)
    parser.add_argument("--directions", default="upload,download")
    parser.add_argument("--datasets", default="small")
    parser.add_argument("--file-parallelisms", default="1")
    parser.add_argument("--connections", default="2")
    parser.add_argument("--checksums", default="crc32c")
    parser.add_argument("--checksum-backend", choices=["auto", "software", "hardware"], default="auto")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--chunk-size", type=int, default=1048576)
    parser.add_argument("--buffer-size", type=int, default=65536)
    parser.add_argument("--case-timeout", type=int, default=900)
    args = parser.parse_args()

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    run_id = compact_timestamp()
    raw_path = output_dir / f"{run_id}_gridftp-tree-private-matrix.csv"
    summary_path = output_dir / f"{run_id}_gridftp-tree-private-matrix-summary.csv"
    cases = build_cases(args)
    rows: list[dict[str, str]] = []
    try:
        for case in cases:
            row = run_case(args, case, run_id, output_dir)
            rows.append(row)
            with raw_path.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
                writer.writeheader()
                writer.writerows(rows)
            with summary_path.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=SUMMARY_FIELDS)
                writer.writeheader()
                writer.writerows(summarize_rows(rows))
        local_processes, remote_processes = process_check(args.remote)
        if local_processes or remote_processes:
            if local_processes:
                print("local residual processes:\n" + local_processes, file=sys.stderr)
            if remote_processes:
                print("remote residual processes:\n" + remote_processes, file=sys.stderr)
            return 2
    finally:
        pass
    fail_count = sum(1 for row in rows if row.get("result") != "pass")
    print(f"raw_csv={raw_path}")
    print(f"summary_csv={summary_path}")
    print(f"result={'pass' if fail_count == 0 else 'fail'} fail_count={fail_count}")
    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
