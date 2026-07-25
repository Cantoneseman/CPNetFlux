#!/usr/bin/env python3
"""Analyze Phase 4I io_uring queue-depth heavy sampling summaries."""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from pathlib import Path


STORAGE_BASELINE_KEY = [
    "side",
    "operation",
    "bytes",
    "buffer_size",
    "preallocate",
    "file_io_advice",
]

MATRIX_BASELINE_KEY = [
    "mode",
    "direction",
    "bytes",
    "connections",
    "chunk_size",
    "buffer_size",
    "checksum_algorithm",
    "checksum_backend",
    "preallocate",
    "file_io_buffer_size",
    "file_io_advice",
    "final_verify_policy",
    "final_verify_policy_effective",
]

IO_URING_FIELDS = [
    "io_uring_submit_count",
    "io_uring_wait_count",
    "io_uring_completion_count",
    "io_uring_sqe_count",
    "io_uring_partial_completion_count",
    "io_uring_retry_count",
    "io_uring_avg_bytes_per_sqe",
]


def append_csv_arg(parser: argparse.ArgumentParser, name: str, help_text: str) -> None:
    parser.add_argument(name, action="append", default=[], help=help_text)


def split_paths(values: list[str]) -> list[Path]:
    paths: list[Path] = []
    for value in values:
        for part in value.split(","):
            item = part.strip()
            if item:
                paths.append(Path(item))
    return paths


def read_csv(paths: list[Path]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        with path.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                row["_source_csv"] = str(path)
                rows.append(row)
    return rows


def as_float(value: str | None) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def as_int(value: str | None) -> int:
    if value is None or value == "":
        return 0
    try:
        return int(float(value))
    except ValueError:
        return 0


def fmt(value: float | None, digits: int = 3) -> str:
    if value is None:
        return ""
    return f"{value:.{digits}f}"


def pct(candidate: float | None, baseline: float | None) -> float | None:
    if candidate is None or baseline is None or baseline == 0:
        return None
    return ((candidate - baseline) / baseline) * 100.0


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return "_No rows._"
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


def row_key(row: dict[str, str], fields: list[str]) -> tuple[str, ...]:
    return tuple(row.get(field, "") for field in fields)


def baseline_lookup(rows: list[dict[str, str]], key_fields: list[str]) -> dict[tuple[str, ...], dict[str, str]]:
    result: dict[tuple[str, ...], dict[str, str]] = {}
    for row in rows:
        if (
            row.get("file_io_backend") == "posix"
            and row.get("file_io_queue_depth") == "1"
            and row.get("file_io_batch_size") == "1"
        ):
            result[row_key(row, key_fields)] = row
    return result


def instability_label(row: dict[str, str], *, count_field: str) -> str:
    problems: list[str] = []
    fail_count = as_int(row.get("fail_count"))
    if fail_count:
        problems.append(f"fail={fail_count}")
    if as_int(row.get(count_field)) < 3:
        problems.append(f"{count_field}<3")
    median = as_float(row.get("throughput_gbps_median"))
    minimum = as_float(row.get("throughput_gbps_min"))
    maximum = as_float(row.get("throughput_gbps_max"))
    if median and minimum is not None and maximum is not None and ((maximum - minimum) / median) > 0.25:
        problems.append("spread>25%")
    return ", ".join(problems)


def storage_table(rows: list[dict[str, str]]) -> str:
    baseline = baseline_lookup(rows, STORAGE_BASELINE_KEY)
    output: list[list[str]] = []
    for row in sorted(
        rows,
        key=lambda item: (
            row_key(item, STORAGE_BASELINE_KEY),
            item.get("file_io_backend", ""),
            int(item.get("file_io_queue_depth", "0") or 0),
            int(item.get("file_io_batch_size", "0") or 0),
        ),
    ):
        base = baseline.get(row_key(row, STORAGE_BASELINE_KEY), {})
        row_median = as_float(row.get("throughput_gbps_median"))
        base_median = as_float(base.get("throughput_gbps_median"))
        output.append(
            [
                row.get("side", ""),
                row.get("operation", ""),
                row.get("buffer_size", ""),
                row.get("file_io_backend", ""),
                row.get("file_io_queue_depth", ""),
                row.get("file_io_batch_size", ""),
                row.get("case_count", ""),
                row.get("fail_count", ""),
                row.get("throughput_gbps_median", ""),
                row.get("throughput_gbps_min", ""),
                row.get("throughput_gbps_max", ""),
                fmt(pct(row_median, base_median), 2),
                row.get("io_uring_sqe_count_median", ""),
                row.get("io_uring_wait_count_median", ""),
                instability_label(row, count_field="case_count"),
            ]
        )
    return markdown_table(
        [
            "side",
            "op",
            "buffer",
            "backend",
            "qd",
            "batch",
            "n",
            "fail",
            "median Gbps",
            "min",
            "max",
            "vs posix %",
            "SQE median",
            "wait median",
            "flags",
        ],
        output,
    )


def matrix_table(rows: list[dict[str, str]]) -> str:
    baseline = baseline_lookup(rows, MATRIX_BASELINE_KEY)
    output: list[list[str]] = []
    for row in sorted(
        rows,
        key=lambda item: (
            row_key(item, MATRIX_BASELINE_KEY),
            item.get("file_io_backend", ""),
            int(item.get("file_io_queue_depth", "0") or 0),
            int(item.get("file_io_batch_size", "0") or 0),
        ),
    ):
        base = baseline.get(row_key(row, MATRIX_BASELINE_KEY), {})
        row_median = as_float(row.get("throughput_gbps_median"))
        base_median = as_float(base.get("throughput_gbps_median"))
        output.append(
            [
                row.get("direction", ""),
                row.get("checksum_algorithm", ""),
                row.get("file_io_backend", ""),
                row.get("file_io_queue_depth", ""),
                row.get("file_io_batch_size", ""),
                row.get("repeat_count", ""),
                row.get("fail_count", ""),
                row.get("throughput_gbps_median", ""),
                row.get("throughput_gbps_min", ""),
                row.get("throughput_gbps_max", ""),
                fmt(pct(row_median, base_median), 2),
                row.get("io_uring_sqe_count_median", ""),
                row.get("io_uring_wait_count_median", ""),
                instability_label(row, count_field="repeat_count"),
            ]
        )
    return markdown_table(
        [
            "direction",
            "checksum",
            "backend",
            "qd",
            "batch",
            "n",
            "fail",
            "median Gbps",
            "min",
            "max",
            "vs posix %",
            "SQE median",
            "wait median",
            "flags",
        ],
        output,
    )


def gate_decision(storage_rows: list[dict[str, str]], matrix_rows: list[dict[str, str]]) -> str:
    matrix_baseline = baseline_lookup(matrix_rows, MATRIX_BASELINE_KEY)
    best_by_direction: dict[tuple[str, str], float] = {}
    unstable = False
    incomplete = False
    for row in matrix_rows:
        if row.get("file_io_backend") != "io_uring":
            continue
        flags = instability_label(row, count_field="repeat_count")
        if "repeat_count<3" in flags or as_int(row.get("fail_count")):
            incomplete = True
        if flags:
            unstable = True
        base = matrix_baseline.get(row_key(row, MATRIX_BASELINE_KEY), {})
        delta = pct(as_float(row.get("throughput_gbps_median")), as_float(base.get("throughput_gbps_median")))
        if delta is None:
            continue
        key = (row.get("direction", ""), row.get("checksum_algorithm", ""))
        best_by_direction[key] = max(best_by_direction.get(key, -10_000.0), delta)

    required_keys = [("stor", "crc32c"), ("stor", "none"), ("retr", "crc32c"), ("retr", "none")]
    enough_matrix_gain = all(best_by_direction.get(key, -10_000.0) >= 10.0 for key in required_keys)

    storage_baseline = baseline_lookup(storage_rows, STORAGE_BASELINE_KEY)
    storage_gain = False
    for row in storage_rows:
        if row.get("file_io_backend") != "io_uring":
            continue
        base = storage_baseline.get(row_key(row, STORAGE_BASELINE_KEY), {})
        delta = pct(as_float(row.get("throughput_gbps_median")), as_float(base.get("throughput_gbps_median")))
        if delta is not None and delta >= 10.0:
            storage_gain = True
            break

    if enough_matrix_gain and storage_gain and not unstable:
        return (
            "Gate recommendation: continue to Phase 4J persistent-ring/deeper async pipeline design. "
            "Do not change defaults yet; keep io_uring opt-in until another repeat matrix confirms the gain."
        )
    if incomplete:
        return (
            "Gate recommendation: do not deepen io_uring yet. Some samples are incomplete or failed; repeat or narrow "
            "the matrix before changing implementation strategy. Defaults remain POSIX."
        )
    if unstable:
        return (
            "Gate recommendation: do not deepen io_uring yet. The repeat=3 samples completed, but several groups are "
            "too volatile and the gains do not hold across STOR/RETR and crc32c/none. Defaults remain POSIX; next work "
            "should return to POSIX storage/writeback, checksum, and final verify path analysis."
        )
    return (
        "Gate recommendation: pause io_uring batching as a default-path candidate. Return to POSIX storage/writeback, "
        "final verify, and checksum path analysis unless a narrower future experiment shows consistent median gains. "
        "Defaults remain POSIX."
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Phase 4I queue-depth summaries.")
    append_csv_arg(parser, "--storage-summary-csv", "storage bench summary CSV path; may be repeated or comma-separated")
    append_csv_arg(parser, "--matrix-summary-csv", "GridFTP private matrix summary CSV path; may be repeated or comma-separated")
    parser.add_argument("--output", default="docs/perf/PHASE4I_HEAVY_QUEUE_DEPTH_GATE.md")
    args = parser.parse_args()

    storage_paths = split_paths(args.storage_summary_csv)
    matrix_paths = split_paths(args.matrix_summary_csv)
    storage_rows = read_csv(storage_paths)
    matrix_rows = read_csv(matrix_paths)

    report = [
        "# Phase 4I io_uring Queue-Depth Heavy Gate",
        "",
        "Phase 4I uses repeat=3 1GiB storage and GridFTP-like private matrix summaries. "
        "All comparisons use median throughput, not best single runs. Defaults remain `file_io_backend=posix`.",
        "",
        "## Inputs",
        "",
        "- Storage summary CSV:",
        *[f"  - `{path}`" for path in storage_paths],
        "- GridFTP private matrix summary CSV:",
        *[f"  - `{path}`" for path in matrix_paths],
        "",
        "## Storage Bench Summary",
        "",
        storage_table(storage_rows),
        "",
        "## GridFTP-like Private Matrix Summary",
        "",
        matrix_table(matrix_rows),
        "",
        "## Gate Decision",
        "",
        gate_decision(storage_rows, matrix_rows),
        "",
        "## Defaults",
        "",
        "- `file_io_backend=posix` remains the default.",
        "- Network remains epoll; file-IO-only io_uring stays opt-in.",
        "- `final_verify_policy=full`, `preallocate=off`, and `verified_chunks` opt-in semantics remain unchanged.",
    ]
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"report={output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
