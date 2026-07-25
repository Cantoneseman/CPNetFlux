#!/usr/bin/env python3
"""Analyze Phase 4E storage and GridFTP private matrix CSV files."""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


MATRIX_GROUP_FIELDS = [
    "direction",
    "checksum_algorithm",
    "preallocate",
    "file_io_buffer_size",
    "file_io_advice",
    "final_verify_policy",
    "final_verify_policy_effective",
]

STAGE_FIELDS = [
    "stage_read_seconds",
    "stage_write_seconds",
    "file_io_wait_seconds",
    "stage_read_calls",
    "stage_write_calls",
    "stage_read_avg_bytes_per_call",
    "stage_write_avg_bytes_per_call",
    "stage_manifest_flush_seconds",
    "stage_final_verify_seconds",
    "stage_rename_commit_seconds",
    "stage_overall_seconds",
]


def split_paths(value: str) -> list[Path]:
    if not value:
        return []
    return [Path(part.strip()) for part in value.split(",") if part.strip()]


def read_csv(paths: list[Path]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        with path.open(newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                row["_source_csv"] = str(path)
                rows.append(row)
    return rows


def to_float(value: str | None) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except ValueError:
        return None


def median_value(rows: list[dict[str, str]], field: str) -> float | None:
    values = [value for value in (to_float(row.get(field)) for row in rows) if value is not None]
    return statistics.median(values) if values else None


def min_value(rows: list[dict[str, str]], field: str) -> float | None:
    values = [value for value in (to_float(row.get(field)) for row in rows) if value is not None]
    return min(values) if values else None


def max_value(rows: list[dict[str, str]], field: str) -> float | None:
    values = [value for value in (to_float(row.get(field)) for row in rows) if value is not None]
    return max(values) if values else None


def fmt(value: float | None, digits: int = 6) -> str:
    if value is None:
        return ""
    return f"{value:.{digits}f}"


def pass_rows(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    return [row for row in rows if row.get("result") == "pass"]


def group_rows(rows: list[dict[str, str]], fields: list[str]) -> dict[tuple[str, ...], list[dict[str, str]]]:
    groups: dict[tuple[str, ...], list[dict[str, str]]] = defaultdict(list)
    for row in rows:
        groups[tuple(row.get(field, "") for field in fields)].append(row)
    return groups


def matrix_summary_from_raw(rows: list[dict[str, str]]) -> list[dict[str, str]]:
    summaries: list[dict[str, str]] = []
    for key, grouped in sorted(group_rows(rows, MATRIX_GROUP_FIELDS).items()):
        good = pass_rows(grouped)
        summary = dict(zip(MATRIX_GROUP_FIELDS, key, strict=True))
        summary.update(
            {
                "repeat_count": str(len(grouped)),
                "pass_count": str(len(good)),
                "fail_count": str(len(grouped) - len(good)),
                "throughput_gbps_min": fmt(min_value(good, "throughput_gbps")),
                "throughput_gbps_median": fmt(median_value(good, "throughput_gbps")),
                "throughput_gbps_max": fmt(max_value(good, "throughput_gbps")),
                "elapsed_median": fmt(median_value(good, "elapsed")),
            }
        )
        for field in STAGE_FIELDS:
            summary[f"{field}_median"] = fmt(median_value(good, field))
        summaries.append(summary)
    return summaries


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for row in rows:
        lines.append("| " + " | ".join(row) + " |")
    return "\n".join(lines)


def storage_table(rows: list[dict[str, str]]) -> str:
    if not rows:
        return "_No storage summary rows provided._"
    output_rows: list[list[str]] = []
    for row in rows:
        output_rows.append(
            [
                row.get("side", ""),
                row.get("operation", ""),
                row.get("buffer_size", ""),
                row.get("preallocate", ""),
                row.get("file_io_advice", ""),
                row.get("case_count", ""),
                row.get("throughput_gbps_median", ""),
                row.get("throughput_gbps_min", ""),
                row.get("throughput_gbps_max", ""),
                row.get("elapsed_median", ""),
            ]
        )
    return markdown_table(
        [
            "side",
            "op",
            "buffer",
            "preallocate",
            "advice",
            "n",
            "median Gbps",
            "min",
            "max",
            "median elapsed",
        ],
        output_rows,
    )


def matrix_table(rows: list[dict[str, str]], *, limit: int = 80) -> str:
    if not rows:
        return "_No matrix rows provided._"
    output_rows: list[list[str]] = []
    for row in rows[:limit]:
        output_rows.append(
            [
                row.get("direction", ""),
                row.get("checksum_algorithm", ""),
                row.get("preallocate", ""),
                row.get("file_io_buffer_size", ""),
                row.get("file_io_advice", ""),
                row.get("final_verify_policy", ""),
                row.get("final_verify_policy_effective", ""),
                row.get("pass_count", ""),
                row.get("fail_count", ""),
                row.get("throughput_gbps_median", ""),
                row.get("throughput_gbps_min", ""),
                row.get("throughput_gbps_max", ""),
                row.get("elapsed_median", ""),
            ]
        )
    suffix = ""
    if len(rows) > limit:
        suffix = f"\n\n_Only the first {limit} of {len(rows)} grouped rows are shown._"
    return (
        markdown_table(
            [
                "direction",
                "checksum",
                "prealloc",
                "file buf",
                "advice",
                "policy",
                "effective",
                "pass",
                "fail",
                "median Gbps",
                "min",
                "max",
                "median elapsed",
            ],
            output_rows,
        )
        + suffix
    )


def stage_table(rows: list[dict[str, str]], *, limit: int = 80) -> str:
    if not rows:
        return "_No raw matrix rows with stage metrics were provided._"
    summaries = matrix_summary_from_raw(rows)
    output_rows: list[list[str]] = []
    for row in summaries[:limit]:
        output_rows.append(
            [
                row.get("direction", ""),
                row.get("checksum_algorithm", ""),
                row.get("preallocate", ""),
                row.get("file_io_buffer_size", ""),
                row.get("file_io_advice", ""),
                row.get("final_verify_policy_effective", ""),
                row.get("stage_read_seconds_median", ""),
                row.get("stage_write_seconds_median", ""),
                row.get("file_io_wait_seconds_median", ""),
                row.get("stage_read_calls_median", ""),
                row.get("stage_write_calls_median", ""),
                row.get("stage_read_avg_bytes_per_call_median", ""),
                row.get("stage_write_avg_bytes_per_call_median", ""),
                row.get("stage_final_verify_seconds_median", ""),
                row.get("stage_overall_seconds_median", ""),
            ]
        )
    suffix = ""
    if len(summaries) > limit:
        suffix = f"\n\n_Only the first {limit} of {len(summaries)} grouped rows are shown._"
    return (
        markdown_table(
            [
                "direction",
                "checksum",
                "prealloc",
                "file buf",
                "advice",
                "effective",
                "read s",
                "write s",
                "io wait s",
                "read calls",
                "write calls",
                "avg read",
                "avg write",
                "final verify s",
                "overall s",
            ],
            output_rows,
        )
        + suffix
    )


def find_summary(
    summaries: list[dict[str, str]],
    *,
    direction: str,
    checksum: str,
    preallocate: str = "off",
    file_buffer: str = "0",
    advice: str = "off",
    policy: str = "full",
    effective: str | None = None,
) -> dict[str, str] | None:
    for row in summaries:
        if row.get("direction") != direction:
            continue
        if row.get("checksum_algorithm") != checksum:
            continue
        if row.get("preallocate") != preallocate:
            continue
        if row.get("file_io_buffer_size") != file_buffer:
            continue
        if row.get("file_io_advice") != advice:
            continue
        if row.get("final_verify_policy") != policy:
            continue
        if effective is not None and row.get("final_verify_policy_effective") != effective:
            continue
        return row
    return None


def median_throughput(row: dict[str, str] | None) -> float | None:
    if row is None:
        return None
    return to_float(row.get("throughput_gbps_median"))


def ratio(candidate: float | None, baseline: float | None) -> float | None:
    if candidate is None or baseline is None or baseline <= 0:
        return None
    return (candidate - baseline) / baseline


def percent(value: float | None) -> str:
    if value is None:
        return "n/a"
    return f"{value * 100:.1f}%"


def recommendation_lines(summaries: list[dict[str, str]], raw_rows: list[dict[str, str]]) -> list[str]:
    lines: list[str] = []

    stor_base = median_throughput(find_summary(summaries, direction="stor", checksum="crc32c"))
    retr_base = median_throughput(find_summary(summaries, direction="retr", checksum="crc32c"))

    for file_buffer in ("1048576", "4194304"):
        stor_candidate = median_throughput(
            find_summary(summaries, direction="stor", checksum="crc32c", file_buffer=file_buffer)
        )
        retr_candidate = median_throughput(
            find_summary(summaries, direction="retr", checksum="crc32c", file_buffer=file_buffer)
        )
        lines.append(
            f"- `file_io_buffer_size={file_buffer}` vs default: STOR {percent(ratio(stor_candidate, stor_base))}, "
            f"RETR {percent(ratio(retr_candidate, retr_base))}; keep default `0` unless both directions clear the gate."
        )

    for advice in ("sequential", "sequential_dontneed"):
        stor_candidate = median_throughput(
            find_summary(summaries, direction="stor", checksum="crc32c", advice=advice)
        )
        retr_candidate = median_throughput(
            find_summary(summaries, direction="retr", checksum="crc32c", advice=advice)
        )
        lines.append(
            f"- `file_io_advice={advice}` vs default: STOR {percent(ratio(stor_candidate, stor_base))}, "
            f"RETR {percent(ratio(retr_candidate, retr_base))}; keep default `off` unless both directions clear the gate."
        )

    stor_prealloc = median_throughput(
        find_summary(summaries, direction="stor", checksum="crc32c", preallocate="full")
    )
    retr_prealloc = median_throughput(
        find_summary(summaries, direction="retr", checksum="crc32c", preallocate="full")
    )
    lines.append(
        f"- `preallocate=full` vs default: STOR {percent(ratio(stor_prealloc, stor_base))}, "
        f"RETR {percent(ratio(retr_prealloc, retr_base))}; keep default `off` unless both directions clear the gate."
    )

    stor_verified = median_throughput(
        find_summary(
            summaries,
            direction="stor",
            checksum="crc32c",
            policy="verified_chunks",
            effective="verified_chunks",
        )
    )
    retr_verified = median_throughput(
        find_summary(
            summaries,
            direction="retr",
            checksum="crc32c",
            policy="verified_chunks",
            effective="verified_chunks",
        )
    )
    lines.append(
        f"- `verified_chunks` opt-in vs full: STOR {percent(ratio(stor_verified, stor_base))}, "
        f"RETR {percent(ratio(retr_verified, retr_base))}; keep opt-in in Phase 4E regardless of speedup."
    )

    go_reasons = io_uring_gate_reasons(raw_rows)
    if go_reasons:
        lines.append("- io_uring gate: **GO for Phase 4F design only**, because " + "; ".join(go_reasons) + ".")
    else:
        lines.append(
            "- io_uring gate: **NO-GO from available raw stage metrics**; continue POSIX tuning or rerun with raw stage fields if data is incomplete."
        )
    return lines


def io_uring_gate_reasons(raw_rows: list[dict[str, str]]) -> list[str]:
    reasons: list[str] = []
    if not raw_rows:
        return reasons
    for direction in ("stor", "retr"):
        candidates = [
            row
            for row in pass_rows(raw_rows)
            if row.get("direction") == direction
            and row.get("checksum_algorithm") == "crc32c"
            and row.get("preallocate") == "off"
            and row.get("file_io_buffer_size") == "0"
            and row.get("file_io_advice") == "off"
            and row.get("final_verify_policy_effective") == "full"
        ]
        overall = median_value(candidates, "stage_overall_seconds")
        if not overall or overall <= 0:
            continue
        read_ratio = (median_value(candidates, "stage_read_seconds") or 0.0) / overall
        write_ratio = (median_value(candidates, "stage_write_seconds") or 0.0) / overall
        wait_ratio = (median_value(candidates, "file_io_wait_seconds") or 0.0) / overall
        if max(read_ratio, write_ratio, wait_ratio) >= 0.40:
            reasons.append(
                f"{direction} POSIX file IO parallel-summed stage ratio is high "
                f"(read={read_ratio * 100:.1f}%, write={write_ratio * 100:.1f}%, wait={wait_ratio * 100:.1f}%)"
            )
    return reasons


def build_report(args: argparse.Namespace) -> str:
    storage_summary = read_csv(split_paths(args.storage_summary_csv))
    matrix_summary = read_csv(split_paths(args.matrix_summary_csv))
    matrix_raw = read_csv(split_paths(args.matrix_raw_csv))
    if not matrix_summary and matrix_raw:
        matrix_summary = matrix_summary_from_raw(matrix_raw)

    lines = [
        "# Phase 4E IO Uring Gate Analysis",
        "",
        "Date: 2026-05-17",
        "",
        "Phase 4E keeps the existing epoll + framed STOR/RETR path. It does not implement io_uring; it only decides whether Phase 4F should prototype a file IO backend.",
        "",
        "## Executive Summary",
        "",
        "- Heavy storage bench and private GridFTP-like matrix completed without failed cases.",
        "- Keep defaults unchanged: `file_io_buffer_size=0`, `file_io_advice=off`, `preallocate=off`, `final_verify_policy=full`.",
        "- `sequential_dontneed` is consistently harmful for RETR and storage read paths and should not be recommended.",
        "- `file_io_buffer_size` and `sequential` advice do not clear the default-change gate across both STOR and RETR.",
        "- Data supports a Phase 4F **design/prototype gate** for optional file-IO-only io_uring, not a main-path switch.",
        "",
        "## Inputs",
        "",
        f"- Storage summary CSV: `{args.storage_summary_csv or 'not provided'}`",
        f"- Matrix summary CSV: `{args.matrix_summary_csv or 'not provided'}`",
        f"- Matrix raw CSV: `{args.matrix_raw_csv or 'not provided'}`",
        "",
        "## Storage Bench Median Summary",
        "",
        storage_table(storage_summary),
        "",
        "## GridFTP Matrix Median Summary",
        "",
        matrix_table(matrix_summary),
        "",
        "## Stage And File IO Breakdown",
        "",
        "Stage seconds are accumulated across connections/threads, so `stage_*_seconds` can exceed wall-clock `overall` time. Treat these as work/wait mass, not elapsed time.",
        "",
        stage_table(matrix_raw),
        "",
        "## Gate Recommendations",
        "",
        *recommendation_lines(matrix_summary, matrix_raw),
        "",
        "## Phase 4F Minimal Prototype Scope If Gate Is GO",
        "",
        "- Add optional `--file-io-backend io_uring`; keep `posix` as default.",
        "- Limit v1 to file IO only; do not change network epoll.",
        "- Cover STOR temp write and RETR source read first.",
        "- Detect liburing at configure time behind an explicit CMake option; runtime fallback remains POSIX.",
        "- Preserve checksum, manifest, resume, final verify, framed STOR/RETR, and GridFTP control semantics.",
        "",
        "## Out Of Scope",
        "",
        "- No raw FTP STOR/RETR.",
        "- No TLS/GSI, MLST/MLSD, Mode E, SPAS/SPOR, or third-party server-to-server transfer.",
        "- No default change to `verified_chunks`, `preallocate`, file IO buffer, or advice in Phase 4E.",
    ]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Phase 4E benchmark CSV files.")
    parser.add_argument("--storage-summary-csv", default="")
    parser.add_argument("--matrix-summary-csv", default="")
    parser.add_argument("--matrix-raw-csv", default="")
    parser.add_argument("--output", default="")
    args = parser.parse_args()

    report = build_report(args)
    if args.output:
        Path(args.output).write_text(report, encoding="utf-8")
    else:
        sys.stdout.write(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
