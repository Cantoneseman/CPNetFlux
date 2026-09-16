#!/usr/bin/env python3
"""Analyze Phase 5B tree dataset matrix summaries."""

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
            for row in csv.DictReader(handle):
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


def render(rows: list[dict[str, str]], inputs: list[str]) -> str:
    table: list[list[str]] = []
    for row in sorted(
        rows,
        key=lambda item: (
            item.get("dataset", ""),
            item.get("direction", ""),
            item.get("resume", ""),
            item.get("checksum_algorithm", ""),
            int(item.get("file_parallelism", "0") or "0"),
        ),
    ):
        table.append(
            [
                row.get("dataset", ""),
                row.get("direction", ""),
                row.get("resume", ""),
                row.get("checksum_algorithm", ""),
                row.get("file_parallelism", ""),
                row.get("repeat_count", ""),
                row.get("fail_count", ""),
                row.get("tree_hash_mismatch_count", ""),
                row.get("file_count", ""),
                row.get("total_bytes", ""),
                row.get("throughput_gbps_median", ""),
                row.get("throughput_gbps_min", ""),
                row.get("throughput_gbps_max", ""),
                row.get("elapsed_seconds_median", ""),
            ]
        )
    failures = [row for row in rows if row.get("fail_count", "0") not in ("", "0")]
    mismatches = [row for row in rows if row.get("tree_hash_mismatch_count", "0") not in ("", "0")]
    best_by_direction: dict[str, dict[str, str]] = {}
    for row in rows:
        if row.get("fail_count", "0") not in ("", "0"):
            continue
        direction = row.get("direction", "")
        current = best_by_direction.get(direction)
        if current is None or as_float(row, "throughput_gbps_median") > as_float(current, "throughput_gbps_median"):
            best_by_direction[direction] = row
    recommendations = [
        "- Defaults remain unchanged: file-level parallelism defaults to 1 and single-file transfer defaults stay POSIX/full-verify/every_n_chunks.",
        "- Directory transfer remains alpha orchestration, not rsync: no permissions, owner, xattr, ACL, or empty-directory preservation.",
    ]
    for direction, row in sorted(best_by_direction.items()):
        recommendations.append(
            f"- Best passing {direction} row in this input: dataset={row.get('dataset')} checksum={row.get('checksum_algorithm')} file_parallelism={row.get('file_parallelism')} median={row.get('throughput_gbps_median')} Gbps. Treat as opt-in guidance only."
        )
    if failures or mismatches:
        recommendations.append(
            f"- Failed grouped rows={len(failures)}, tree hash mismatch grouped rows={len(mismatches)}; do not use failed rows for tuning conclusions."
        )
    else:
        recommendations.append("- All grouped rows passed with matching tree hashes in the supplied summaries.")
    return "\n".join(
        [
            "# Phase 5B Tree Dataset Matrix",
            "",
            "## Inputs",
            "",
            *[f"- `{path}`" for path in inputs],
            "",
            "## Median Summary",
            "",
            markdown_table(
                [
                    "dataset",
                    "direction",
                    "resume",
                    "checksum",
                    "file parallelism",
                    "repeat",
                    "fail",
                    "hash mismatch",
                    "files",
                    "bytes",
                    "median Gbps",
                    "min Gbps",
                    "max Gbps",
                    "median seconds",
                ],
                table,
            ),
            "",
            "## Recommendation",
            "",
            *recommendations,
            "",
            "## Boundaries",
            "",
            "- No raw FTP recursive transfer, no MLST/MLSD, no TLS/GSI/production auth, and no third-party transfer.",
            "- Each file still uses the existing CPNetFlux framed STOR/RETR path and per-file manifest/verified_chunks semantics.",
            "- Changed-file handling remains fail-safe: changed files are marked and the transfer exits nonzero.",
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Phase 5B tree private matrix summaries.")
    parser.add_argument("--matrix-summary-csv", action="append", default=[])
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    rows = load_rows(args.matrix_summary_csv)
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render(rows, args.matrix_summary_csv), encoding="utf-8")
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
