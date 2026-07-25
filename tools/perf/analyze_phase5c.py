#!/usr/bin/env python3
"""Analyze Phase 5C tree alpha hardening matrix summaries."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def load_rows(paths: list[str]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for text in paths:
        path = Path(text)
        with path.open("r", encoding="utf-8", newline="") as handle:
            for row in csv.DictReader(handle):
                row["_source_csv"] = str(path)
                rows.append(row)
    return rows


def table(headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return "_No rows._\n"
    lines = ["| " + " | ".join(headers) + " |", "| " + " | ".join(["---"] * len(headers)) + " |"]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(lines) + "\n"


def render(rows: list[dict[str, str]], inputs: list[str]) -> str:
    matrix_rows: list[list[str]] = []
    for row in sorted(
        rows,
        key=lambda item: (
            item.get("dataset", ""),
            item.get("direction", ""),
            item.get("checksum_algorithm", ""),
            int(item.get("file_parallelism", "0") or "0"),
        ),
    ):
        matrix_rows.append(
            [
                row.get("dataset", ""),
                row.get("direction", ""),
                row.get("checksum_algorithm", ""),
                row.get("file_parallelism", ""),
                row.get("repeat_count", ""),
                row.get("fail_count", ""),
                row.get("tree_hash_mismatch_count", ""),
                row.get("file_count", ""),
                row.get("total_bytes", ""),
                row.get("completed_files", ""),
                row.get("bytes_transferred", ""),
                row.get("throughput_gbps_median", ""),
                row.get("throughput_gbps_min", ""),
                row.get("throughput_gbps_max", ""),
            ]
        )
    failures = [row for row in rows if row.get("fail_count", "0") not in ("", "0")]
    mismatches = [row for row in rows if row.get("tree_hash_mismatch_count", "0") not in ("", "0")]
    return "\n".join(
        [
            "# Phase 5C Tree Alpha Hardening",
            "",
            "## Inputs",
            "",
            *[f"- `{path}`" for path in inputs],
            "",
            "## Dataset Matrix",
            "",
            table(
                [
                    "dataset",
                    "direction",
                    "checksum",
                    "file parallelism",
                    "repeat",
                    "fail",
                    "hash mismatch",
                    "files",
                    "bytes",
                    "completed",
                    "transferred",
                    "median Gbps",
                    "min Gbps",
                    "max Gbps",
                ],
                matrix_rows,
            ),
            "",
            "## Phase 5C Checks",
            "",
            "- Tree CLI JSON summary is enabled in the private matrix and the raw CSV records the summary path plus completed/skipped/failed/changed counts.",
            "- Edge-case smoke covers special-character paths, deep directories, many small files, empty-directory non-preservation, symlink rejection, and same-size mtime drift fail-safe.",
            "- Release manifest freshness is checked locally after the final manifest is written, before remote sync/verify.",
            "",
            "## Recommendation",
            "",
            "- Defaults remain unchanged: file_parallelism defaults to 1 and single-file transfer defaults remain POSIX/full-verify/every_n_chunks.",
            "- Directory transfer remains alpha orchestration, not rsync. Permissions, owner, xattr, ACL, and empty directories are not preserved.",
            (
                "- All supplied grouped rows passed with matching tree hashes."
                if not failures and not mismatches
                else f"- Failures={len(failures)}, hash mismatch groups={len(mismatches)}; keep recommendations conservative."
            ),
            "",
        ]
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Phase 5C tree private matrix summaries.")
    parser.add_argument("--matrix-summary-csv", action="append", default=[])
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(render(load_rows(args.matrix_summary_csv), args.matrix_summary_csv), encoding="utf-8")
    print(f"wrote {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
