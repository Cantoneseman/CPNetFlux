#!/usr/bin/env python3
"""Run the CPNetFlux alpha release gate by orchestrating existing checks."""

from __future__ import annotations

import argparse
import csv
import fcntl
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from pathlib import Path

import sync_remote_artifacts
import remote_auth


EXCLUDED_HASH_PARTS = {
    ".git",
    "__pycache__",
    ".pytest_cache",
    "tools/perf/results",
}
EXCLUDED_HASH_NAMES = {
    "AGENTS.md",
    "CMakeCache.txt",
    "CTestTestfile.cmake",
    "build.ninja",
    "cmake_install.cmake",
    "compile_commands.json",
}


@dataclass
class StepResult:
    name: str
    status: str
    returncode: int
    log: str
    elapsed_seconds: float
    error_code: str = "ok"


def repo_root() -> Path:
    return Path(__file__).resolve().parents[2]


def compact_timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def timestamp_utc() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def acquire_gate_lock(results_dir: Path):
    lock_path = results_dir / ".alpha-release-gate.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    handle = lock_path.open("w", encoding="utf-8")
    try:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError as exc:
        handle.close()
        raise SystemExit(f"another alpha release gate is already running: {lock_path}") from exc
    handle.write(f"pid={os.getpid()} timestamp={timestamp_utc()}\n")
    handle.flush()
    return handle


def is_build_like(part: str) -> bool:
    return part == "build" or part.startswith("build-") or part.startswith("cmake-build-")


def should_hash_file(path: Path, root: Path) -> bool:
    relative = path.relative_to(root)
    joined = "/".join(relative.parts)
    if any(part in EXCLUDED_HASH_PARTS or is_build_like(part) for part in relative.parts):
        return False
    if joined.startswith("tools/perf/results/"):
        return False
    if path.name in EXCLUDED_HASH_NAMES:
        return False
    if path.suffix in {".pyc", ".o", ".a", ".so", ".log", ".tmp", ".bin"}:
        return False
    return path.is_file()


def source_tree_hash(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        if not should_hash_file(path, root):
            continue
        relative = path.relative_to(root).as_posix()
        digest.update(relative.encode("utf-8") + b"\0")
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(block)
    return digest.hexdigest()


def git_status(root: Path) -> dict[str, str]:
    def run_git(args: list[str]) -> str:
        completed = subprocess.run(
            ["git", *args],
            cwd=root,
            text=True,
            capture_output=True,
            check=False,
        )
        return completed.stdout.strip() if completed.returncode == 0 else ""

    return {
        "head": run_git(["rev-parse", "HEAD"]),
        "status_short": run_git(["status", "--short"]),
    }


def run_step(name: str, command: list[str], log_dir: Path, *, cwd: Path) -> StepResult:
    log_path = log_dir / f"{name}.log"
    start = time.monotonic()
    completed = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
    elapsed = time.monotonic() - start
    log_path.write_text(
        "$ " + " ".join(command) + "\n\n" + completed.stdout + completed.stderr,
        encoding="utf-8",
    )
    error_code = "ok" if completed.returncode == 0 else classify_error_text(completed.stdout + completed.stderr)
    return StepResult(
        name=name,
        status="pass" if completed.returncode == 0 else "fail",
        returncode=completed.returncode,
        log=str(log_path),
        elapsed_seconds=elapsed,
        error_code=error_code,
    )


def classify_error_text(text: str) -> str:
    lowered = text.lower()
    if not lowered:
        return "unknown_error"
    if "please login" in lowered or "auth required" in lowered:
        return "auth_required"
    if "login incorrect" in lowered or "pass rejected" in lowered or "auth failed" in lowered:
        return "auth_failed"
    if "changed" in lowered:
        return "changed_file"
    if "manifest" in lowered and any(word in lowered for word in ["corrupt", "checksum", "invalid", "stale"]):
        return "manifest_corrupt"
    if "checksum" in lowered and ("mismatch" in lowered or "failed" in lowered):
        return "checksum_mismatch"
    if "data tls" in lowered and "required" in lowered:
        return "data_tls_required"
    if "data tls" in lowered:
        return "data_tls_failed"
    if "tls required" in lowered:
        return "tls_required"
    if any(word in lowered for word in ["tls", "ssl", "certificate", "private key"]):
        return "tls_failed"
    if "remote" in lowered and ("sync" in lowered or "artifact" in lowered):
        return "remote_sync_failed"
    if "path" in lowered or "escape" in lowered or "outside" in lowered:
        return "path_rejected"
    if "protocol" in lowered or "frame" in lowered or "unexpected" in lowered:
        return "protocol_error"
    if any(word in lowered for word in ["open", "read", "write", "stat", "connect", "socket", "recv", "send"]):
        return "io_error"
    if "unknown option" in lowered or "requires a value" in lowered or "invalid" in lowered:
        return "config_error"
    return "unknown_error"


def ctest_counts(log_path: Path) -> dict[str, int | str]:
    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    match = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", text)
    if match:
        return {
            "passed_percent": int(match.group(1)),
            "failed": int(match.group(2)),
            "total": int(match.group(3)),
        }
    match = re.search(r"Total Test time.*\n\n(?:.*\n)*?(\d+)% tests passed", text)
    return {"summary": match.group(0) if match else ""}


def parse_matrix_summary(path: Path) -> dict[str, str | int | list[dict[str, str]]]:
    if not path.exists():
        return {"status": "missing", "rows": 0, "fail_count": -1, "default_baseline": []}
    rows: list[dict[str, str]]
    with path.open("r", encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    fail_count = 0
    for row in rows:
        try:
            fail_count += int(row.get("fail_count", "0") or "0")
        except ValueError:
            fail_count += 1
    default_rows = [
        row
        for row in rows
        if row.get("file_io_backend") == "posix"
        and row.get("file_io_buffer_size") == "0"
        and row.get("posix_write_strategy") == "auto"
        and row.get("manifest_flush_policy") == "every_n_chunks"
        and row.get("manifest_flush_interval_chunks") == "16"
        and row.get("commit_sync_policy") == "none"
        and row.get("final_verify_policy") == "full"
        and row.get("checksum_algorithm") == "crc32c"
    ]
    return {
        "status": "pass" if fail_count == 0 and rows else "fail",
        "rows": len(rows),
        "fail_count": fail_count,
        "default_baseline": default_rows,
    }


def find_latest_matrix_csvs(results_dir: Path, before: set[Path]) -> tuple[str, str]:
    after = set(results_dir.glob("*_gridftp-private-matrix-*.csv"))
    new_files = sorted(after - before, key=lambda path: path.stat().st_mtime)
    raw = [path for path in new_files if not path.name.endswith("-summary.csv")]
    summary = [path for path in new_files if path.name.endswith("-summary.csv")]
    return (str(raw[-1]) if raw else "", str(summary[-1]) if summary else "")


def ssh_prefix(remote: str) -> list[str]:
    return remote_auth.ssh_prefix(remote)


def run_remote_process_check(remote: str | None) -> dict[str, str]:
    pattern = "'[g]ridflux-gridftp-server|[g]ridflux-file-'"
    local = subprocess.run(
        ["bash", "-lc", f"pgrep -af {pattern} || true"],
        text=True,
        capture_output=True,
        check=False,
    ).stdout.strip()
    remote_text = ""
    if remote:
        remote_text = subprocess.run(
            ssh_prefix(remote) + [f"pgrep -af {pattern} || true"],
            text=True,
            capture_output=True,
            check=False,
            env=remote_auth.command_env(remote),
        ).stdout.strip()
    return {"local": local, "remote": remote_text}


def sync_artifacts(remote: str, remote_root: str, root: Path, artifacts: list[str]) -> StepResult:
    log_dir = root / "tools" / "perf" / "results"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / f"{compact_timestamp()}_alpha-artifact-rsync.log"
    existing = [artifact for artifact in artifacts if (root / artifact).exists()]
    if not existing:
        log_path.write_text("no artifacts to sync\n", encoding="utf-8")
        return StepResult("artifact_rsync", "pass", 0, str(log_path), 0.0)
    command = [
        "rsync",
        "-az",
        "-e",
        "ssh -o StrictHostKeyChecking=no",
        "--relative",
        *existing,
        f"{remote}:{remote_root.rstrip('/')}/",
    ]
    start = time.monotonic()
    command, env = remote_auth.wrap_with_sshpass(remote, command, root=root)
    completed = subprocess.run(
        command,
        cwd=root,
        text=True,
        capture_output=True,
        check=False,
        env=env,
    )
    elapsed = time.monotonic() - start
    log_path.write_text(
        "$ " + " ".join(command) + "\n\n" + completed.stdout + completed.stderr,
        encoding="utf-8",
    )
    return StepResult(
        "artifact_rsync",
        "pass" if completed.returncode == 0 else "fail",
        completed.returncode,
        str(log_path),
        elapsed,
    )


def normalize_artifact_path(value: str, root: Path) -> str | None:
    if not value:
        return None
    parsed = Path(value)
    if parsed.is_absolute():
        try:
            return parsed.resolve().relative_to(root).as_posix()
        except ValueError:
            return None
    if ".." in parsed.parts:
        return None
    return parsed.as_posix()


def csv_referenced_artifacts(csv_text: str, root: Path) -> list[str]:
    normalized = normalize_artifact_path(csv_text, root)
    if not normalized:
        return []
    path = root / normalized
    result = {normalized}
    if not path.exists():
        return sorted(result)
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            for field, value in row.items():
                if field.endswith("_log") or field in {
                    "server_log",
                    "client_log",
                    "server_env_before_log",
                    "server_env_after_log",
                    "client_env_before_log",
                    "client_env_after_log",
                    "json_summary",
                    "scheduler_summary_csv",
                    "scheduler_events_jsonl",
                    "scheduler_samples_csv",
                    "auth_preflight_audit",
                    "process_audit",
                }:
                    referenced = normalize_artifact_path(value.strip(), root)
                    if referenced:
                        result.add(referenced)
    return sorted(result)


def perf_doc_paths(root: Path) -> list[str]:
    perf_dir = root / "docs" / "perf"
    if not perf_dir.is_dir():
        return []
    return sorted(
        path.relative_to(root).as_posix()
        for path in perf_dir.glob("PHASE*.md")
        if path.is_file()
    )


def release_doc_paths(root: Path) -> list[str]:
    return sorted(path.relative_to(root).as_posix() for path in (root / "docs" / "release").glob("*.md"))


def release_tool_paths(root: Path) -> list[str]:
    return sorted(path.relative_to(root).as_posix() for path in (root / "tools" / "release").glob("*.py"))


def tree_test_tool_paths(root: Path) -> list[str]:
    return sorted(
        path.relative_to(root).as_posix()
        for path in (root / "tools" / "test").glob("*tree*.py")
    )


def security_test_tool_paths(root: Path) -> list[str]:
    result: list[str] = []
    for pattern in ("*tls*.py", "*token*.py", "*event_log*.py", "*soak*.py"):
        for path in (root / "tools" / "test").glob(pattern):
            if path.is_file():
                result.append(path.relative_to(root).as_posix())
    return sorted(set(result))


def perf_tool_paths(root: Path) -> list[str]:
    result: list[str] = []
    perf_dir = root / "tools" / "perf"
    if perf_dir.is_dir():
        for pattern in (
            "run_gridftp_private_matrix.py",
            "run_gridftp_tree_private_matrix.py",
            "run_private_host_baseline.py",
            "analyze_phase*.py",
        ):
            for path in perf_dir.glob(pattern):
                if path.is_file():
                    result.append(path.relative_to(root).as_posix())
    benchmark = root / "tools" / "benchmark" / "run_storage_bench.py"
    if benchmark.is_file():
        result.append(benchmark.relative_to(root).as_posix())
    return sorted(result)


def demo_tool_paths(root: Path) -> list[str]:
    demo_dir = root / "tools" / "demo"
    if not demo_dir.is_dir():
        return []
    return sorted(path.relative_to(root).as_posix() for path in demo_dir.glob("*.py"))


def latest_demo_artifacts(root: Path) -> list[str]:
    results_dir = root / "tools" / "perf" / "results"
    if not results_dir.is_dir():
        return []
    paths: set[str] = set()

    def is_demo_result(path: Path) -> bool:
        relative = path.relative_to(results_dir).as_posix()
        if "/dataset/" in relative or "/work/" in relative:
            return False
        if ".cpnetflux." in relative or ".part." in relative:
            return False
        return path.suffix in {".json", ".log", ".csv", ".txt", ".md"}

    for path in results_dir.glob("*_alpha-demo-*.json"):
        if path.is_file():
            paths.add(path.relative_to(root).as_posix())
    for path in results_dir.glob("*_alpha-release-gate/alpha_demo*"):
        if path.is_file() and is_demo_result(path):
            paths.add(path.relative_to(root).as_posix())
    for directory in results_dir.glob("*_alpha-demo-*"):
        if not directory.is_dir():
            continue
        for path in directory.rglob("*"):
            if path.is_file() and is_demo_result(path):
                paths.add(path.relative_to(root).as_posix())
    return sorted(paths)


def latest_by_mtime(paths: list[Path]) -> Path | None:
    existing = [path for path in paths if path.is_file()]
    if not existing:
        return None
    return max(existing, key=lambda path: path.stat().st_mtime)


def latest_tree_matrix_artifacts(root: Path) -> list[str]:
    results_dir = root / "tools" / "perf" / "results"
    raw = latest_by_mtime(sorted(results_dir.glob("*_gridftp-tree-private-matrix.csv")))
    summary = latest_by_mtime(sorted(results_dir.glob("*_gridftp-tree-private-matrix-summary.csv")))
    result: set[str] = set()
    for path in [raw, summary]:
        if path is None:
            continue
        result.update(csv_referenced_artifacts(path.relative_to(root).as_posix(), root))
    return sorted(result)


def latest_storage_bench_artifacts(root: Path) -> list[str]:
    results_dir = root / "tools" / "perf" / "results"
    raw = latest_by_mtime(sorted(results_dir.rglob("*_storage-bench.csv")))
    summary = latest_by_mtime(sorted(results_dir.rglob("*_storage-bench-summary.csv")))
    result: set[str] = set()
    for path in [raw, summary]:
        if path is None:
            continue
        result.update(csv_referenced_artifacts(path.relative_to(root).as_posix(), root))
    return sorted(result)


def collect_alpha_artifact_paths(
    *,
    root: Path,
    gate_json: Path,
    matrix_raw: str,
    matrix_summary: str,
) -> list[str]:
    paths = {
        "INDEX.md",
        "docs/ROADMAP.md",
        "docs/PROJECT_STATE.md",
        "docs/DESIGN.md",
        "docs/ARCHITECTURE_ALPHA.md",
        "docs/ENGINEERING.md",
        "docs/DIRECTORY_TRANSFER.md",
        "docs/DEMO.md",
        "docs/SECURITY.md",
        "docs/OBSERVABILITY.md",
        "docs/perf/README.md",
        *perf_doc_paths(root),
        "docs/perf/PHASE5B_TREE_DATASET_MATRIX.md",
        "docs/perf/PHASE5C_TREE_ALPHA_HARDENING.md",
        "docs/release/PHASE5D_ALPHA_DEMO.md",
        relative_to_root(str(gate_json), root),
        *release_doc_paths(root),
        *release_tool_paths(root),
        *tree_test_tool_paths(root),
        *security_test_tool_paths(root),
        *perf_tool_paths(root),
        *demo_tool_paths(root),
        *latest_demo_artifacts(root),
        *latest_tree_matrix_artifacts(root),
        *latest_storage_bench_artifacts(root),
    }
    if matrix_raw:
        paths.update(csv_referenced_artifacts(relative_to_root(matrix_raw, root), root))
    if matrix_summary:
        paths.update(csv_referenced_artifacts(relative_to_root(matrix_summary, root), root))

    result: list[str] = []
    for path in sorted(path for path in paths if path):
        try:
            normalized = sync_remote_artifacts.validate_artifact_path(path)
            if (root / normalized).is_file():
                result.append(normalized)
        except ValueError:
            continue
    return result


def write_alpha_artifact_manifest(
    *,
    path: Path,
    root: Path,
    source_hash: str,
    remote_required: bool,
    artifact_paths: list[str],
) -> dict[str, object]:
    artifacts = [
        asdict(sync_remote_artifacts.manifest_entry_for(root, artifact_path, required=True))
        for artifact_path in artifact_paths
    ]
    manifest = {
        "timestamp": timestamp_utc(),
        "source_tree_hash": source_hash,
        "remote_required": remote_required,
        "artifacts": artifacts,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return manifest


def check_alpha_artifact_manifest_freshness(manifest_path: Path, root: Path) -> dict[str, object]:
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    stale: list[dict[str, object]] = []
    for item in data.get("artifacts", []):
        if not isinstance(item, dict):
            continue
        relative = str(item.get("path", ""))
        try:
            normalized = sync_remote_artifacts.validate_artifact_path(relative)
        except ValueError as exc:
            stale.append({"path": relative, "reason": str(exc)})
            continue
        path = root / normalized
        if not path.is_file():
            stale.append({"path": normalized, "reason": "missing"})
            continue
        size = path.stat().st_size
        digest = sync_remote_artifacts.sha256_file(path)
        if size != int(item.get("size", -1)) or digest != str(item.get("sha256", "")):
            stale.append(
                {
                    "path": normalized,
                    "reason": "stale_hash",
                    "manifest_size": item.get("size"),
                    "current_size": size,
                    "manifest_sha256": item.get("sha256"),
                    "current_sha256": digest,
                }
            )
    return {
        "path": relative_to_root(str(manifest_path), root),
        "checked": len(data.get("artifacts", [])),
        "stale_count": len(stale),
        "status": "pass" if not stale else "fail",
        "stale": stale,
    }


def read_json_if_exists(path: Path) -> dict[str, object]:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else {}
    except json.JSONDecodeError:
        return {}


def update_report_status(
    report: dict[str, object],
    steps: list[StepResult],
    residual: dict[str, str],
    private_matrix: dict[str, object],
) -> None:
    failures = [step.name for step in steps if step.status != "pass"]

    def add_failure(name: str) -> None:
        if name not in failures:
            failures.append(name)

    if residual.get("local") or residual.get("remote"):
        add_failure("residual_process_check")
    if private_matrix and private_matrix.get("status") == "fail":
        add_failure("private_matrix")

    artifact_sync_summary = report.get("artifact_sync_summary", {})
    artifact_verify_summary = report.get("artifact_verify_summary", {})
    artifact_freshness = report.get("artifact_manifest_freshness", {})
    if isinstance(artifact_sync_summary, dict) and artifact_sync_summary.get("status") not in {None, "pass"}:
        add_failure("artifact_sync_summary")
    if isinstance(artifact_verify_summary, dict) and artifact_verify_summary.get("status") not in {None, "pass"}:
        add_failure("artifact_verify_summary")
    if isinstance(artifact_freshness, dict) and artifact_freshness.get("status") not in {None, "pass"}:
        add_failure("artifact_manifest_freshness")

    failed_steps = [asdict(step) for step in steps if step.status != "pass"]
    report["steps"] = [asdict(step) for step in steps]
    report["failures"] = failures
    report["total_steps"] = len(steps)
    report["passed_steps"] = sum(1 for step in steps if step.status == "pass")
    report["failed_steps"] = sum(1 for step in steps if step.status != "pass")
    report["first_failed_step"] = failed_steps[0] if failed_steps else {}
    report["passed"] = not failures


def persist_report(report_path: Path, json_path: Path, report: dict[str, object]) -> None:
    write_markdown_report(report_path, report)
    json_report = dict(report)
    json_report.pop("artifact_manifest_freshness", None)
    json_path.write_text(json.dumps(json_report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def refresh_alpha_artifact_manifest(
    *,
    report: dict[str, object],
    root: Path,
    artifact_manifest_path: Path,
    source_hash: str,
    remote_required: bool,
    gate_json: Path,
    matrix_raw: str,
    matrix_summary: str,
) -> None:
    artifact_paths = collect_alpha_artifact_paths(
        root=root,
        gate_json=gate_json,
        matrix_raw=matrix_raw,
        matrix_summary=matrix_summary,
    )
    manifest_data = write_alpha_artifact_manifest(
        path=artifact_manifest_path,
        root=root,
        source_hash=source_hash,
        remote_required=remote_required,
        artifact_paths=artifact_paths,
    )
    report["artifact_manifest"] = {
        "path": relative_to_root(str(artifact_manifest_path), root),
        "artifact_count": len(manifest_data.get("artifacts", [])),
        "remote_required": remote_required,
    }
    report["artifact_manifest_freshness"] = check_alpha_artifact_manifest_freshness(
        artifact_manifest_path, root
    )


def write_markdown_report(path: Path, report: dict[str, object]) -> None:
    steps = report["steps"]  # type: ignore[index]
    assert isinstance(steps, list)
    private_matrix = report.get("private_matrix", {})
    residual = report.get("residual_process_check", {})
    lines = [
        "# CPNetFlux Alpha Release Gate",
        "",
        f"- Timestamp: `{report['timestamp']}`",
        f"- Mode: `{report['mode']}`",
        f"- Source tree hash: `{report['source_tree_hash']}`",
        f"- Result: `{'pass' if report['passed'] else 'fail'}`",
        f"- Total steps: `{report.get('total_steps', len(steps))}`",
        f"- Passed steps: `{report.get('passed_steps', '')}`",
        f"- Failed steps: `{report.get('failed_steps', '')}`",
        "",
        "## Step Results",
        "",
        "| Step | Status | Error Code | Seconds | Log |",
        "|------|--------|------------|---------|-----|",
    ]
    for step in steps:
        lines.append(
            f"| `{step['name']}` | `{step['status']}` | `{step.get('error_code', 'ok')}` | `{step['elapsed_seconds']:.2f}` | `{step['log']}` |"
        )
    first_failure = report.get("first_failed_step")
    if isinstance(first_failure, dict) and first_failure:
        lines.extend(
            [
                "",
                "First failed step:",
                "",
                f"- Name: `{first_failure.get('name', '')}`",
                f"- Error code: `{first_failure.get('error_code', '')}`",
                f"- Log: `{first_failure.get('log', '')}`",
            ]
        )
    lines.extend(["", "## Private Baseline", ""])
    if isinstance(private_matrix, dict) and private_matrix:
        lines.extend(
            [
                f"- Raw CSV: `{private_matrix.get('raw_csv', '')}`",
                f"- Summary CSV: `{private_matrix.get('summary_csv', '')}`",
                f"- Fail count: `{private_matrix.get('fail_count', '')}`",
                f"- Summary rows: `{private_matrix.get('rows', '')}`",
            ]
        )
        default_rows = private_matrix.get("default_baseline", [])
        if isinstance(default_rows, list) and default_rows:
            lines.extend(["", "Default baseline rows:", ""])
            for row in default_rows:
                if isinstance(row, dict):
                    lines.append(
                        "- "
                        f"direction={row.get('direction')} "
                        f"throughput_median={row.get('throughput_gbps_median')} "
                        f"spread_pct={row.get('throughput_gbps_spread_pct')}"
                    )
    else:
        lines.append("- Not run in quick mode.")
    artifact_manifest = report.get("artifact_manifest", {})
    artifact_sync = report.get("artifact_sync_summary", {})
    artifact_verify = report.get("artifact_verify_summary", {})
    lines.extend(["", "## Artifact Sync", ""])
    if isinstance(artifact_manifest, dict) and artifact_manifest:
        lines.extend(
            [
                f"- Manifest: `{artifact_manifest.get('path', '')}`",
                f"- Artifacts: `{artifact_manifest.get('artifact_count', '')}`",
            ]
        )
    else:
        lines.append("- Manifest: not generated.")
    if isinstance(artifact_sync, dict) and artifact_sync:
        lines.append(
            "- Sync: "
            f"checked=`{artifact_sync.get('checked', '')}` "
            f"synced=`{artifact_sync.get('synced', '')}` "
            f"missing=`{artifact_sync.get('missing', '')}` "
            f"mismatch=`{artifact_sync.get('mismatch', '')}` "
            f"pre_missing=`{artifact_sync.get('pre_sync_missing', '')}` "
            f"pre_mismatch=`{artifact_sync.get('pre_sync_mismatch', '')}` "
            f"post_missing=`{artifact_sync.get('post_sync_missing', '')}` "
            f"post_mismatch=`{artifact_sync.get('post_sync_mismatch', '')}` "
            f"status=`{artifact_sync.get('status', '')}`"
        )
    if isinstance(artifact_verify, dict) and artifact_verify:
        lines.append(
            "- Verify: "
            f"checked=`{artifact_verify.get('checked', artifact_verify.get('total', ''))}` "
            f"missing=`{artifact_verify.get('missing', '')}` "
            f"mismatch=`{artifact_verify.get('mismatch', '')}` "
            f"status=`{artifact_verify.get('status', '')}`"
        )
    lines.extend(
        [
            "",
            "## Alpha Readiness",
            "",
            "- Alpha scope is demonstrable GridFTP-like framed STOR/RETR, bidirectional resume, CRC32C chunk verification, and control metadata commands.",
            "- Not beta/production: performance spread remains significant, 100G dedicated-line validation is not complete, and TLS/GSI/raw FTP stream/directory sync are out of scope.",
            "- Defaults remain POSIX backend, full final verify, every_n_chunks manifest flush, no commit fsync, no preallocate full, and no default io_uring.",
            "",
            "## Residual Process Check",
            "",
            f"- Local: `{residual.get('local', '') if isinstance(residual, dict) else ''}`",
            f"- Remote: `{residual.get('remote', '') if isinstance(residual, dict) else ''}`",
            "",
            "## Failures",
            "",
        ]
    )
    failures = report.get("failures", [])
    if isinstance(failures, list) and failures:
        lines.extend(f"- {failure}" for failure in failures)
    else:
        lines.append("- None.")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def relative_to_root(path: str, root: Path) -> str:
    if not path:
        return ""
    parsed = Path(path)
    if parsed.is_absolute():
        try:
            return parsed.resolve().relative_to(root).as_posix()
        except ValueError:
            return path
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux alpha release gate.")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--quick", action="store_true")
    mode.add_argument("--full", action="store_true")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--io-uring-build-dir", default="build-io-uring-real")
    parser.add_argument("--remote")
    parser.add_argument("--remote-root", default="/root/projects/CPNetFlux")
    parser.add_argument("--server-host")
    parser.add_argument("--results-dir", default="tools/perf/results")
    args = parser.parse_args()

    if args.full and (not args.remote or not args.server_host):
        raise SystemExit("--full requires --remote and --server-host")

    root = repo_root()
    results_dir = root / args.results_dir
    results_dir.mkdir(parents=True, exist_ok=True)
    gate_lock = acquire_gate_lock(results_dir)
    timestamp = compact_timestamp()
    log_dir = results_dir / f"{timestamp}_alpha-release-gate"
    log_dir.mkdir(parents=True, exist_ok=True)
    report_path = root / "docs" / "release" / "ALPHA_RELEASE_GATE.md"
    json_path = results_dir / f"{timestamp}_alpha-release-gate.json"
    artifact_manifest_path = results_dir / f"{timestamp}_alpha-artifacts.json"

    steps: list[StepResult] = []
    matrix_raw = ""
    matrix_summary = ""
    private_matrix: dict[str, object] = {}

    command_specs = [
        ("build_debug", ["cmake", "--build", args.build_dir]),
        ("ctest_debug", ["ctest", "--test-dir", args.build_dir, "--output-on-failure"]),
        ("ctest_iouring", ["ctest", "--test-dir", args.io_uring_build_dir, "--output-on-failure"]),
        (
            "ctest_iouring_smoke",
            [
                "ctest",
                "--test-dir",
                args.io_uring_build_dir,
                "-R",
                "FileIoTest.IoUringContextReadWriteSmokeWhenAvailable",
                "--output-on-failure",
            ],
        ),
        (
            "public_export_hygiene",
            [
                sys.executable,
                "tools/release/export_public_repo.py",
                "--output",
                f"/tmp/cpnetflux-public-alpha-{timestamp}",
                "--force",
            ],
        ),
        ("stor_smoke", [sys.executable, "tools/test/run_gridftp_control_stor_smoke.py", "--build-dir", args.build_dir]),
        ("retr_smoke", [sys.executable, "tools/test/run_gridftp_control_retr_smoke.py", "--build-dir", args.build_dir]),
        ("stor_resume_smoke", [sys.executable, "tools/test/run_gridftp_control_resume_smoke.py", "--build-dir", args.build_dir]),
        ("retr_resume_smoke", [sys.executable, "tools/test/run_gridftp_control_retr_resume_smoke.py", "--build-dir", args.build_dir]),
        ("metadata_smoke", [sys.executable, "tools/test/run_gridftp_control_metadata_smoke.py", "--build-dir", args.build_dir]),
        ("list_smoke", [sys.executable, "tools/test/run_gridftp_control_list_smoke.py", "--build-dir", args.build_dir]),
        ("token_auth_smoke", [sys.executable, "tools/test/run_gridftp_control_token_smoke.py", "--build-dir", args.build_dir]),
        ("tls_control_smoke", [sys.executable, "tools/test/run_gridftp_control_tls_smoke.py", "--build-dir", args.build_dir]),
        ("data_tls_smoke", [sys.executable, "tools/test/run_gridftp_data_tls_smoke.py", "--build-dir", args.build_dir]),
        ("tree_upload_smoke", [sys.executable, "tools/test/run_gridftp_tree_upload_smoke.py", "--build-dir", args.build_dir]),
        ("tree_download_smoke", [sys.executable, "tools/test/run_gridftp_tree_download_smoke.py", "--build-dir", args.build_dir]),
        ("tree_resume_smoke", [sys.executable, "tools/test/run_gridftp_tree_resume_smoke.py", "--build-dir", args.build_dir]),
        ("tree_parallel_smoke", [sys.executable, "tools/test/run_gridftp_tree_parallel_smoke.py", "--build-dir", args.build_dir]),
        ("tree_changed_file_smoke", [sys.executable, "tools/test/run_gridftp_tree_changed_file_smoke.py", "--build-dir", args.build_dir]),
        ("tree_edge_cases_smoke", [sys.executable, "tools/test/run_gridftp_tree_edge_cases_smoke.py", "--build-dir", args.build_dir]),
        ("tree_manifest_corrupt_smoke", [sys.executable, "tools/test/run_gridftp_tree_manifest_corrupt_smoke.py", "--build-dir", args.build_dir]),
        (
            "alpha_demo_local",
            [
                sys.executable,
                "tools/demo/run_alpha_demo.py",
                "--mode",
                "local",
                "--build-dir",
                args.build_dir,
                "--profile",
                "tiny",
                "--results-dir",
                args.results_dir,
                "--json-output",
                str(log_dir / "alpha_demo_local.json"),
                "--event-log",
                str(log_dir / "alpha_demo_local_events.jsonl"),
            ],
        ),
    ]
    for name, command in command_specs:
        step = run_step(name, command, log_dir, cwd=root)
        steps.append(step)

    if args.full:
        tree_private_command = [
            sys.executable,
            "tools/test/run_gridftp_tree_private_once.py",
            "--remote",
            args.remote,
            "--server-host",
            args.server_host,
            "--local-build-dir",
            str((root / args.build_dir).resolve()),
            "--remote-build-dir",
            f"{args.remote_root.rstrip('/')}/{args.build_dir}",
            "--connections",
            "2",
            "--checksum",
            "crc32c",
            "--checksum-backend",
            "auto",
            "--output-dir",
            args.results_dir,
        ]
        tree_private_step = run_step("tree_private_smoke", tree_private_command, log_dir, cwd=root)
        steps.append(tree_private_step)

        alpha_private_command = [
            sys.executable,
            "tools/demo/run_alpha_demo.py",
            "--mode",
            "private",
            "--build-dir",
            args.build_dir,
            "--remote",
            args.remote,
            "--remote-root",
            args.remote_root,
            "--server-host",
            args.server_host,
            "--profile",
            "tiny",
            "--results-dir",
            args.results_dir,
            "--json-output",
            str(log_dir / "alpha_demo_private.json"),
        ]
        alpha_private_step = run_step("alpha_demo_private", alpha_private_command, log_dir, cwd=root)
        steps.append(alpha_private_step)

        private_token_command = [
            sys.executable,
            "tools/test/run_gridftp_control_token_private_once.py",
            "--remote",
            args.remote,
            "--server-host",
            args.server_host,
            "--local-build-dir",
            str((root / args.build_dir).resolve()),
            "--control-port",
            str(25000 + (os.getpid() % 1000)),
            "--data-port-base",
            str(26000 + (os.getpid() % 1000)),
            "--output-dir",
            args.results_dir,
        ]
        private_token_step = run_step(
            "private_token_auth_smoke", private_token_command, log_dir, cwd=root
        )
        steps.append(private_token_step)

        private_tls_command = [
            sys.executable,
            "tools/test/run_gridftp_control_tls_private_once.py",
            "--remote",
            args.remote,
            "--server-host",
            args.server_host,
            "--local-build-dir",
            str((root / args.build_dir).resolve()),
            "--control-port",
            str(27000 + (os.getpid() % 1000)),
            "--data-port-base",
            str(28000 + (os.getpid() % 1000)),
            "--output-dir",
            args.results_dir,
        ]
        private_tls_step = run_step("private_tls_control_smoke", private_tls_command, log_dir, cwd=root)
        steps.append(private_tls_step)

        private_data_tls_command = [
            sys.executable,
            "tools/test/run_gridftp_data_tls_private_once.py",
            "--remote",
            args.remote,
            "--server-host",
            args.server_host,
            "--local-build-dir",
            str((root / args.build_dir).resolve()),
            "--remote-build-dir",
            f"{args.remote_root.rstrip('/')}/{args.build_dir}",
            "--control-port",
            str(29000 + (os.getpid() % 1000)),
            "--data-port-base",
            str(30000 + (os.getpid() % 1000)),
            "--output-dir",
            args.results_dir,
            "--bytes",
            "1048576",
        ]
        private_data_tls_step = run_step(
            "private_data_tls_smoke", private_data_tls_command, log_dir, cwd=root
        )
        steps.append(private_data_tls_step)

        soak_step = run_step(
            "alpha_soak_smoke",
            [
                sys.executable,
                "tools/test/run_alpha_soak_smoke.py",
                "--build-dir",
                args.build_dir,
                "--iterations",
                "2",
                "--results-dir",
                str(log_dir / "alpha_soak"),
                "--json-output",
                str(log_dir / "alpha_soak.json"),
                "--event-log",
                str(log_dir / "alpha_soak_events.jsonl"),
            ],
            log_dir,
            cwd=root,
        )
        steps.append(soak_step)

        before = set(results_dir.glob("*_gridftp-private-matrix-*.csv"))
        matrix_command = [
            sys.executable,
            "tools/perf/run_gridftp_private_matrix.py",
            "--smoke",
            "--directions",
            "stor,retr",
            "--bytes",
            "1073741824",
            "--connections",
            "8",
            "--chunk-sizes",
            "4194304",
            "--buffer-sizes",
            "262144",
            "--checksums",
            "crc32c,none",
            "--checksum-backend",
            "auto",
            "--file-io-backends",
            "posix",
            "--file-io-buffer-sizes",
            "0",
            "--posix-write-strategies",
            "auto",
            "--file-io-advices",
            "off",
            "--preallocates",
            "off",
            "--manifest-flush-policies",
            "every_n_chunks",
            "--manifest-flush-interval-chunks-list",
            "16",
            "--commit-sync-policies",
            "none",
            "--final-verify-policies",
            "full,verified_chunks",
            "--repeat",
            "3",
            "--remote",
            args.remote,
            "--server-host",
            args.server_host,
            "--local-build-dir",
            str((root / args.io_uring_build_dir).resolve()),
            "--remote-build-dir",
            f"{args.remote_root.rstrip('/')}/{args.io_uring_build_dir}",
            "--output-dir",
            args.results_dir,
            "--case-timeout",
            "900",
        ]
        matrix_step = run_step("private_baseline_matrix", matrix_command, log_dir, cwd=root)
        steps.append(matrix_step)
        matrix_raw, matrix_summary = find_latest_matrix_csvs(results_dir, before)
        private_matrix = parse_matrix_summary(Path(matrix_summary))
        private_matrix["raw_csv"] = relative_to_root(matrix_raw, root)
        private_matrix["summary_csv"] = relative_to_root(matrix_summary, root)

    residual = run_remote_process_check(args.remote)

    source_hash = source_tree_hash(root)
    report: dict[str, object] = {
        "timestamp": timestamp_utc(),
        "mode": "full" if args.full else "quick",
        "source_tree_hash": source_hash,
        "git": git_status(root),
        "steps": [asdict(step) for step in steps],
        "ctest": {
            step.name: ctest_counts(Path(step.log))
            for step in steps
            if step.name.startswith("ctest_")
        },
        "private_matrix": private_matrix,
        "hygiene": next((asdict(step) for step in steps if step.name == "public_export_hygiene"), {}),
        "artifact_sync": {},
        "artifact_manifest": {},
        "artifact_sync_summary": {},
        "artifact_verify_summary": {},
        "residual_process_check": residual,
        "failures": [],
        "passed": False,
    }
    persist_report(report_path, json_path, report)

    if args.full:
        refresh_alpha_artifact_manifest(
            report=report,
            root=root,
            artifact_manifest_path=artifact_manifest_path,
            source_hash=source_hash,
            remote_required=bool(args.remote),
            gate_json=json_path,
            matrix_raw=matrix_raw,
            matrix_summary=matrix_summary,
        )
        persist_report(report_path, json_path, report)
        refresh_alpha_artifact_manifest(
            report=report,
            root=root,
            artifact_manifest_path=artifact_manifest_path,
            source_hash=source_hash,
            remote_required=bool(args.remote),
            gate_json=json_path,
            matrix_raw=matrix_raw,
            matrix_summary=matrix_summary,
        )

    if args.remote and args.full:
        sync_json = log_dir / "remote-artifact-sync.json"
        sync_step = run_step(
            "remote_artifact_sync",
            [
                sys.executable,
                "tools/release/sync_remote_artifacts.py",
                "--manifest",
                relative_to_root(str(artifact_manifest_path), root),
                "--remote",
                args.remote,
                "--local-root",
                str(root),
                "--remote-root",
                args.remote_root,
                "--sync",
                "--json-output",
                str(sync_json),
            ],
            log_dir,
            cwd=root,
        )
        steps.append(sync_step)
        report["artifact_sync_summary"] = read_json_if_exists(sync_json)

        sync_verify_json = log_dir / "remote-artifact-verify.json"
        sync_check = run_step(
            "remote_artifact_sync_check",
            [
                sys.executable,
                "tools/release/check_remote_artifact_sync.py",
                "--remote",
                args.remote,
                "--local-root",
                str(root),
                "--remote-root",
                args.remote_root,
                "--manifest",
                relative_to_root(str(artifact_manifest_path), root),
                "--json-output",
                str(sync_verify_json),
            ],
            log_dir,
            cwd=root,
        )
        steps.append(sync_check)
        report["artifact_sync"] = {
            "sync": asdict(sync_step),
            "check": asdict(sync_check),
            "json": str(sync_json),
            "verify_json": str(sync_verify_json),
        }
        report["artifact_verify_summary"] = read_json_if_exists(sync_verify_json)

    update_report_status(report, steps, residual, private_matrix)
    persist_report(report_path, json_path, report)

    if args.full:
        refresh_alpha_artifact_manifest(
            report=report,
            root=root,
            artifact_manifest_path=artifact_manifest_path,
            source_hash=source_hash,
            remote_required=bool(args.remote),
            gate_json=json_path,
            matrix_raw=matrix_raw,
            matrix_summary=matrix_summary,
        )
        if args.remote:
            final_sync_json = log_dir / "remote-artifact-final-sync.json"
            final_sync = run_step(
                "remote_artifact_final_sync",
                [
                    sys.executable,
                    "tools/release/sync_remote_artifacts.py",
                    "--manifest",
                    relative_to_root(str(artifact_manifest_path), root),
                    "--remote",
                    args.remote,
                    "--local-root",
                    str(root),
                    "--remote-root",
                    args.remote_root,
                    "--sync",
                    "--json-output",
                    str(final_sync_json),
                ],
                log_dir,
                cwd=root,
            )
            steps.append(final_sync)
            final_verify_json = log_dir / "remote-artifact-final-verify.json"
            final_verify = run_step(
                "remote_artifact_final_verify",
                [
                    sys.executable,
                    "tools/release/check_remote_artifact_sync.py",
                    "--remote",
                    args.remote,
                    "--local-root",
                    str(root),
                    "--remote-root",
                    args.remote_root,
                    "--manifest",
                    relative_to_root(str(artifact_manifest_path), root),
                    "--json-output",
                    str(final_verify_json),
                ],
                log_dir,
                cwd=root,
            )
            steps.append(final_verify)
            report["artifact_sync_summary"] = read_json_if_exists(final_sync_json)
            report["artifact_verify_summary"] = read_json_if_exists(final_verify_json)
            report["artifact_sync"] = {
                "sync": asdict(final_sync),
                "check": asdict(final_verify),
                "json": str(final_sync_json),
                "verify_json": str(final_verify_json),
            }
            update_report_status(report, steps, residual, private_matrix)
            persist_report(report_path, json_path, report)

            # The report itself is an artifact. Refresh the manifest after the
            # final report write, then perform a terminal sync without changing
            # the report again so the remote hashes match the published files.
            refresh_alpha_artifact_manifest(
                report=report,
                root=root,
                artifact_manifest_path=artifact_manifest_path,
                source_hash=source_hash,
                remote_required=True,
                gate_json=json_path,
                matrix_raw=matrix_raw,
                matrix_summary=matrix_summary,
            )
            post_sync_json = log_dir / "remote-artifact-post-report-sync.json"
            post_sync = run_step(
                "remote_artifact_post_report_sync",
                [
                    sys.executable,
                    "tools/release/sync_remote_artifacts.py",
                    "--manifest",
                    relative_to_root(str(artifact_manifest_path), root),
                    "--remote",
                    args.remote,
                    "--local-root",
                    str(root),
                    "--remote-root",
                    args.remote_root,
                    "--sync",
                    "--json-output",
                    str(post_sync_json),
                ],
                log_dir,
                cwd=root,
            )
            post_verify_json = log_dir / "remote-artifact-post-report-verify.json"
            post_verify = run_step(
                "remote_artifact_post_report_verify",
                [
                    sys.executable,
                    "tools/release/check_remote_artifact_sync.py",
                    "--remote",
                    args.remote,
                    "--local-root",
                    str(root),
                    "--remote-root",
                    args.remote_root,
                    "--manifest",
                    relative_to_root(str(artifact_manifest_path), root),
                    "--json-output",
                    str(post_verify_json),
                ],
                log_dir,
                cwd=root,
            )
            if post_sync.status != "pass" or post_verify.status != "pass":
                steps.extend([post_sync, post_verify])
                update_report_status(report, steps, residual, private_matrix)
                persist_report(report_path, json_path, report)
    print(f"alpha_release_gate_report={report_path}")
    print(f"alpha_release_gate_json={json_path}")
    if args.full:
        print(f"alpha_artifact_manifest={artifact_manifest_path}")
    print(f"result={'pass' if report['passed'] else 'fail'}")
    gate_lock.close()
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
