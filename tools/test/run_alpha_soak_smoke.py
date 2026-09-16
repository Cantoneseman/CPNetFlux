#!/usr/bin/env python3
"""Short local alpha soak smoke built from the tiny alpha demo."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def merge_error_counts(target: dict[str, int], source: dict[str, object]) -> None:
    for key, value in source.items():
        try:
            target[key] = target.get(key, 0) + int(value)
        except (TypeError, ValueError):
            continue


def demo_command(
    args: argparse.Namespace,
    *,
    iteration_json: Path,
    results_dir: Path,
    token_file: str,
) -> list[str]:
    auth_mode = "token" if getattr(args, "token", False) else args.auth_mode
    tls_enabled = bool(getattr(args, "tls", False) or getattr(args, "data_tls", False))
    command = [
        sys.executable,
        str(REPO_ROOT / "tools" / "demo" / "run_alpha_demo.py"),
        "--mode",
        "local",
        "--build-dir",
        args.build_dir,
        "--profile",
        args.profile,
        "--results-dir",
        str(results_dir),
        "--json-output",
        str(iteration_json),
        "--auth-mode",
        auth_mode,
    ]
    if token_file:
        command.extend(["--auth-token-file", token_file])
    if args.event_log:
        command.extend(["--event-log", args.event_log])
    if tls_enabled:
        command.extend(["--tls-mode", "required"])
    if getattr(args, "data_tls", False):
        command.extend(["--data-tls-mode", "required"])
    return command


def run_soak(args: argparse.Namespace) -> int:
    results_dir = Path(args.results_dir)
    results_dir.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    pass_count = 0
    fail_count = 0
    first_failure: dict[str, object] | None = None
    total_bytes = 0
    error_counts: dict[str, int] = {}
    attempted_iterations = 0

    token_context = None
    token_file = args.auth_token_file
    auth_mode = "token" if args.token else args.auth_mode
    if auth_mode == "token" and not token_file:
        token_context = tempfile.TemporaryDirectory(prefix="cpnetflux-soak-token.")
        path = Path(token_context.name) / "auth-token.txt"
        path.write_text("phase6b-alpha-soak-token\n", encoding="utf-8")
        path.chmod(0o600)
        token_file = str(path)

    try:
        for index in range(args.iterations):
            if args.duration_seconds and index > 0 and time.monotonic() - start >= args.duration_seconds:
                break
            iteration_json = results_dir / f"alpha_soak_iteration_{index + 1}.json"
            iteration_log = results_dir / f"alpha_soak_iteration_{index + 1}.log"
            command = demo_command(args, iteration_json=iteration_json, results_dir=results_dir, token_file=token_file)
            completed = subprocess.run(
                command,
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
            attempted_iterations += 1
            iteration_log.write_text(
                "$ " + " ".join(command) + "\n\n" + completed.stdout + completed.stderr,
                encoding="utf-8",
            )
            payload: dict[str, object] = {}
            if iteration_json.exists():
                payload = json.loads(iteration_json.read_text(encoding="utf-8"))
            merge_error_counts(error_counts, dict(payload.get("error_code_counts", {}) or {}))
            for case in payload.get("cases", []) if isinstance(payload.get("cases"), list) else []:
                if isinstance(case, dict):
                    total_bytes += int(case.get("bytes", 0) or 0)
            if completed.returncode == 0 and payload.get("result") == "pass":
                pass_count += 1
                continue
            fail_count += 1
            if first_failure is None:
                first_failure = {
                    "iteration": index + 1,
                    "log": str(iteration_log),
                    "error_code": (payload.get("first_error") or {}).get("error_code")
                    if isinstance(payload.get("first_error"), dict)
                    else "unknown_error",
                }
            break
    finally:
        if token_context is not None:
            token_context.cleanup()

    elapsed = max(time.monotonic() - start, 0.000001)
    summary = {
        "iterations": attempted_iterations,
        "requested_iterations": args.iterations,
        "duration_seconds": args.duration_seconds,
        "profile": args.profile,
        "auth_mode": auth_mode,
        "tls": bool(args.tls or args.data_tls),
        "data_tls": bool(args.data_tls),
        "pass_count": pass_count,
        "fail_count": fail_count,
        "first_failure": first_failure,
        "total_bytes": total_bytes,
        "elapsed_seconds": elapsed,
        "error_code_counts": error_counts,
        "event_log_path": args.event_log,
        "result": "pass" if fail_count == 0 and pass_count == args.iterations else "fail",
    }
    if args.duration_seconds:
        summary["result"] = "pass" if fail_count == 0 and attempted_iterations > 0 else "fail"
    output = Path(args.json_output) if args.json_output else results_dir / "alpha_soak_summary.json"
    output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"alpha_soak_json={output}")
    print(f"result={summary['result']}")
    return 0 if summary["result"] == "pass" else 1


def main() -> int:
    parser = argparse.ArgumentParser(description="Run a short local CPNetFlux alpha soak smoke.")
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--duration-seconds", type=float, default=0.0)
    parser.add_argument("--profile", choices=["tiny", "small", "mixed"], default="tiny")
    parser.add_argument("--auth-mode", choices=["anonymous", "token"], default="anonymous")
    parser.add_argument("--token", action="store_true", help="Alias for --auth-mode token with a temporary token file.")
    parser.add_argument("--auth-token-file", default="")
    parser.add_argument("--tls", action="store_true", help="Run each demo iteration with control-plane TLS required.")
    parser.add_argument("--data-tls", action="store_true", help="Run each demo iteration with control and STOR/RETR data TLS required.")
    parser.add_argument("--event-log", default="")
    parser.add_argument("--json-output", default="")
    parser.add_argument("--results-dir", default="tools/perf/results")
    args = parser.parse_args()
    if args.iterations <= 0:
        raise SystemExit("--iterations must be positive")
    if args.duration_seconds < 0:
        raise SystemExit("--duration-seconds must be non-negative")
    return run_soak(args)


if __name__ == "__main__":
    raise SystemExit(main())
