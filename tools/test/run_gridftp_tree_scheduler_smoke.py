#!/usr/bin/env python3
import argparse
import csv
import io
import json
import random
import shutil
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, make_tree, run_checked, start_server, stop_server, tree_hash


def make_scheduler_tree(root: Path) -> None:
    make_tree(root)
    big_size = 72 * 1024 * 1024
    pattern = b"CPNetFluxSchedulerCoreMVP"
    payload = (pattern * (big_size // len(pattern) + 1))[:big_size]
    (root / "nested" / "scheduler-big.bin").write_bytes(payload)
    random_source = random.Random(0x5343484544)
    (root / "nested" / "scheduler-random.bin").write_bytes(
        random_source.randbytes(big_size)
    )


def scheduler_args(metrics_dir: Path, policy: str = "adaptive") -> list[str]:
    return [
        "--scheduler",
        "global",
        "--scheduler-policy",
        policy,
        "--scheduler-metrics-dir",
        str(metrics_dir),
        "--scheduler-link-id",
        "link0",
        "--scheduler-min-compress-gbps",
        "0.000001",
    ]


def validate_event_order(line: str) -> None:
    ordered_fields = [
        '"ts":"',
        '"event":"',
        '"task_id":"',
        '"file_id":"',
        '"link_id":"',
        '"offset":',
        '"length":',
        '"reason":"',
        '"ready_bytes":',
        '"target_connections":',
        '"result":"',
        '"message":"',
    ]
    positions = [line.find(field) for field in ordered_fields]
    if any(position < 0 for position in positions):
        raise RuntimeError("scheduler event missing required fields: " + line)
    if positions != sorted(positions):
        raise RuntimeError("scheduler event field order changed: " + line)


def validate_metrics(
    metrics_dir: Path,
    *,
    expect_raw_fallback: bool,
    expect_compressed: bool,
    expect_compression_decision: bool,
    policy: str,
) -> None:
    summary_lines = (metrics_dir / "scheduler_summary.csv").read_text(encoding="utf-8").splitlines()
    events_lines = (metrics_dir / "scheduler_events.jsonl").read_text(encoding="utf-8").splitlines()
    sample_lines = (metrics_dir / "scheduler_samples.csv").read_text(encoding="utf-8").splitlines()

    expected_summary_header = (
        "task_id,total_bytes,logical_bytes,wire_bytes,elapsed_seconds,goodput_gbps,wire_gbps,"
        "compression_ratio_effective,compression_attempts,compressed_workitems,"
        "raw_fallback_workitems,compression_failures,decompression_failures,retried_workitems,"
        "result,dominant_bottleneck"
    )
    expected_sample_header = (
        "file_id,offset,length,sample_ratio,sample_comp_gbps,decision,reason,cpu_percent,"
        "link_ready_ratio"
    )

    if not summary_lines or not summary_lines[0].startswith(expected_summary_header):
        raise RuntimeError("scheduler summary header mismatch")
    if ",policy,initial_connections,current_connections,target_connections,max_connections," not in summary_lines[0]:
        raise RuntimeError("scheduler summary missing phase2 policy fields")
    if len(summary_lines) < 2 or ",pass," not in summary_lines[1]:
        raise RuntimeError("scheduler summary did not record a passing run")
    if f",{policy}," not in summary_lines[1]:
        raise RuntimeError("scheduler summary did not record scheduler policy")
    if not sample_lines or sample_lines[0] != expected_sample_header:
        raise RuntimeError("scheduler samples header mismatch")
    if expect_raw_fallback and not any("sample_unavailable" in line for line in sample_lines[1:]):
        raise RuntimeError("scheduler download samples did not record raw fallback")
    summary = next(csv.DictReader(io.StringIO("\n".join(summary_lines))))
    if expect_compressed:
        if int(summary["compression_attempts"]) == 0:
            raise RuntimeError("scheduler upload did not attempt compression")
        if int(summary["compressed_workitems"]) == 0:
            raise RuntimeError("scheduler upload did not send compressed DATA")
        if float(summary["compression_ratio_effective"]) >= 1.0:
            raise RuntimeError("scheduler upload compression ratio is not below one")
        if int(summary["raw_fallback_workitems"]) == 0:
            raise RuntimeError("scheduler upload did not record raw fallback")
    else:
        if int(summary["compressed_workitems"]) != 0:
            raise RuntimeError("scheduler download unexpectedly sent compressed DATA")
    if not any("workitem_dispatch" in line for line in events_lines):
        raise RuntimeError("scheduler events missing workitem_dispatch")
    if not any("executor_complete" in line for line in events_lines):
        raise RuntimeError("scheduler events missing executor_complete")
    if policy == "adaptive" and not any("queue_low" in line for line in events_lines):
        raise RuntimeError("adaptive scheduler events missing queue_low evidence")
    if expect_compression_decision and not any(
        ("compression_candidate" in line) or ("compression_reject" in line)
        for line in events_lines
    ):
        raise RuntimeError("scheduler events missing compression decision")
    if expect_compressed and not any("compression_dispatch" in line for line in events_lines):
        raise RuntimeError("scheduler upload events missing compression_dispatch")
    if expect_compressed and not any("compression_fallback_raw" in line for line in events_lines):
        raise RuntimeError("scheduler upload events missing compression_fallback_raw")
    if expect_compressed and not any("compression_reject" in line for line in events_lines):
        raise RuntimeError("scheduler upload events missing incompressible-file rejection")
    validate_event_order(events_lines[0])


def run_phase(
    build_dir: Path,
    control_port: int,
    data_port: int,
    server_root: Path,
    server_log: Path,
    source_root: Path,
    *,
    upload: bool,
    metrics_dir: Path,
    policy: str = "adaptive",
    destination: Path | None = None,
) -> None:
    if upload:
        base_cmd = [
            str(build_dir / "cpnetflux-tree-upload-client"),
            "--host",
            "127.0.0.1",
            "--port",
            str(control_port),
            "--source-dir",
            str(source_root),
            "--dest-dir",
            "dataset",
            "--connections",
            "2",
            "--file-parallelism",
            "2",
        ]
        expect_raw_fallback = False
    else:
        assert destination is not None
        base_cmd = [
            str(build_dir / "cpnetflux-tree-download-client"),
            "--host",
            "127.0.0.1",
            "--port",
            str(control_port),
            "--source-dir",
            "dataset",
            "--dest-dir",
            str(destination),
            "--connections",
            "2",
            "--file-parallelism",
            "2",
        ]
        expect_raw_fallback = True

    transfer_root = source_root if upload else destination
    assert transfer_root is not None
    compression_args = ["--compression", "auto"] if upload else []
    bad_metrics = transfer_root / "scheduler-metrics"
    run_checked(base_cmd + compression_args + scheduler_args(bad_metrics, policy), expect_success=False)
    run_checked(base_cmd + compression_args + scheduler_args(metrics_dir, policy) + ["--max-files", "1"], expect_success=False)
    run_checked(base_cmd + compression_args + scheduler_args(metrics_dir, policy) + ["--resume"])
    validate_metrics(
        metrics_dir,
        expect_raw_fallback=expect_raw_fallback,
        expect_compressed=upload,
        expect_compression_decision=upload,
        policy=policy,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux tree scheduler smoke.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--results-dir")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)

    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-scheduler.") as temp_text:
        temp = Path(temp_text)
        source = temp / "source"
        source.mkdir()
        make_scheduler_tree(source)

        server_root = temp / "server-root"
        server_root.mkdir()
        control_port = free_port()
        data_port = free_port()
        server_log = temp / "server.log"
        server = start_server(build_dir, server_root, control_port, data_port, server_log)
        try:
            off_source = temp / "off-source"
            off_source.mkdir()
            make_tree(off_source)
            off_metrics = temp / "off-metrics"
            run_checked(
                [
                    str(build_dir / "cpnetflux-tree-upload-client"),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(control_port),
                    "--source-dir",
                    str(off_source),
                    "--dest-dir",
                    "off-dataset",
                    "--connections",
                    "1",
                    "--file-parallelism",
                    "1",
                    "--scheduler",
                    "off",
                    "--scheduler-metrics-dir",
                    str(off_metrics),
                ]
            )
            if off_metrics.exists():
                raise RuntimeError("scheduler=off unexpectedly wrote compression sidecars")

            upload_metrics = temp / "upload-metrics"
            run_phase(build_dir, control_port, data_port, server_root, server_log, source,
                      upload=True, metrics_dir=upload_metrics, policy="adaptive")

            expected, _, _ = tree_hash(source)
            uploaded, _, _ = tree_hash(server_root / "dataset")
            if expected != uploaded:
                raise RuntimeError("scheduler upload tree hash mismatch")

            download_dest = temp / "downloaded"
            download_metrics = temp / "download-metrics"
            run_phase(build_dir, control_port, data_port, server_root, server_log, source,
                      upload=False, metrics_dir=download_metrics, policy="adaptive",
                      destination=download_dest)

            downloaded, _, _ = tree_hash(download_dest)
            if expected != downloaded:
                raise RuntimeError("scheduler download tree hash mismatch")
            if args.results_dir:
                results_dir = Path(args.results_dir)
                results_dir.mkdir(parents=True, exist_ok=True)
                shutil.copytree(upload_metrics, results_dir / "upload-metrics", dirs_exist_ok=True)
                shutil.copytree(download_metrics, results_dir / "download-metrics", dirs_exist_ok=True)
                shutil.copy2(server_log, results_dir / "server.log")
                (results_dir / "phase3_evidence.json").write_text(
                    json.dumps(
                        {
                            "source_tree_hash": expected,
                            "uploaded_tree_hash": uploaded,
                            "downloaded_tree_hash": downloaded,
                            "upload_metrics_dir": str(results_dir / "upload-metrics"),
                            "download_metrics_dir": str(results_dir / "download-metrics"),
                            "scope": "local correctness, resume, fallback, metrics, bounded behavior",
                        },
                        indent=2,
                    )
                    + "\n",
                    encoding="utf-8",
                )
        finally:
            stop_server(server, server_log)

    print("tree scheduler smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
