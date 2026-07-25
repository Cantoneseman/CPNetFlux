#!/usr/bin/env python3
import argparse
import json
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, run_checked, start_server, stop_server, tree_hash


def make_single_file(root: Path) -> int:
    root.mkdir(parents=True)
    payload = bytes((index * 31) % 251 for index in range(2 * 1024 * 1024 + 123))
    path = root / "artifact.lz4"
    path.write_bytes(payload)
    return len(payload)


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def run_recovery_case(build_dir: Path, temp: Path) -> None:
    source = temp / "source"
    total_bytes = make_single_file(source)
    server_root = temp / "server-root"
    server_root.mkdir()
    summary = temp / "recovery-summary.json"
    event_log = temp / "recovery-events.jsonl"
    control_port = free_port()
    data_port = free_port()
    server_log = temp / "recovery-server.log"
    server = start_server(
        build_dir,
        server_root,
        control_port,
        data_port,
        server_log,
        extra_env={"GRIDFLUX_TEST_DELAY_BEFORE_DATA_COMPLETE_MS": "1500"},
    )
    try:
        run_checked(
            [
                str(build_dir / "gridflux-tree-upload-client"),
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
                "1",
                "--control-reuse",
                "worker",
                "--planner-preset",
                "test_data_final_status_timeout",
                "--data-final-status-timeout-seconds",
                "1",
                "--json-summary",
                str(summary),
                "--event-log",
                str(event_log),
            ]
        )
        expected, file_count, _ = tree_hash(source)
        actual, _, _ = tree_hash(server_root / "dataset")
        if expected != actual:
            raise RuntimeError(f"tree hash mismatch: {expected} != {actual}")
        data = load_json(summary)
        if data.get("result") != "pass":
            raise RuntimeError(f"summary did not pass: {data}")
        if int(data.get("completed_files", 0)) != file_count:
            raise RuntimeError(f"completed_files mismatch: {data}")
        if int(data.get("bytes_transferred", 0)) != total_bytes:
            raise RuntimeError(f"bytes_transferred mismatch: {data}")
        if int(data.get("data_transfer_count", 0)) != file_count:
            raise RuntimeError(f"data_transfer_count mismatch: {data}")
        if data.get("recovered_after_data_final_status_timeout") is not True:
            raise RuntimeError(f"recovery flag missing: {data}")
        events = event_log.read_text(encoding="utf-8")
        if "file_recovered_after_data_final_status_timeout" not in events:
            raise RuntimeError("recovery event missing")
    finally:
        stop_server(server, server_log)


def run_non_final_error_case(build_dir: Path, temp: Path) -> None:
    source = temp / "negative-source"
    make_single_file(source)
    summary = temp / "negative-summary.json"
    unused_control_port = free_port()
    completed = run_checked(
        [
            str(build_dir / "gridflux-tree-upload-client"),
            "--host",
            "127.0.0.1",
            "--port",
            str(unused_control_port),
            "--source-dir",
            str(source),
            "--dest-dir",
            "dataset",
            "--connections",
            "1",
            "--file-parallelism",
            "1",
            "--data-final-status-timeout-seconds",
            "1",
            "--json-summary",
            str(summary),
        ],
        expect_success=False,
    )
    data = load_json(summary)
    if data.get("result") != "fail":
        raise RuntimeError(f"negative summary unexpectedly passed: {data}")
    if data.get("recovered_after_data_final_status_timeout") is not False:
        raise RuntimeError(f"negative summary recovered unexpectedly: {data}")
    message = str(data.get("error", {}).get("message", ""))
    if "data final status timeout" in message:
        raise RuntimeError(f"negative case misclassified as final status timeout: {message}")
    if completed.returncode == 0:
        raise RuntimeError("negative command unexpectedly succeeded")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run GridFlux tree data final status timeout smoke."
    )
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="gridflux-tree-final-status.") as temp_text:
        temp = Path(temp_text)
        run_recovery_case(build_dir, temp)
        run_non_final_error_case(build_dir, temp)
        print("tree data final status timeout smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
