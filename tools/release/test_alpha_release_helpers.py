#!/usr/bin/env python3
"""Lightweight tests for alpha release helper functions."""

from __future__ import annotations

import csv
import json
import tempfile
import os
from pathlib import Path

import check_remote_artifact_sync
import remote_auth
import run_alpha_release_candidate
import run_alpha_release_gate
import sync_remote_artifacts


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def test_source_tree_hash_excludes_private_and_build() -> None:
    with tempfile.TemporaryDirectory(prefix="gridflux-alpha-hash.") as temp:
        root = Path(temp)
        write(root / "src" / "main.cpp", "int main() { return 0; }\n")
        first = run_alpha_release_gate.source_tree_hash(root)
        write(root / "AGENTS.md", "private password\n")
        write(root / "build-private" / "artifact.txt", "binary-ish\n")
        write(root / "tools" / "perf" / "results" / "sample.csv", "secret\n")
        second = run_alpha_release_gate.source_tree_hash(root)
        if first != second:
            raise AssertionError("source tree hash included private/build/result files")


def test_csv_sidecar_extraction() -> None:
    with tempfile.TemporaryDirectory(prefix="gridflux-alpha-csv.") as temp:
        root = Path(temp)
        csv_path = root / "tools" / "perf" / "results" / "matrix.csv"
        csv_path.parent.mkdir(parents=True)
        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(
                handle,
                fieldnames=["server_log", "client_env_before_log", "absolute_log", "result"],
            )
            writer.writeheader()
            writer.writerow(
                {
                    "server_log": "tools/perf/results/server.log",
                    "client_env_before_log": "tools/perf/results/client-env.log",
                    "absolute_log": str(root / "tools" / "perf" / "results" / "abs.log"),
                    "result": "pass",
                }
            )
        paths = check_remote_artifact_sync.paths_from_csv(csv_path, root)
        expected = {
            "tools/perf/results/matrix.csv",
            "tools/perf/results/server.log",
            "tools/perf/results/client-env.log",
            "tools/perf/results/abs.log",
        }
        if paths != expected:
            raise AssertionError(f"unexpected sidecar paths: {paths}")
        gate_paths = set(run_alpha_release_gate.csv_referenced_artifacts(str(csv_path), root))
        if gate_paths != expected:
            raise AssertionError(f"unexpected gate sidecar paths: {gate_paths}")


def test_matrix_summary_default_baseline() -> None:
    with tempfile.TemporaryDirectory(prefix="gridflux-alpha-summary.") as temp:
        summary = Path(temp) / "summary.csv"
        fields = [
            "file_io_backend",
            "file_io_buffer_size",
            "posix_write_strategy",
            "manifest_flush_policy",
            "manifest_flush_interval_chunks",
            "commit_sync_policy",
            "final_verify_policy",
            "checksum_algorithm",
            "fail_count",
            "direction",
            "throughput_gbps_median",
            "throughput_gbps_spread_pct",
        ]
        with summary.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerow(
                {
                    "file_io_backend": "posix",
                    "file_io_buffer_size": "0",
                    "posix_write_strategy": "auto",
                    "manifest_flush_policy": "every_n_chunks",
                    "manifest_flush_interval_chunks": "16",
                    "commit_sync_policy": "none",
                    "final_verify_policy": "full",
                    "checksum_algorithm": "crc32c",
                    "fail_count": "0",
                    "direction": "stor",
                    "throughput_gbps_median": "1.0",
                    "throughput_gbps_spread_pct": "10.0",
                }
            )
        result = run_alpha_release_gate.parse_matrix_summary(summary)
        if result["status"] != "pass" or result["fail_count"] != 0:
            raise AssertionError(f"unexpected matrix summary status: {result}")
        baseline = result["default_baseline"]
        if not isinstance(baseline, list) or len(baseline) != 1:
            raise AssertionError(f"default baseline not detected: {result}")


def test_artifact_manifest_excludes_private_paths_and_includes_sidecars() -> None:
    with tempfile.TemporaryDirectory(prefix="gridflux-alpha-manifest.") as temp:
        root = Path(temp)
        write(root / "INDEX.md", "index\n")
        write(root / "docs" / "ROADMAP.md", "roadmap\n")
        write(root / "docs" / "PROJECT_STATE.md", "state\n")
        write(root / "docs" / "perf" / "README.md", "perf\n")
        write(root / "docs" / "perf" / "PHASE5B_TREE_DATASET_MATRIX.md", "tree matrix report\n")
        write(root / "docs" / "release" / "ALPHA_RELEASE_GATE.md", "gate\n")
        write(root / "tools" / "release" / "helper.py", "print('ok')\n")
        write(root / "tools" / "perf" / "run_gridftp_tree_private_matrix.py", "print('matrix')\n")
        write(root / "tools" / "perf" / "analyze_phase5b.py", "print('analyze')\n")
        write(root / "AGENTS.md", "password\n")
        write(root / "build-private" / "artifact.log", "private\n")
        csv_path = root / "tools" / "perf" / "results" / "matrix.csv"
        tree_csv_path = root / "tools" / "perf" / "results" / "20260518T000000Z_gridftp-tree-private-matrix.csv"
        tree_summary_path = root / "tools" / "perf" / "results" / "20260518T000000Z_gridftp-tree-private-matrix-summary.csv"
        write(root / "tools" / "perf" / "results" / "server.log", "server\n")
        write(root / "tools" / "perf" / "results" / "client_env_before.log", "env\n")
        write(root / "tools" / "perf" / "results" / "tree_server.log", "tree server\n")
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        with csv_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=["server_log", "client_env_before_log"])
            writer.writeheader()
            writer.writerow(
                {
                    "server_log": "tools/perf/results/server.log",
                    "client_env_before_log": "tools/perf/results/client_env_before.log",
                }
            )
        with tree_csv_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=["server_log", "result"])
            writer.writeheader()
            writer.writerow({"server_log": "tools/perf/results/tree_server.log", "result": "pass"})
        with tree_summary_path.open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=["dataset", "fail_count"])
            writer.writeheader()
            writer.writerow({"dataset": "mixed", "fail_count": "0"})
        gate_json = root / "tools" / "perf" / "results" / "gate.json"
        write(gate_json, "{}\n")
        paths = run_alpha_release_gate.collect_alpha_artifact_paths(
            root=root,
            gate_json=gate_json,
            matrix_raw=str(csv_path),
            matrix_summary="",
        )
        if "AGENTS.md" in paths or any(path.startswith("build") for path in paths):
            raise AssertionError(f"private/build path leaked into artifact paths: {paths}")
        for expected in {
            "INDEX.md",
            "docs/ROADMAP.md",
            "docs/PROJECT_STATE.md",
            "docs/perf/README.md",
            "docs/perf/PHASE5B_TREE_DATASET_MATRIX.md",
            "docs/release/ALPHA_RELEASE_GATE.md",
            "tools/release/helper.py",
            "tools/perf/run_gridftp_tree_private_matrix.py",
            "tools/perf/analyze_phase5b.py",
            "tools/perf/results/matrix.csv",
            "tools/perf/results/server.log",
            "tools/perf/results/client_env_before.log",
            "tools/perf/results/20260518T000000Z_gridftp-tree-private-matrix.csv",
            "tools/perf/results/20260518T000000Z_gridftp-tree-private-matrix-summary.csv",
            "tools/perf/results/tree_server.log",
        }:
            if expected not in paths:
                raise AssertionError(f"missing expected artifact path {expected}: {paths}")


def test_remote_artifact_sync_local_verify_and_sync() -> None:
    with tempfile.TemporaryDirectory(prefix="gridflux-alpha-sync.") as temp:
        base = Path(temp)
        local = base / "machine1"
        remote = base / "machine2"
        write(local / "docs" / "release" / "ALPHA_RELEASE_GATE.md", "gate-v1\n")
        write(local / "tools" / "perf" / "results" / "matrix.csv", "result,server_log\npass,tools/perf/results/server.log\n")
        write(local / "tools" / "perf" / "results" / "server.log", "server-v1\n")
        write(local / "tools" / "perf" / "results" / "events.jsonl", "{\"event\":\"ok\"}\n")
        manifest_path = local / "tools" / "perf" / "results" / "alpha-artifacts.json"
        manifest = {
            "timestamp": "2026-05-18T00:00:00Z",
            "source_tree_hash": "test",
            "remote_required": True,
            "artifacts": [
                sync_remote_artifacts.manifest_entry_for(
                    local, "docs/release/ALPHA_RELEASE_GATE.md", required=True
                ).__dict__,
                sync_remote_artifacts.manifest_entry_for(
                    local, "tools/perf/results/matrix.csv", required=True
                ).__dict__,
                sync_remote_artifacts.manifest_entry_for(
                    local, "tools/perf/results/server.log", required=True
                ).__dict__,
                sync_remote_artifacts.manifest_entry_for(
                    local, "tools/perf/results/events.jsonl", required=True
                ).__dict__,
            ],
        }
        if manifest["artifacts"][-1]["type"] != "event_log":
            raise AssertionError(f"jsonl artifact was not classified as event log: {manifest['artifacts'][-1]}")
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

        verify = sync_remote_artifacts.sync_from_manifest(
            manifest_path=manifest_path,
            remote=None,
            local_root=local,
            remote_root=str(remote),
            remote_local_root=remote,
            mode="verify-only",
        )
        if verify["missing"] == 0 or verify["status"] != "fail":
            raise AssertionError(f"verify-only did not detect missing artifacts: {verify}")

        synced = sync_remote_artifacts.sync_from_manifest(
            manifest_path=manifest_path,
            remote=None,
            local_root=local,
            remote_root=str(remote),
            remote_local_root=remote,
            mode="sync",
        )
        if synced["status"] != "pass" or synced["synced"] == 0:
            raise AssertionError(f"sync did not repair remote artifacts: {synced}")

        (remote / "tools" / "perf" / "results" / "server.log").write_text("changed\n", encoding="utf-8")
        mismatch = sync_remote_artifacts.sync_from_manifest(
            manifest_path=manifest_path,
            remote=None,
            local_root=local,
            remote_root=str(remote),
            remote_local_root=remote,
            mode="verify-only",
        )
        if mismatch["mismatch"] == 0:
            raise AssertionError(f"verify-only did not detect mismatch: {mismatch}")


def test_artifact_path_rejects_traversal_and_sensitive_paths() -> None:
    bad_paths = [
        "../escape.md",
        "/tmp/absolute.md",
        "AGENTS.md",
        "build/private.log",
        "build-private/file.log",
        "docs/password.txt",
        "secret/token.txt",
    ]
    for path in bad_paths:
        try:
            sync_remote_artifacts.validate_artifact_path(path)
        except ValueError:
            continue
        raise AssertionError(f"unsafe path was accepted: {path}")


def test_artifact_manifest_freshness_detects_stale_required_doc() -> None:
    with tempfile.TemporaryDirectory(prefix="gridflux-alpha-freshness.") as temp:
        root = Path(temp)
        write(root / "docs" / "PROJECT_STATE.md", "state-v1\n")
        manifest_path = root / "tools" / "perf" / "results" / "alpha-artifacts.json"
        run_alpha_release_gate.write_alpha_artifact_manifest(
            path=manifest_path,
            root=root,
            source_hash="test",
            remote_required=False,
            artifact_paths=["docs/PROJECT_STATE.md"],
        )
        fresh = run_alpha_release_gate.check_alpha_artifact_manifest_freshness(manifest_path, root)
        if fresh["status"] != "pass":
            raise AssertionError(f"fresh manifest unexpectedly failed: {fresh}")
        write(root / "docs" / "PROJECT_STATE.md", "state-v2\n")
        stale = run_alpha_release_gate.check_alpha_artifact_manifest_freshness(manifest_path, root)
        if stale["status"] != "fail" or stale["stale_count"] != 1:
            raise AssertionError(f"stale manifest was not detected: {stale}")


def test_remote_auth_reads_matching_private_agents_row() -> None:
    with tempfile.TemporaryDirectory(prefix="gridflux-remote-auth.") as temp:
        root = Path(temp)
        write(
            root / "AGENTS.md",
            "\n".join(
                [
                    "|<redacted>|公网 IP|私网 IP|用户|密码|",
                    "|---|---|---|---|---|",
                    "|<redacted>一|203.0.113.1|192.0.2.1|root|first-secret|",
                    "|<redacted>二|203.0.113.2|192.0.2.2|root|second-secret|",
                    "",
                ]
            ),
        )
        for key in ["GRIDFLUX_SSH_PASSWORD", "SSHPASS"]:
            os.environ.pop(key, None)
        auth = remote_auth.resolve_auth("root@192.0.2.2", root)
        if auth is None or auth.password != "second-secret" or auth.source != "AGENTS.md":
            raise AssertionError(f"unexpected remote auth resolution: {auth}")
        if remote_auth.resolve_auth("admin@192.0.2.2", root) is not None:
            raise AssertionError("remote auth ignored username mismatch")


def test_release_candidate_helpers_summarize_defaults_and_gate_paths() -> None:
    defaults = run_alpha_release_candidate.default_strategy_summary()
    expected = {
        "auth_mode": "anonymous",
        "tls_mode": "off",
        "data_tls_mode": "off",
        "file_io_backend": "posix",
        "final_verify_policy": "full",
        "manifest_flush_policy": "every_n_chunks",
        "preallocate": "off",
        "posix_write_strategy": "auto",
    }
    if defaults != expected:
        raise AssertionError(f"unexpected default strategy summary: {defaults}")

    paths = run_alpha_release_candidate.parse_gate_paths_from_log(
        "\n".join(
            [
                "alpha_release_gate_report=/tmp/gate.md",
                "alpha_release_gate_json=/tmp/gate.json",
                "alpha_artifact_manifest=/tmp/artifacts.json",
                "result=pass",
            ]
        )
    )
    if paths.get("alpha_release_gate_json") != "/tmp/gate.json":
        raise AssertionError(f"failed to parse nested gate paths: {paths}")

    steps = [
        run_alpha_release_gate.StepResult("one", "pass", 0, "one.log", 0.1),
        run_alpha_release_gate.StepResult("two", "fail", 1, "two.log", 0.2, "config_error"),
    ]
    summary = run_alpha_release_candidate.summarize_steps(steps)
    if summary["passed"] or summary["failed_steps"] != 1:
        raise AssertionError(f"unexpected step summary: {summary}")


def main() -> int:
    test_source_tree_hash_excludes_private_and_build()
    test_csv_sidecar_extraction()
    test_matrix_summary_default_baseline()
    test_artifact_manifest_excludes_private_paths_and_includes_sidecars()
    test_remote_artifact_sync_local_verify_and_sync()
    test_artifact_path_rejects_traversal_and_sensitive_paths()
    test_artifact_manifest_freshness_detects_stale_required_doc()
    test_remote_auth_reads_matching_private_agents_row()
    test_release_candidate_helpers_summarize_defaults_and_gate_paths()
    print("alpha release helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
