#!/usr/bin/env python3
"""Summarize Phase 2 feedback-driven global scheduler matrix evidence."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def load_rows(paths: list[str]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for text in paths:
        path = Path(text)
        with path.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                row["_source_csv"] = str(path)
                rows.append(row)
    return rows


def as_float(row: dict[str, str], field: str) -> float:
    try:
        value = float(row.get(field, "") or "0")
        return 0.0 if math.isnan(value) or math.isinf(value) else value
    except ValueError:
        return 0.0


def markdown_cell(value: str) -> str:
    return str(value).replace("\n", " ").replace("|", "\\|")


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return "_No rows._\n"
    output = ["| " + " | ".join(markdown_cell(header) for header in headers) + " |"]
    output.append("| " + " | ".join(["---"] * len(headers)) + " |")
    output.extend("| " + " | ".join(markdown_cell(value) for value in row) + " |" for row in rows)
    return "\n".join(output) + "\n"


def pct_delta(value: float, baseline: float) -> str:
    if baseline <= 0.0:
        return ""
    return f"{((value - baseline) / baseline) * 100.0:+.1f}%"


def comparison_key(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row.get("dataset", ""),
        row.get("direction", ""),
        row.get("resume", ""),
        row.get("file_parallelism", ""),
        row.get("connections", ""),
        row.get("checksum_algorithm", ""),
        row.get("checksum_backend", ""),
    )


def label(row: dict[str, str]) -> str:
    resume = "resume" if row.get("resume") == "1" else "fresh"
    return (
        f"{row.get('dataset', '')}/{row.get('direction', '')}/{resume} "
        f"fp={row.get('file_parallelism', '')} conn={row.get('connections', '')} "
        f"checksum={row.get('checksum_algorithm', '')}"
    )


def mode_label(row: dict[str, str]) -> str:
    mode = row.get("scheduler_mode", "")
    policy = row.get("scheduler_policy", "")
    if mode == "global":
        return f"{mode}/{policy}"
    return mode or "off"


def render_summary(summary_rows: list[dict[str, str]]) -> str:
    rows: list[list[str]] = []
    for row in sorted(summary_rows, key=lambda item: (comparison_key(item), mode_label(item))):
        rows.append(
            [
                label(row),
                mode_label(row),
                "/".join(
                    [
                        row.get("scheduler_initial_connections", ""),
                        row.get("scheduler_current_connections", ""),
                        row.get("scheduler_target_connections", ""),
                        row.get("scheduler_max_connections", ""),
                    ]
                ),
                row.get("repeat_count", ""),
                row.get("pass_count", ""),
                row.get("fail_count", ""),
                row.get("tree_hash_mismatch_count", ""),
                row.get("throughput_gbps_median", ""),
                row.get("elapsed_seconds_median", ""),
                row.get("scheduler_dominant_bottleneck", ""),
                row.get("scheduler_metrics_present_count", ""),
                row.get("scheduler_dispatch_count", ""),
                row.get("scheduler_ramp_up_count", ""),
                row.get("scheduler_ramp_down_count", ""),
                row.get("scheduler_queue_low_count", ""),
                row.get("scheduler_queue_high_count", ""),
                row.get("scheduler_send_pressure_count", ""),
                row.get("scheduler_write_pressure_count", ""),
                row.get("scheduler_cpu_pressure_count", ""),
                row.get("scheduler_retry_count", ""),
                row.get("scheduler_max_send_pressure", ""),
                row.get("scheduler_max_write_pressure", ""),
                row.get("scheduler_max_cpu_pressure", ""),
                row.get("resume_partial_result_count", ""),
            ]
        )
    return markdown_table(
        [
            "case",
            "scheduler",
            "initial/current/target/max",
            "repeat",
            "pass",
            "fail",
            "hash mismatch",
            "median Gbps",
            "median elapsed s",
            "bottleneck",
            "metrics",
            "dispatch",
            "ramp up",
            "ramp down",
            "queue low",
            "queue high",
            "send high",
            "write high",
            "cpu high",
            "retries",
            "max send p",
            "max write p",
            "max cpu %",
            "partial resume",
        ],
        rows,
    )


def render_adaptive_comparison(summary_rows: list[dict[str, str]]) -> str:
    groups: dict[tuple[str, ...], dict[str, dict[str, str]]] = {}
    for row in summary_rows:
        groups.setdefault(comparison_key(row), {})[mode_label(row)] = row

    rows: list[list[str]] = []
    for modes in groups.values():
        fixed = modes.get("global/fixed")
        adaptive = modes.get("global/adaptive")
        baseline = modes.get("off")
        if not fixed or not adaptive:
            continue
        fixed_gbps = as_float(fixed, "throughput_gbps_median")
        adaptive_gbps = as_float(adaptive, "throughput_gbps_median")
        off_gbps = as_float(baseline, "throughput_gbps_median") if baseline else 0.0
        rows.append(
            [
                label(fixed),
                f"{off_gbps:.6f}" if off_gbps else "",
                f"{fixed_gbps:.6f}" if fixed_gbps else "",
                f"{adaptive_gbps:.6f}" if adaptive_gbps else "",
                pct_delta(adaptive_gbps, fixed_gbps),
                adaptive.get("scheduler_dominant_bottleneck", ""),
                adaptive.get("scheduler_ramp_up_count", ""),
                adaptive.get("scheduler_ramp_down_count", ""),
                adaptive.get("scheduler_queue_low_count", ""),
                adaptive.get("scheduler_queue_high_count", ""),
            ]
        )
    return markdown_table(
        [
            "case",
            "off Gbps",
            "global fixed Gbps",
            "global adaptive Gbps",
            "adaptive vs fixed",
            "adaptive bottleneck",
            "ramp up",
            "ramp down",
            "queue low",
            "queue high",
        ],
        rows,
    )


def render_compression_metrics(summary_rows: list[dict[str, str]]) -> str:
    rows: list[list[str]] = []
    for row in sorted(summary_rows, key=lambda item: (comparison_key(item), mode_label(item))):
        rows.append(
            [
                label(row),
                mode_label(row),
                row.get("scheduler_logical_bytes", ""),
                row.get("scheduler_wire_bytes_median", ""),
                row.get("scheduler_compression_ratio_effective_median", ""),
                row.get("scheduler_compression_attempts", ""),
                row.get("scheduler_compressed_workitems", ""),
                row.get("scheduler_raw_fallback_workitems", ""),
                row.get("scheduler_compression_failures", ""),
                row.get("scheduler_decompression_failures", ""),
                row.get("scheduler_retried_workitems", ""),
                row.get("scheduler_compression_dispatch_count", ""),
                row.get("scheduler_compression_reject_count", ""),
                row.get("scheduler_compression_fallback_raw_count", ""),
                row.get("scheduler_workitem_retry_count", ""),
            ]
        )
    return markdown_table(
        [
            "case",
            "scheduler",
            "logical bytes",
            "wire bytes median",
            "ratio median",
            "attempts",
            "compressed",
            "raw fallback",
            "comp fail",
            "decomp fail",
            "retried items",
            "dispatch events",
            "reject events",
            "fallback events",
            "retry events",
        ],
        rows,
    )


def render_failures(raw_rows: list[dict[str, str]]) -> str:
    failed = [
        row
        for row in raw_rows
        if row.get("result") != "pass"
        or (
            row.get("source_tree_hash")
            and row.get("dest_tree_hash")
            and row.get("source_tree_hash") != row.get("dest_tree_hash")
        )
    ]
    rows: list[list[str]] = []
    for row in failed[:20]:
        rows.append(
            [
                label(row),
                mode_label(row),
                row.get("result", ""),
                "1" if row.get("source_tree_hash") != row.get("dest_tree_hash") else "0",
                row.get("error_message") or row.get("error", ""),
                row.get("client_log", ""),
            ]
        )
    return markdown_table(
        ["case", "scheduler", "result", "hash mismatch", "error", "client log"],
        rows,
    )


def render(raw_rows: list[dict[str, str]], summary_rows: list[dict[str, str]],
           inputs: list[str]) -> str:
    pass_count = sum(1 for row in raw_rows if row.get("result") == "pass")
    mismatch_count = sum(
        1
        for row in raw_rows
        if row.get("source_tree_hash")
        and row.get("dest_tree_hash")
        and row.get("source_tree_hash") != row.get("dest_tree_hash")
    )
    global_rows = [row for row in raw_rows if row.get("scheduler_mode") == "global"]
    metrics_present = sum(
        1 for row in global_rows if row.get("scheduler_metrics_present") == "1"
    )
    modes = sorted({mode_label(row) for row in raw_rows if mode_label(row)})
    content = [
        "# Phase 2 Feedback-Driven Global Scheduler Analysis",
        "",
        "## Inputs",
        "",
        *[f"- `{path}`" for path in inputs],
        "",
        "## Verdict",
        "",
        f"- Raw rows: `{len(raw_rows)}`; pass: `{pass_count}`; fail: `{len(raw_rows) - pass_count}`.",
        f"- Tree hash mismatches: `{mismatch_count}`.",
        f"- Scheduler modes observed: `{','.join(modes)}`.",
        f"- Global scheduler rows with complete sidecars: `{metrics_present}/{len(global_rows)}`.",
        "- Scope: 10M cloud validation supports correctness, resume, queue/backpressure and metrics explainability only; it is not 100G readiness evidence.",
        "",
        "## Summary CSV View",
        "",
        render_summary(summary_rows),
        "## Adaptive Versus Fixed",
        "",
        render_adaptive_comparison(summary_rows),
        "## Phase 3 Compression Metrics",
        "",
        render_compression_metrics(summary_rows),
        "## Failures Or Hash Mismatches",
        "",
        render_failures(raw_rows),
        "## Preserved Boundaries",
        "",
        "- `--scheduler off` remains the default baseline.",
        "- `global/fixed` preserves Phase 1 scheduler behavior for comparison.",
        "- `global/adaptive` changes only future file dispatch connection targets.",
        "- Completion evidence remains tree manifest, per-file manifest and verified chunks.",
        "- No daemon/API, Prometheus/HTTP, data-channel reuse, NIC/NUMA pinning, lossy compression, or 100G readiness claim is introduced.",
        "",
    ]
    return "\n".join(content)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Analyze Phase 2 feedback-driven global scheduler matrix CSVs.")
    parser.add_argument("--raw-csv", action="append", required=True)
    parser.add_argument("--summary-csv", action="append", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    raw_rows = load_rows(args.raw_csv)
    summary_rows = load_rows(args.summary_csv)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render(raw_rows, summary_rows, args.raw_csv + args.summary_csv),
                      encoding="utf-8")
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
