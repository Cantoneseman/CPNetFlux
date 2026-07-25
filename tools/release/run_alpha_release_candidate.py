#!/usr/bin/env python3
"""Run the complete GridFlux alpha release-candidate validation package."""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from dataclasses import asdict
from pathlib import Path

import remote_auth
import run_alpha_release_gate as gate


DEFAULT_STRATEGY = {
    "auth_mode": "anonymous",
    "tls_mode": "off",
    "data_tls_mode": "off",
    "file_io_backend": "posix",
    "final_verify_policy": "full",
    "manifest_flush_policy": "every_n_chunks",
    "preallocate": "off",
    "posix_write_strategy": "auto",
}


def default_strategy_summary() -> dict[str, str]:
    return dict(DEFAULT_STRATEGY)


def parse_gate_paths_from_log(log_text: str) -> dict[str, str]:
    result: dict[str, str] = {}
    for key in ["alpha_release_gate_report", "alpha_release_gate_json", "alpha_artifact_manifest"]:
        match = re.search(rf"^{key}=(.+)$", log_text, flags=re.MULTILINE)
        if match:
            result[key] = match.group(1).strip()
    return result


def read_json(path_text: str) -> dict[str, object]:
    if not path_text:
        return {}
    path = Path(path_text)
    if not path.exists():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        return payload if isinstance(payload, dict) else {}
    except json.JSONDecodeError:
        return {}


def summarize_steps(steps: list[gate.StepResult]) -> dict[str, object]:
    failures = [step for step in steps if step.status != "pass"]
    return {
        "total_steps": len(steps),
        "passed_steps": sum(1 for step in steps if step.status == "pass"),
        "failed_steps": len(failures),
        "first_failed_step": asdict(failures[0]) if failures else {},
        "passed": not failures,
    }


def write_candidate_markdown(path: Path, report: dict[str, object]) -> None:
    steps = report.get("steps", [])
    gate_report = report.get("alpha_release_gate", {})
    artifact_sync = report.get("artifact_sync_summary", {})
    artifact_verify = report.get("artifact_verify_summary", {})
    freshness = report.get("artifact_manifest_freshness", {})
    lines = [
        "# GridFlux Alpha Release Candidate",
        "",
        f"- Timestamp: `{report.get('timestamp', '')}`",
        f"- Result: `{'pass' if report.get('passed') else 'fail'}`",
        f"- Source tree hash: `{report.get('source_tree_hash', '')}`",
        "",
        "## Default Strategy",
        "",
    ]
    for key, value in sorted(default_strategy_summary().items()):
        lines.append(f"- `{key}`: `{value}`")
    lines.extend(
        [
            "",
            "## Step Results",
            "",
            "| Step | Status | Error Code | Seconds | Log |",
            "|------|--------|------------|---------|-----|",
        ]
    )
    if isinstance(steps, list):
        for step in steps:
            if isinstance(step, dict):
                lines.append(
                    f"| `{step.get('name', '')}` | `{step.get('status', '')}` | "
                    f"`{step.get('error_code', '')}` | `{float(step.get('elapsed_seconds', 0.0)):.2f}` | "
                    f"`{step.get('log', '')}` |"
                )
    lines.extend(["", "## Nested Full Gate", ""])
    if isinstance(gate_report, dict) and gate_report:
        lines.extend(
            [
                f"- Gate JSON: `{gate_report.get('json', '')}`",
                f"- Gate artifact manifest: `{gate_report.get('artifact_manifest', '')}`",
                f"- Gate passed: `{gate_report.get('passed', '')}`",
                f"- Gate total steps: `{gate_report.get('total_steps', '')}`",
                f"- Gate failed steps: `{gate_report.get('failed_steps', '')}`",
            ]
        )
    else:
        lines.append("- Not available.")
    lines.extend(["", "## Release Candidate Artifacts", ""])
    manifest = report.get("artifact_manifest", {})
    if isinstance(manifest, dict):
        lines.extend(
            [
                f"- Manifest: `{manifest.get('path', '')}`",
                f"- Artifact count: `{manifest.get('artifact_count', '')}`",
            ]
        )
    if isinstance(freshness, dict):
        lines.append(
            "- Freshness: "
            f"checked=`{freshness.get('checked', '')}` "
            f"stale=`{freshness.get('stale_count', '')}` "
            f"status=`{freshness.get('status', '')}`"
        )
    if isinstance(artifact_sync, dict) and artifact_sync:
        lines.append(
            "- Sync: "
            f"checked=`{artifact_sync.get('checked', '')}` "
            f"synced=`{artifact_sync.get('synced', '')}` "
            f"missing=`{artifact_sync.get('missing', '')}` "
            f"mismatch=`{artifact_sync.get('mismatch', '')}` "
            f"status=`{artifact_sync.get('status', '')}`"
        )
    if isinstance(artifact_verify, dict) and artifact_verify:
        lines.append(
            "- Verify: "
            f"checked=`{artifact_verify.get('checked', '')}` "
            f"missing=`{artifact_verify.get('missing', '')}` "
            f"mismatch=`{artifact_verify.get('mismatch', '')}` "
            f"status=`{artifact_verify.get('status', '')}`"
        )
    lines.extend(["", "## Failures", ""])
    failures = report.get("failures", [])
    if isinstance(failures, list) and failures:
        lines.extend(f"- `{failure}`" for failure in failures)
    else:
        lines.append("- None.")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def candidate_artifact_paths(
    *,
    root: Path,
    candidate_json: Path,
    candidate_markdown: Path,
    log_dir: Path,
    gate_json: str,
) -> list[str]:
    paths = set(
        gate.collect_alpha_artifact_paths(
            root=root,
            gate_json=candidate_json,
            matrix_raw="",
            matrix_summary="",
        )
    )
    for path in [
        candidate_json,
        candidate_markdown,
        root / "docs" / "release" / "ALPHA_LIMITATIONS.md",
        root / "docs" / "ARCHITECTURE_ALPHA.md",
        root / "docs" / "release" / "PHASE6E_ALPHA_RC.md",
        root / "tools" / "release" / "run_alpha_release_candidate.py",
    ]:
        if path.is_file():
            paths.add(gate.relative_to_root(str(path), root))
    gate_payload = read_json(gate_json)
    private_matrix = gate_payload.get("private_matrix", {}) if isinstance(gate_payload, dict) else {}
    if isinstance(private_matrix, dict):
        for key in ["raw_csv", "summary_csv"]:
            value = str(private_matrix.get(key, ""))
            if value:
                paths.update(gate.csv_referenced_artifacts(value, root))
    for path in log_dir.rglob("*"):
        if path.is_file() and path.suffix in {".json", ".log", ".jsonl", ".md", ".txt"}:
            paths.add(gate.relative_to_root(str(path), root))
    return sorted(paths)


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the complete GridFlux alpha release-candidate package.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--io-uring-build-dir", default="build-io-uring-real")
    parser.add_argument("--remote")
    parser.add_argument("--remote-root", default="/root/projects/GridFlux")
    parser.add_argument("--server-host")
    parser.add_argument("--results-dir", default="tools/perf/results")
    parser.add_argument("--soak-iterations", type=int, default=5)
    parser.add_argument("--duration-seconds", type=float, default=600.0)
    parser.add_argument("--profile", choices=["tiny", "small", "mixed"], default="tiny")
    parser.add_argument("--local-only", action="store_true", help="Run a non-release local-only RC dry validation.")
    args = parser.parse_args()

    if not args.local_only and (not args.remote or not args.server_host):
        raise SystemExit("complete alpha release candidate requires --remote and --server-host")
    if args.soak_iterations <= 0:
        raise SystemExit("--soak-iterations must be positive")
    if args.duration_seconds < 0:
        raise SystemExit("--duration-seconds must be non-negative")

    root = gate.repo_root()
    results_dir = root / args.results_dir
    results_dir.mkdir(parents=True, exist_ok=True)
    timestamp = gate.compact_timestamp()
    log_dir = results_dir / f"{timestamp}_alpha-release-candidate"
    log_dir.mkdir(parents=True, exist_ok=True)
    json_path = results_dir / f"{timestamp}_alpha-release-candidate.json"
    markdown_path = root / "docs" / "release" / "ALPHA_RELEASE_CANDIDATE.md"
    manifest_path = results_dir / f"{timestamp}_alpha-release-candidate-artifacts.json"

    steps: list[gate.StepResult] = []
    gate_command = [
        sys.executable,
        "tools/release/run_alpha_release_gate.py",
        "--quick" if args.local_only else "--full",
        "--build-dir",
        args.build_dir,
        "--io-uring-build-dir",
        args.io_uring_build_dir,
        "--results-dir",
        args.results_dir,
    ]
    if args.remote:
        gate_command.extend(["--remote", args.remote, "--remote-root", args.remote_root])
    if args.server_host and not args.local_only:
        gate_command.extend(["--server-host", args.server_host])
    gate_step = gate.run_step("alpha_release_gate_full" if not args.local_only else "alpha_release_gate_quick", gate_command, log_dir, cwd=root)
    steps.append(gate_step)

    gate_log_text = Path(gate_step.log).read_text(encoding="utf-8", errors="replace")
    gate_paths = parse_gate_paths_from_log(gate_log_text)
    nested_gate_json = gate_paths.get("alpha_release_gate_json", "")
    nested_gate_report = read_json(nested_gate_json)

    soak_step = gate.run_step(
        "alpha_long_soak",
        [
            sys.executable,
            "tools/test/run_alpha_soak_smoke.py",
            "--build-dir",
            args.build_dir,
            "--iterations",
            str(args.soak_iterations),
            "--duration-seconds",
            str(args.duration_seconds),
            "--profile",
            args.profile,
            "--token",
            "--tls",
            "--data-tls",
            "--results-dir",
            str(log_dir / "alpha_long_soak"),
            "--json-output",
            str(log_dir / "alpha_long_soak.json"),
            "--event-log",
            str(log_dir / "alpha_long_soak_events.jsonl"),
        ],
        log_dir,
        cwd=root,
    )
    steps.append(soak_step)

    hygiene_step = gate.run_step(
        "public_export_hygiene",
        [
            sys.executable,
            "tools/release/export_public_repo.py",
            "--output",
            f"/tmp/gridflux-public-alpha-rc-{timestamp}",
            "--force",
        ],
        log_dir,
        cwd=root,
    )
    steps.append(hygiene_step)

    residual = gate.run_remote_process_check(args.remote)
    source_hash = gate.source_tree_hash(root)
    step_summary = summarize_steps(steps)
    failures = [step.name for step in steps if step.status != "pass"]
    if not nested_gate_report.get("passed", False):
        failures.append("nested_alpha_release_gate")
    if residual.get("local") or residual.get("remote"):
        failures.append("residual_process_check")
    if args.local_only:
        failures.append("local_only_not_complete")

    report: dict[str, object] = {
        "timestamp": gate.timestamp_utc(),
        "mode": "local-only" if args.local_only else "full",
        "source_tree_hash": source_hash,
        "default_strategy": default_strategy_summary(),
        "steps": [asdict(step) for step in steps],
        **step_summary,
        "alpha_release_gate": {
            "json": nested_gate_json,
            "report": gate_paths.get("alpha_release_gate_report", ""),
            "artifact_manifest": gate_paths.get("alpha_artifact_manifest", ""),
            "passed": nested_gate_report.get("passed", False),
            "total_steps": nested_gate_report.get("total_steps", 0),
            "failed_steps": nested_gate_report.get("failed_steps", 0),
        },
        "soak_summary": gate.read_json_if_exists(log_dir / "alpha_long_soak.json"),
        "artifact_manifest": {},
        "artifact_manifest_freshness": {},
        "artifact_sync_summary": {},
        "artifact_verify_summary": {},
        "residual_process_check": residual,
        "failures": failures,
        "passed": False,
    }
    write_candidate_markdown(markdown_path, report)
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    artifact_paths = candidate_artifact_paths(
        root=root,
        candidate_json=json_path,
        candidate_markdown=markdown_path,
        log_dir=log_dir,
        gate_json=nested_gate_json,
    )
    manifest_data = gate.write_alpha_artifact_manifest(
        path=manifest_path,
        root=root,
        source_hash=source_hash,
        remote_required=bool(args.remote and not args.local_only),
        artifact_paths=artifact_paths,
    )
    report["artifact_manifest"] = {
        "path": gate.relative_to_root(str(manifest_path), root),
        "artifact_count": len(manifest_data.get("artifacts", [])),
        "remote_required": bool(args.remote and not args.local_only),
    }
    report["artifact_manifest_freshness"] = gate.check_alpha_artifact_manifest_freshness(manifest_path, root)

    if args.remote and not args.local_only:
        sync_json = log_dir / "remote-artifact-sync.json"
        sync_step = gate.run_step(
            "remote_artifact_sync",
            [
                sys.executable,
                "tools/release/sync_remote_artifacts.py",
                "--manifest",
                gate.relative_to_root(str(manifest_path), root),
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
        verify_json = log_dir / "remote-artifact-verify.json"
        verify_step = gate.run_step(
            "remote_artifact_verify",
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
                gate.relative_to_root(str(manifest_path), root),
                "--json-output",
                str(verify_json),
            ],
            log_dir,
            cwd=root,
        )
        steps.append(verify_step)
        report["artifact_sync_summary"] = gate.read_json_if_exists(sync_json)
        report["artifact_verify_summary"] = gate.read_json_if_exists(verify_json)

    failures = [step.name for step in steps if step.status != "pass"]
    if not nested_gate_report.get("passed", False):
        failures.append("nested_alpha_release_gate")
    if residual.get("local") or residual.get("remote"):
        failures.append("residual_process_check")
    freshness = report.get("artifact_manifest_freshness", {})
    if isinstance(freshness, dict) and freshness.get("status") != "pass":
        failures.append("artifact_manifest_freshness")
    sync_summary = report.get("artifact_sync_summary", {})
    verify_summary = report.get("artifact_verify_summary", {})
    if args.remote and not args.local_only:
        if isinstance(sync_summary, dict) and sync_summary.get("status") != "pass":
            failures.append("artifact_sync_summary")
        if isinstance(verify_summary, dict) and verify_summary.get("status") != "pass":
            failures.append("artifact_verify_summary")
    if args.local_only:
        failures.append("local_only_not_complete")

    step_summary = summarize_steps(steps)
    report["steps"] = [asdict(step) for step in steps]
    report.update(step_summary)
    failed_steps = [asdict(step) for step in steps if step.status != "pass"]
    report["first_failed_step"] = failed_steps[0] if failed_steps else {}
    report["failures"] = failures
    report["passed"] = not failures

    # Re-write the manifest after final report/JSON updates, then check freshness.
    artifact_paths = candidate_artifact_paths(
        root=root,
        candidate_json=json_path,
        candidate_markdown=markdown_path,
        log_dir=log_dir,
        gate_json=nested_gate_json,
    )
    report["artifact_manifest"] = {
        "path": gate.relative_to_root(str(manifest_path), root),
        "artifact_count": len(artifact_paths),
        "remote_required": bool(args.remote and not args.local_only),
    }
    report["artifact_manifest_freshness"] = {
        "path": gate.relative_to_root(str(manifest_path), root),
        "checked": len(artifact_paths),
        "stale_count": 0,
        "status": "pass",
        "stale": [],
    }
    write_candidate_markdown(markdown_path, report)
    json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    manifest_data = gate.write_alpha_artifact_manifest(
        path=manifest_path,
        root=root,
        source_hash=source_hash,
        remote_required=bool(args.remote and not args.local_only),
        artifact_paths=artifact_paths,
    )
    final_freshness = gate.check_alpha_artifact_manifest_freshness(manifest_path, root)
    if len(manifest_data.get("artifacts", [])) != len(artifact_paths):
        final_freshness["status"] = "fail"
        final_freshness["stale_count"] = int(final_freshness.get("stale_count", 0)) + 1
        final_freshness.setdefault("stale", []).append(
            {
                "path": gate.relative_to_root(str(manifest_path), root),
                "reason": "artifact_count_changed_during_manifest_write",
                "expected": len(artifact_paths),
                "actual": len(manifest_data.get("artifacts", [])),
            }
        )
    if final_freshness.get("status") != "pass":
        report["artifact_manifest_freshness"] = final_freshness
        report["failures"] = list(report.get("failures", [])) + ["artifact_manifest_freshness"]
        report["passed"] = False
        write_candidate_markdown(markdown_path, report)
        json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"alpha_release_candidate_report={markdown_path}")
        print(f"alpha_release_candidate_json={json_path}")
        print(f"alpha_release_candidate_artifact_manifest={manifest_path}")
        print("result=fail")
        return 1

    if args.remote and not args.local_only:
        post_sync_json = log_dir / "remote-artifact-post-report-sync.json"
        post_sync = gate.run_step(
            "remote_artifact_post_report_sync",
            [
                sys.executable,
                "tools/release/sync_remote_artifacts.py",
                "--manifest",
                gate.relative_to_root(str(manifest_path), root),
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
        post_verify = gate.run_step(
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
                gate.relative_to_root(str(manifest_path), root),
                "--json-output",
                str(post_verify_json),
            ],
            log_dir,
            cwd=root,
        )
        if post_sync.status != "pass" or post_verify.status != "pass":
            steps.extend([post_sync, post_verify])
            report["steps"] = [asdict(step) for step in steps]
            report["failures"] = [step.name for step in steps if step.status != "pass"]
            report["passed"] = False
            write_candidate_markdown(markdown_path, report)
            json_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print(f"alpha_release_candidate_report={markdown_path}")
    print(f"alpha_release_candidate_json={json_path}")
    print(f"alpha_release_candidate_artifact_manifest={manifest_path}")
    print(f"result={'pass' if report['passed'] else 'fail'}")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
