#!/usr/bin/env python3
"""Loopback correctness gate for persistent global scheduler dispatch."""
import json
import subprocess
import tempfile
from pathlib import Path
from tree_smoke_common import free_port, make_tree, start_server, stop_server, tree_hash

def run_client(binary: Path, args: list[str], summary: Path) -> dict:
    completed = subprocess.run([str(binary), *args, "--json-summary", str(summary)], text=True, capture_output=True, check=False, timeout=60)
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout + completed.stderr)
    return json.loads(summary.read_text(encoding="utf-8"))

def main() -> int:
    import argparse
    parser = argparse.ArgumentParser(); parser.add_argument("--build-dir", type=Path, required=True); args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cpnetflux-v3-unified-executor.") as text:
        root = Path(text); source = root / "source"; source.mkdir(); make_tree(source)
        (source / "nested" / "large.bin").write_bytes(b"L" * (2 * 1024 * 1024 + 17))
        server_root = root / "server"; server_root.mkdir(); control_port = free_port(); data_port = free_port(); server_log = root / "server.log"
        server = start_server(args.build_dir, server_root, control_port, data_port, server_log)
        try:
            common = ["--host", "127.0.0.1", "--port", str(control_port), "--connections", "1", "--file-parallelism", "2", "--checksum", "crc32c", "--data-session-reuse", "tree", "--file-scheduling", "dynamic", "--control-pipeline-depth", "2", "--scheduler", "global", "--scheduler-policy", "adaptive", "--compression", "off"]
            upload_metrics = root / "upload-metrics"
            upload = run_client(args.build_dir / "cpnetflux-tree-upload-client", [*common, "--scheduler-link-id", "loopback-up", "--scheduler-metrics-dir", str(upload_metrics), "--source-dir", str(source), "--dest-dir", "dataset"], root / "upload.json")
            download_metrics = root / "download-metrics"; destination = root / "download"
            download = run_client(args.build_dir / "cpnetflux-tree-download-client", [*common, "--scheduler-link-id", "loopback-down", "--scheduler-metrics-dir", str(download_metrics), "--source-dir", "dataset", "--dest-dir", str(destination)], root / "download.json")
            expected = tree_hash(source)
            if tree_hash(server_root / "dataset") != expected or tree_hash(destination) != expected: raise RuntimeError("persistent global scheduler loopback tree hash mismatch")
            for summary in (upload, download):
                if summary["result"] != "pass" or summary["actual_mode"] != "persistent_tree" or summary["actual_file_scheduling"] != "dynamic" or summary["scheduler_mode"] != "global": raise RuntimeError(f"unexpected transfer summary: {summary}")
            for metrics in (upload_metrics, download_metrics):
                events = (metrics / "scheduler_events.jsonl").read_text(encoding="utf-8").splitlines()
                if not any('"event":"workitem_dispatch"' in line for line in events): raise RuntimeError("scheduler dispatch event missing")
                if not any('"event":"executor_complete"' in line for line in events): raise RuntimeError("executor completion event missing")
                if not any('"lease_id":' in line and '"connection_epoch":' in line for line in events): raise RuntimeError("lease identity fields missing")
        finally:
            stop_server(server, server_log)
    print("v3 unified executor loopback passed")
    return 0
if __name__ == "__main__": raise SystemExit(main())
