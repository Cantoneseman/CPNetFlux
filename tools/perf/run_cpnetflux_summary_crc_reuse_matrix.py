#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "test"))
from gridftp_port_window import clamp_passive_data_port_base
from run_gridftp_data_tls_smoke import generate_cert
from tree_smoke_common import free_port, stop_server

def sha(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def sha_map(root):
    return {path.relative_to(root).as_posix(): sha(path)
            for path in sorted(root.rglob("*")) if path.is_file()
            and ".cpnetflux." not in path.relative_to(root).as_posix()
            and ".part." not in path.relative_to(root).as_posix()}

def make_datasets(root):
    block = bytes(i % 251 for i in range(65536))
    result = {}
    large = root / "4g"
    large.mkdir(exist_ok=True)
    with (large / "large.bin").open("wb") as stream:
        for _ in range(65536):
            stream.write(block)
    result["4g"] = large
    dense = root / "dense"
    dense.mkdir(exist_ok=True)
    for index in range(2048):
        (dense / f"f-{index:04d}.bin").write_bytes(block)
    result["dense"] = dense
    mixed = root / "mixed"
    mixed.mkdir(exist_ok=True)
    with (mixed / "large.bin").open("wb") as stream:
        for _ in range(1024):
            stream.write(block)
    for index in range(128):
        (mixed / f"small-{index:03d}.bin").write_bytes(block)
    result["mixed"] = mixed
    return result

def run_one(build, source, cert, key, case, expected_hash, expected_shas, expected_source):
    peer = case / "peer"
    peer.mkdir()
    port = free_port()
    data_port = clamp_passive_data_port_base(free_port())
    server_log = case / "server.log"
    server_cmd = [
        str(build / "cpnetflux-gridftp-server"), "--host", "127.0.0.1",
        "--port", str(port), "--root", str(peer), "--data-port-base", str(data_port),
        "--connections", "4", "--link-set-max-links", "4", "--checksum", "crc32c", "--checksum-backend", "auto",
        "--chunk-size", "4194304", "--buffer-size", "65536",
        "--manifest-flush-policy", "final_only", "--final-verify-policy", "verified_chunks",
        "--commit-sync-policy", "none", "--tls-mode", "required",
        "--tls-cert-file", str(cert), "--tls-key-file", str(key), "--data-tls-mode", "required",
    ]
    with server_log.open("w") as output:
        server = subprocess.Popen(server_cmd, stdout=output, stderr=subprocess.STDOUT)
    try:
        import socket
        deadline = time.monotonic() + 10
        while True:
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.02)
        summary = case / "summary.json"
        command = [
            str(build / "cpnetflux-tree-upload-client"), "--host", "127.0.0.1",
            "--port", str(port), "--source-dir", str(source), "--dest-dir", "received",
            "--checksum", "crc32c", "--checksum-backend", "auto", "--file-parallelism", "4",
            "--connections", "4", "--link-set-links", "4", "--chunk-size", "4194304",
            "--buffer-size", "65536", "--control-reuse", "worker", "--data-session-reuse", "tree",
            "--control-pipeline-depth", "2", "--compression", "off", "--tls-mode", "required",
            "--tls-ca-file", str(cert), "--tls-server-name", "localhost",
            "--data-tls-mode", "required", "--json-summary", str(summary),
        ]
        start = time.monotonic()
        result = subprocess.run(command, capture_output=True, text=True,
            env=dict(os.environ, CPNETFLUX_RANGE_CHECKPOINT_BATCH="1"), timeout=300)
        wall = time.monotonic() - start
        (case / "client.log").write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stdout + result.stderr
        summary_data = json.loads(summary.read_text())
        assert summary_data["result"] == "pass", summary_data
        if expected_hash is not None:
            assert summary_data["tree_hash"] == expected_hash, summary_data
        actual_source = summary_data.get("summary_tree_hash_source", "filesystem_read")
        assert actual_source == expected_source, (actual_source, expected_source)
        actual_shas = sha_map(peer / "received")
        assert actual_shas == expected_shas, "independent destination SHA-256 mismatch"
        return dict(process_wall_seconds=wall, transfer_seconds=summary_data["elapsed_seconds"],
            summary_hash_seconds=summary_data["summary_tree_hash_seconds"],
            summary_hash_source=actual_source, tree_hash=summary_data["tree_hash"])
    finally:
        stop_server(server, server_log)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-build", type=Path, required=True)
    parser.add_argument("--candidate-build", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--only", choices=("4g", "dense", "mixed"), default=None)
    parser.add_argument("--input-dir", type=Path, default=None)
    args = parser.parse_args()
    if args.rounds < 5:
        parser.error("requires at least five paired runs")
    root = args.evidence_dir.absolute()
    root.mkdir(parents=True, exist_ok=True)
    baseline = args.baseline_build.absolute()
    candidate = args.candidate_build.absolute()
    cert = root / "cert.pem"
    key = root / "key.pem"
    if not cert.exists() or not key.exists():
        assert generate_cert(cert, key), "openssl unavailable"
    if args.input_dir:
        input_root = args.input_dir.absolute()
        selected = (args.only,) if args.only else ("4g", "dense", "mixed")
        datasets = {name: input_root / name for name in selected}
        if not all(path.is_dir() for path in datasets.values()):
            parser.error("input directory is missing a selected dataset")
    else:
        datasets = make_datasets(root)
        if args.only:
            datasets = {args.only: datasets[args.only]}
    binary_hashes = {
        arm: {name: sha(build / name) for name in
              ("cpnetflux-gridftp-server", "cpnetflux-tree-upload-client")}
        for arm, build in (("baseline", baseline), ("candidate", candidate))
    }
    report = dict(schema="cpnetflux_summary_crc_reuse_matrix_v1",
        baseline_commit="0d4686cc21f1d8083c189119df576906ce419ae4",
        candidate_commit=subprocess.check_output(
            ["git", "-C", "/root/projects/CPNetFlux", "rev-parse", "HEAD"], text=True).strip(),
        binary_sha256=binary_hashes,
        conditions=dict(transport="loopback TCP", tls="control+data required",
            checksum="CRC32C Auto", compression="off", connections=4,
            chunk_bytes=4194304, buffer_bytes=65536, checkpoint_batch=1,
            independent_sha256_outside_timed_wall=True), datasets={}, runs=[])
    for name, source in datasets.items():
        expected_shas = sha_map(source)
        byte_count = sum(path.stat().st_size for path in source.rglob("*") if path.is_file())
        calibration = root / f"{name}-calibration"
        if calibration.exists():
            shutil.rmtree(calibration)
        calibration.mkdir()
        reference = run_one(baseline, source, cert, key, calibration, None,
                            expected_shas, "filesystem_read")
        expected_hash = reference["tree_hash"]
        shutil.rmtree(calibration)
        report["datasets"][name] = dict(files=len(expected_shas), bytes=byte_count,
            tree_hash_legacy_full_read=expected_hash, source_sha256=expected_shas)
        candidate_source = "verified_chunks" if byte_count >= 128 * 1024 * 1024 else "filesystem_read"
        for pair in range(args.rounds):
            arms = [("baseline", baseline, "filesystem_read"),
                    ("candidate", candidate, candidate_source)]
            if pair % 2:
                arms.reverse()
            for arm, build, expected_source in arms:
                label = f"{name}-{pair}-{arm}"
                case = root / label
                case.mkdir()
                row = run_one(build, source, cert, key, case, expected_hash,
                              expected_shas, expected_source)
                row.update(dataset=name, pair=pair, arm=arm)
                report["runs"].append(row)
                (root / "result.json").write_text(json.dumps(report, indent=2) + "\n")
                print(json.dumps({"label": label, "wall": row["process_wall_seconds"],
                    "transfer": row["transfer_seconds"], "summary_hash": row["summary_hash_seconds"],
                    "source": expected_source}), flush=True)
                shutil.rmtree(case)
    report["aggregates"] = {}
    for name in datasets:
        rows = [row for row in report["runs"] if row["dataset"] == name]
        paired = []
        for pair in range(args.rounds):
            before = next(row["process_wall_seconds"] for row in rows
                          if row["pair"] == pair and row["arm"] == "baseline")
            after = next(row["process_wall_seconds"] for row in rows
                         if row["pair"] == pair and row["arm"] == "candidate")
            paired.append((before - after) / before * 100)
        report["aggregates"][name] = dict(
            baseline_median=statistics.median(row["process_wall_seconds"] for row in rows
                if row["arm"] == "baseline"),
            candidate_median=statistics.median(row["process_wall_seconds"] for row in rows
                if row["arm"] == "candidate"),
            paired_improvement_median_percent=statistics.median(paired),
            paired_improvements_percent=paired,
            baseline_summary_median=statistics.median(row["summary_hash_seconds"] for row in rows
                if row["arm"] == "baseline"),
            candidate_summary_median=statistics.median(row["summary_hash_seconds"] for row in rows
                if row["arm"] == "candidate"),
            candidate_sources=sorted({row["summary_hash_source"] for row in rows
                if row["arm"] == "candidate"}))
    if "4g" in report["aggregates"]:
        report["decision"] = "go" if (
            report["aggregates"]["4g"]["paired_improvement_median_percent"] >= 10 and
            all(report["aggregates"][name]["paired_improvement_median_percent"] >= -5
                for name in ("dense", "mixed"))) else "no_go"
    else:
        report["decision"] = "mixed_pass" if (
            report["aggregates"]["mixed"]["paired_improvement_median_percent"] >= -5) else "mixed_no_go"
    report["result"] = "pass"
    (root / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"result": "pass", "decision": report["decision"],
                      "aggregates": report["aggregates"]}), flush=True)

if __name__ == "__main__":
    main()
