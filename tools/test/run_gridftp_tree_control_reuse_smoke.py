#!/usr/bin/env python3
import argparse
import json
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, run_checked, start_server, stop_server, tree_hash


def make_many_small_files(root: Path, count: int = 24) -> None:
    (root / "small").mkdir(parents=True)
    for index in range(count):
        payload = bytes(((index + offset) % 251 for offset in range(2048 + index)))
        (root / "small" / f"file-{index:03d}.bin").write_bytes(payload)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def run_upload_case(build_dir: Path, temp: Path, mode: str) -> dict:
    source = temp / f"source-{mode}"
    source.mkdir()
    make_many_small_files(source)
    server_root = temp / f"server-root-{mode}"
    server_root.mkdir()
    summary = temp / f"{mode}-summary.json"
    control_port = free_port()
    data_port = free_port()
    server_log = temp / f"{mode}-server.log"
    server = start_server(build_dir, server_root, control_port, data_port, server_log)
    try:
        run_checked(
            [
                str(build_dir / "cpnetflux-tree-upload-client"),
                "--host",
                "127.0.0.1",
                "--port",
                str(control_port),
                "--source-dir",
                str(source),
                "--dest-dir",
                "dataset",
                "--connections",
                "2",
                "--file-parallelism",
                "4",
            "--control-reuse",
            mode,
            "--data-session-reuse",
            "off",
                "--planner-preset",
                f"test_{mode}",
                "--json-summary",
                str(summary),
            ]
        )
        expected, file_count, _ = tree_hash(source)
        actual, _, _ = tree_hash(server_root / "dataset")
        if expected != actual:
            raise RuntimeError(f"tree hash mismatch for {mode}: {expected} != {actual}")
        data = load_json(summary)
        if data.get("control_reuse_mode") != mode:
            raise RuntimeError(f"summary control_reuse_mode mismatch for {mode}")
        if data.get("planner_preset") != f"test_{mode}":
            raise RuntimeError(f"summary planner_preset mismatch for {mode}")
        if int(data.get("data_transfer_count", 0)) != file_count:
            raise RuntimeError(f"summary data_transfer_count mismatch for {mode}")
        return data
    finally:
        stop_server(server, server_log)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux tree control reuse smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-control-reuse.") as temp_text:
        temp = Path(temp_text)
        off = run_upload_case(build_dir, temp, "off")
        worker = run_upload_case(build_dir, temp, "worker")
        file_count = int(worker["file_count"])
        off_connects = int(off.get("control_connect_count", 0))
        worker_connects = int(worker.get("control_connect_count", 0))
        if off_connects < file_count:
            raise RuntimeError("off mode should open at least one control connection per file")
        if worker_connects >= file_count:
            raise RuntimeError("worker mode should reuse control connections across files")
        print(
            "tree control reuse smoke passed "
            f"files={file_count} off_connects={off_connects} worker_connects={worker_connects}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
