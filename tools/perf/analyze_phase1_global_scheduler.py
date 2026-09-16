#!/usr/bin/env python3
"""Summarize Phase 1 global scheduler tree matrix evidence."""

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
        value = row.get(field, "")
        if value == "":
            return 0.0
        parsed = float(value)
        if math.isnan(parsed) or math.isinf(parsed):
            return 0.0
        return parsed
    except (TypeError, ValueError):
        return 0.0


def as_int(row: dict[str, str], field: str) -> int:
    try:
        return int(row.get(field, ""))
    except (TypeError, ValueError):
        return 0


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return "_No rows._\n"
    output = ["| " + " | ".join(markdown_cell(header) for header in headers) + " |"]
    output.append("| " + " | ".join(["---"] * len(headers)) + " |")
    output.extend("| " + " | ".join(markdown_cell(value) for value in row) + " |" for row in rows)
    return "\n".join(output) + "\n"


def markdown_cell(value: str) -> str:
    return str(value).replace("\n", " ").replace("|", "\\|")


def pct_delta(value: float, baseline: float) -> str:
    if baseline <= 0:
        return ""
    return f"{((value - baseline) / baseline) * 100.0:+.1f}%"


def summary_key_without_scheduler(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row.get("dataset", ""),
        row.get("direction", ""),
        row.get("resume", ""),
        row.get("file_parallelism", ""),
        row.get("connections", ""),
        row.get("checksum_algorithm", ""),
        row.get("checksum_backend", ""),
    )


def label_for_summary(row: dict[str, str]) -> str:
    resume = "resume" if row.get("resume") == "1" else "fresh"
    return (
        f"{row.get('dataset', '')}/{row.get('direction', '')}/{resume} "
        f"fp={row.get('file_parallelism', '')} conn={row.get('connections', '')} "
        f"checksum={row.get('checksum_algorithm', '')}"
    )


def render_summary_table(summary_rows: list[dict[str, str]]) -> str:
    rows: list[list[str]] = []
    for row in sorted(
        summary_rows,
        key=lambda item: (
            summary_key_without_scheduler(item),
            item.get("scheduler_mode", ""),
        ),
    ):
        rows.append(
            [
                label_for_summary(row),
                row.get("scheduler_mode", ""),
                row.get("repeat_count", ""),
                row.get("pass_count", ""),
                row.get("fail_count", ""),
                row.get("tree_hash_mismatch_count", ""),
                row.get("throughput_gbps_median", ""),
                row.get("elapsed_seconds_median", ""),
                row.get("scheduler_dominant_bottleneck", ""),
                row.get("scheduler_dispatch_count", ""),
                row.get("scheduler_ramp_up_count", ""),
                row.get("scheduler_ramp_down_count", ""),
            ]
        )
    return markdown_table(
        [
            "case",
            "scheduler",
            "repeat",
            "pass",
            "fail",
            "hash mismatch",
            "median Gbps",
            "median elapsed s",
            "scheduler bottleneck",
            "dispatch",
            "ramp up",
            "ramp down",
        ],
        rows,
    )


def render_comparison_table(summary_rows: list[dict[str, str]]) -> str:
    groups: dict[tuple[str, ...], dict[str, dict[str, str]]] = {}
    for row in summary_rows:
        groups.setdefault(summary_key_without_scheduler(row), {})[row.get("scheduler_mode", "")] = row

    rows: list[list[str]] = []
    for _key, modes in sorted(groups.items()):
        off = modes.get("off")
        global_row = modes.get("global")
        if not off or not global_row:
            continue
        off_gbps = as_float(off, "throughput_gbps_median")
        global_gbps = as_float(global_row, "throughput_gbps_median")
        off_elapsed = as_float(off, "elapsed_seconds_median")
        global_elapsed = as_float(global_row, "elapsed_seconds_median")
        rows.append(
            [
                label_for_summary(off),
                f"{off_gbps:.6f}" if off_gbps else "",
                f"{global_gbps:.6f}" if global_gbps else "",
                pct_delta(global_gbps, off_gbps),
                f"{off_elapsed:.6f}" if off_elapsed else "",
                f"{global_elapsed:.6f}" if global_elapsed else "",
                pct_delta(global_elapsed, off_elapsed),
                global_row.get("scheduler_dominant_bottleneck", ""),
                global_row.get("scheduler_metrics_present_count", ""),
            ]
        )
    return markdown_table(
        [
            "comparison case",
            "off median Gbps",
            "global median Gbps",
            "global vs off",
            "off elapsed s",
            "global elapsed s",
            "elapsed delta",
            "global bottleneck",
            "global metrics rows",
        ],
        rows,
    )


def render_compression_metrics(summary_rows: list[dict[str, str]]) -> str:
    rows: list[list[str]] = []
    for row in sorted(
        summary_rows,
        key=lambda item: (
            summary_key_without_scheduler(item),
            item.get("scheduler_mode", ""),
        ),
    ):
        rows.append(
            [
                label_for_summary(row),
                row.get("scheduler_mode", ""),
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


def render_slow_global_rows(raw_rows: list[dict[str, str]]) -> str:
    candidates = [
        row
        for row in raw_rows
        if row.get("scheduler_mode") == "global" and row.get("result") == "pass"
    ]
    candidates.sort(key=lambda row: as_float(row, "elapsed_seconds"), reverse=True)
    rows: list[list[str]] = []
    for row in candidates[:8]:
        rows.append(
            [
                row.get("comparison_key", ""),
                row.get("elapsed_seconds", ""),
                row.get("throughput_gbps", ""),
                row.get("scheduler_dominant_bottleneck", ""),
                row.get("scheduler_event_count", ""),
                row.get("scheduler_dispatch_count", ""),
                row.get("scheduler_ramp_up_count", ""),
                row.get("scheduler_ramp_down_count", ""),
                row.get("scheduler_summary_csv", ""),
                row.get("scheduler_events_jsonl", ""),
                row.get("scheduler_samples_csv", ""),
            ]
        )
    return markdown_table(
        [
            "case",
            "elapsed s",
            "Gbps",
            "bottleneck",
            "events",
            "dispatch",
            "ramp up",
            "ramp down",
            "summary csv",
            "events jsonl",
            "samples csv",
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
                row.get("comparison_key", ""),
                row.get("scheduler_mode", ""),
                row.get("repeat_index", ""),
                row.get("result", ""),
                "1"
                if row.get("source_tree_hash")
                and row.get("dest_tree_hash")
                and row.get("source_tree_hash") != row.get("dest_tree_hash")
                else "0",
                row.get("error_message") or row.get("error", ""),
                row.get("client_log", ""),
            ]
        )
    return markdown_table(
        ["case", "scheduler", "repeat", "result", "hash mismatch", "error", "client log"],
        rows,
    )


def render(raw_rows: list[dict[str, str]], summary_rows: list[dict[str, str]], inputs: list[str]) -> str:
    pass_count = sum(1 for row in raw_rows if row.get("result") == "pass")
    mismatch_count = sum(
        1
        for row in raw_rows
        if row.get("source_tree_hash")
        and row.get("dest_tree_hash")
        and row.get("source_tree_hash") != row.get("dest_tree_hash")
    )
    scheduler_modes = sorted({row.get("scheduler_mode", "") for row in raw_rows if row.get("scheduler_mode")})
    global_rows = [row for row in raw_rows if row.get("scheduler_mode") == "global"]
    global_metrics_count = sum(
        1
        for row in global_rows
        if row.get("scheduler_summary_csv") and Path(row["scheduler_summary_csv"]).is_file()
    )
    comparable_pairs = sum(
        1
        for modes in {
            key: {row.get("scheduler_mode", "") for row in summary_rows if summary_key_without_scheduler(row) == key}
            for key in {summary_key_without_scheduler(row) for row in summary_rows}
        }.values()
        if {"off", "global"} <= modes
    )

    content = [
        "# Phase 1 Global Scheduler Matrix Analysis",
        "",
        "## Inputs",
        "",
        *[f"- `{path}`" for path in inputs],
        "",
        "## Verdict",
        "",
        f"- Raw rows: `{len(raw_rows)}`; pass: `{pass_count}`; fail: `{len(raw_rows) - pass_count}`.",
        f"- Tree hash mismatches: `{mismatch_count}`.",
        f"- Scheduler modes observed: `{','.join(scheduler_modes)}`.",
        f"- Comparable off/global summary pairs: `{comparable_pairs}`.",
        f"- Global scheduler cases with local scheduler summary CSV: `{global_metrics_count}/{len(global_rows)}`.",
        "- Scope: this matrix can support correctness, resume, backpressure and metrics explainability only; it is not 100G readiness evidence.",
        "",
        "## Summary CSV View",
        "",
        render_summary_table(summary_rows),
        "## Off/Global Comparison",
        "",
        render_comparison_table(summary_rows),
        "## Phase 3 Compression Metrics",
        "",
        render_compression_metrics(summary_rows),
        "## Slow Global Cases And Sidecars",
        "",
        render_slow_global_rows(raw_rows),
        "## Failures Or Hash Mismatches",
        "",
        render_failures(raw_rows),
        "## Preserved Boundaries",
        "",
        "- `--scheduler off` remains the default and baseline row.",
        "- Global scheduler rows use existing tree upload/download framed STOR/RETR.",
        "- Completion evidence remains tree manifest, per-file manifest and verified chunks; scheduler metrics are explanatory sidecars.",
        "- No daemon/API, Prometheus/HTTP, data-channel reuse, NIC/NUMA pinning, lossy compression, or 100G readiness claim is introduced.",
        "",
    ]
    return "\n".join(content)


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Phase 1 global scheduler tree matrix CSVs.")
    parser.add_argument("--raw-csv", action="append", required=True)
    parser.add_argument("--summary-csv", action="append", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    raw_rows = load_rows(args.raw_csv)
    summary_rows = load_rows(args.summary_csv)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        render(raw_rows, summary_rows, args.raw_csv + args.summary_csv),
        encoding="utf-8",
    )
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
