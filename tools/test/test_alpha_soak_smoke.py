#!/usr/bin/env python3
"""Helper tests for the alpha soak smoke wrapper."""

from __future__ import annotations

import argparse
import json
import tempfile
import time
from pathlib import Path

import run_alpha_soak_smoke


def base_args(**overrides):
    values = {
        "build_dir": "build",
        "iterations": 3,
        "duration_seconds": 0.0,
        "profile": "tiny",
        "auth_mode": "anonymous",
        "token": False,
        "auth_token_file": "",
        "tls": False,
        "data_tls": False,
        "event_log": "",
        "json_output": "",
        "results_dir": "unused",
    }
    values.update(overrides)
    return argparse.Namespace(**values)


def test_demo_command_adds_token_tls_and_data_tls() -> None:
    args = base_args(token=True, tls=True, data_tls=True, profile="small", event_log="events.jsonl")
    command = run_alpha_soak_smoke.demo_command(
        args,
        iteration_json=Path("iteration.json"),
        results_dir=Path("results"),
        token_file="/tmp/token.txt",
    )
    joined = " ".join(command)
    for expected in [
        "--profile small",
        "--auth-mode token",
        "--auth-token-file /tmp/token.txt",
        "--tls-mode required",
        "--data-tls-mode required",
        "--event-log events.jsonl",
    ]:
        if expected not in joined:
            raise AssertionError(f"missing expected command fragment {expected!r}: {joined}")


def test_duration_stops_after_completed_iteration(monkeypatch=None) -> None:
    with tempfile.TemporaryDirectory(prefix="cpnetflux-soak-test.") as temp:
        root = Path(temp)
        output = root / "summary.json"
        calls = {"count": 0}
        original_run = run_alpha_soak_smoke.subprocess.run

        def fake_run(command, cwd, text, capture_output, check):  # noqa: ANN001 - matches subprocess.run shape.
            calls["count"] += 1
            json_path = Path(command[command.index("--json-output") + 1])
            json_path.write_text(
                json.dumps(
                    {
                        "result": "pass",
                        "cases": [{"bytes": 123}],
                        "error_code_counts": {"ok": 1},
                    }
                ),
                encoding="utf-8",
            )
            time.sleep(0.01)

            class Completed:
                returncode = 0
                stdout = ""
                stderr = ""

            return Completed()

        run_alpha_soak_smoke.subprocess.run = fake_run
        try:
            args = base_args(
                iterations=10,
                duration_seconds=0.001,
                results_dir=str(root),
                json_output=str(output),
            )
            code = run_alpha_soak_smoke.run_soak(args)
        finally:
            run_alpha_soak_smoke.subprocess.run = original_run
        if code != 0:
            raise AssertionError("duration-limited soak should pass after a successful iteration")
        payload = json.loads(output.read_text(encoding="utf-8"))
        if payload["iterations"] != 1 or payload["pass_count"] != 1 or calls["count"] != 1:
            raise AssertionError(f"duration limit did not stop after one iteration: {payload}, calls={calls}")
        if payload.get("event_log_path") != "":
            raise AssertionError(f"unexpected event log path: {payload}")


def main() -> int:
    test_demo_command_adds_token_tls_and_data_tls()
    test_duration_stops_after_completed_iteration()
    print("alpha soak helper tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
