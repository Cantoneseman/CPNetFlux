#!/usr/bin/env python3
"""Analyze Phase 4K POSIX writeback strategy summaries."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


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


def load_rows(paths: list[str]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for text in paths:
        path = Path(text)
        with path.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                row["_source_csv"] = str(path)
                rows.append(row)
    return rows


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return "_No rows._\n"
    output = ["| " + " | ".join(headers) + " |"]
    output.append("| " + " | ".join(["---"] * len(headers)) + " |")
    output.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(output) + "\n"


def pct_delta(value: float, baseline: float) -> str:
    if baseline <= 0:
        return ""
    return f"{((value - baseline) / baseline) * 100.0:+.1f}%"


def baseline_key(row: dict[str, str]) -> tuple[str, ...]:
    return (
        row.get("direction", row.get("operation", "")),
        row.get("checksum_algorithm", ""),
        row.get("buffer_size", ""),
    )


def baseline_rows(rows: list[dict[str, str]]) -> dict[tuple[str, ...], dict[str, str]]:
    result: dict[tuple[str, ...], dict[str, str]] = {}
    for row in rows:
        if row.get("posix_write_strategy") != "auto":
            continue
        if row.get("file_io_buffer_size") != "0":
            continue
        if row.get("file_io_backend") not in ("", "posix"):
            continue
        key = baseline_key(row)
        result[key] = row
    return result


def label(row: dict[str, str]) -> str:
    direction = row.get("direction", row.get("operation", ""))
    checksum = row.get("checksum_algorithm", "")
    if checksum:
        checksum = f" {checksum}"
    return (
        f"{direction}{checksum} strategy={row.get('posix_write_strategy', '')}"
        f"->{row.get('posix_write_strategy_effective', '')}"
        f" fiobuf={row.get('file_io_buffer_size', '')}"
    )


def render_summary(title: str, rows: list[dict[str, str]]) -> str:
    baselines = baseline_rows(rows)
    table: list[list[str]] = []
    for row in sorted(
        rows,
        key=lambda item: (
            item.get("direction", item.get("operation", "")),
            item.get("checksum_algorithm", ""),
            item.get("posix_write_strategy", ""),
            item.get("file_io_buffer_size", ""),
        ),
    ):
        throughput = as_float(row, "throughput_gbps_median")
        baseline = baselines.get(baseline_key(row))
        baseline_throughput = as_float(baseline, "throughput_gbps_median") if baseline else 0.0
        temp_write = as_float(row, "temp_write_seconds_median") or as_float(
            row, "receiver_download_temp_write_seconds_median"
        )
        table.append(
            [
                label(row),
                row.get("repeat_count", row.get("case_count", "")),
                row.get("fail_count", ""),
                f"{throughput:.3f}" if throughput else "",
                pct_delta(throughput, baseline_throughput),
                row.get("throughput_gbps_min", ""),
                row.get("throughput_gbps_max", ""),
                f"{temp_write:.3f}" if temp_write else "",
                row.get("write_syscall_count_median", "")
                or row.get("receiver_write_syscall_count_median", ""),
                row.get("write_avg_bytes_per_syscall_median", "")
                or row.get("receiver_write_avg_bytes_per_syscall_median", ""),
            ]
        )
    return "\n".join(
        [
            f"## {title}",
            "",
            markdown_table(
                [
                    "case",
                    "repeat/cases",
                    "fail",
                    "median Gbps",
                    "vs baseline",
                    "min",
                    "max",
                    "temp/write s",
                    "write syscalls",
                    "avg bytes/syscall",
                ],
                table,
            ),
        ]
    )


def best_candidate(rows: list[dict[str, str]]) -> tuple[dict[str, str] | None, float]:
    candidates = [
        row
        for row in rows
        if row.get("fail_count", "0") in ("", "0")
        and row.get("posix_write_strategy") in ("direct", "coalesced")
    ]
    if not candidates:
        return None, 0.0
    baselines = baseline_rows(rows)
    best = None
    best_delta = -1e9
    for row in candidates:
        baseline = baselines.get(baseline_key(row))
        base = as_float(baseline, "throughput_gbps_median") if baseline else 0.0
        value = as_float(row, "throughput_gbps_median")
        if base <= 0:
            continue
        delta = (value - base) / base
        if delta > best_delta:
            best = row
            best_delta = delta
    return best, best_delta


def render(storage_rows: list[dict[str, str]], matrix_rows: list[dict[str, str]], inputs: list[str]) -> str:
    best_matrix, best_delta = best_candidate(matrix_rows)
    conclusion = [
        "- Defaults remain unchanged in Phase 4K: POSIX backend, `posix_write_strategy=auto`, `file_io_buffer_size=0`, full final verify, every-16 manifest flush.",
        "- A strategy should only be considered for a future default if private STOR and RETR medians improve by >=10%, variance is not worse, and write syscall metrics explain the gain.",
    ]
    if best_matrix is not None and best_delta >= 0.10:
        conclusion.append(
            f"- Best observed opt-in candidate is `{label(best_matrix)}` at {best_delta * 100.0:.1f}% over its baseline; keep opt-in until both directions satisfy the default gate."
        )
    else:
        conclusion.append(
            "- No opt-in POSIX write strategy meets the default gate from the provided summaries; keep current defaults and continue targeted diagnosis."
        )

    failed = [row for row in storage_rows + matrix_rows if row.get("fail_count", "0") not in ("", "0")]
    content = [
        "# Phase 4K POSIX Writeback Optimization",
        "",
        "## Inputs",
        "",
        *[f"- `{path}`" for path in inputs],
        "",
        render_summary("Storage Bench Median", storage_rows) if storage_rows else "## Storage Bench Median\n\n_No rows._\n",
        "",
        render_summary("GridFTP-like Private Matrix Median", matrix_rows)
        if matrix_rows
        else "## GridFTP-like Private Matrix Median\n\n_No rows._\n",
        "",
        "## Gate Conclusion",
        "",
        *conclusion,
        f"- Failed grouped rows: {len(failed)}.",
        "",
        "## Non-Goals Preserved",
        "",
        "- No raw FTP STOR/RETR, no network io_uring, and no default io_uring.",
        "- No default preallocate full, verified_chunks, final_only, or commit fsync.",
        "- No checksum, manifest, resume, or final verify semantic changes.",
        "",
    ]
    return "\n".join(content)


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Phase 4K POSIX writeback summaries.")
    parser.add_argument("--storage-summary-csv", action="append", default=[])
    parser.add_argument("--matrix-summary-csv", action="append", default=[])
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    storage_rows = load_rows(args.storage_summary_csv)
    matrix_rows = load_rows(args.matrix_summary_csv)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        render(storage_rows, matrix_rows, args.storage_summary_csv + args.matrix_summary_csv),
        encoding="utf-8",
    )
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
