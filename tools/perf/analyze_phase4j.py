#!/usr/bin/env python3
"""Analyze Phase 4J POSIX pipeline diagnosis summaries."""

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
    for path_text in paths:
        path = Path(path_text)
        with path.open("r", encoding="utf-8", newline="") as handle:
            reader = csv.DictReader(handle)
            for row in reader:
                row["_source_csv"] = str(path)
                rows.append(row)
    return rows


def percent(part: float, total: float) -> str:
    if total <= 0:
        return ""
    return f"{(part / total) * 100.0:.1f}%"


def pct_delta(value: float, baseline: float) -> str:
    if baseline <= 0:
        return ""
    return f"{((value - baseline) / baseline) * 100.0:+.1f}%"


def markdown_table(headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return "_No rows._\n"
    output = ["| " + " | ".join(headers) + " |"]
    output.append("| " + " | ".join(["---"] * len(headers)) + " |")
    output.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(output) + "\n"


def key_label(row: dict[str, str]) -> str:
    return (
        f"{row.get('direction', '')} {row.get('checksum_algorithm', '')} "
        f"flush={row.get('manifest_flush_policy', '')}/"
        f"{row.get('manifest_flush_interval_chunks', '')} "
        f"fv={row.get('final_verify_policy', '')}->"
        f"{row.get('final_verify_policy_effective', '')} "
        f"fiobuf={row.get('file_io_buffer_size', '')}"
    )


def stage_breakdown(row: dict[str, str]) -> tuple[str, float, list[tuple[str, float]]]:
    direction = row.get("direction", "")
    elapsed = as_float(row, "elapsed_median")
    if direction.startswith("stor"):
        stages = [
            ("data receive", as_float(row, "data_receive_seconds_median")),
            ("temp write", as_float(row, "temp_write_seconds_median")),
            ("checksum", as_float(row, "checksum_seconds_median")),
            ("manifest", as_float(row, "manifest_flush_seconds_median")),
            ("final verify", as_float(row, "final_verify_seconds_median")),
            ("finalize", as_float(row, "finalize_rename_seconds_median")),
        ]
    else:
        stages = [
            ("sender read", as_float(row, "sender_source_read_seconds_median")),
            ("network send", as_float(row, "sender_network_send_seconds_median")),
            ("sender checksum", as_float(row, "sender_checksum_seconds_median")),
            ("download write", as_float(row, "receiver_download_temp_write_seconds_median")),
            ("receiver manifest", as_float(row, "receiver_manifest_flush_seconds_median")),
            ("receiver final verify", as_float(row, "receiver_final_verify_seconds_median")),
            ("receiver finalize", as_float(row, "receiver_finalize_rename_seconds_median")),
        ]
    return direction, elapsed, stages


def dominant_stage(row: dict[str, str]) -> tuple[str, float]:
    _, _, stages = stage_breakdown(row)
    if not stages:
        return "", 0.0
    stage_total = sum(value for _, value in stages)
    name, value = max(stages, key=lambda item: item[1])
    ratio = (value / stage_total) if stage_total > 0 else 0.0
    return name, ratio


def best_rows_by_direction(rows: list[dict[str, str]]) -> dict[str, dict[str, str]]:
    best: dict[str, dict[str, str]] = {}
    for row in rows:
        if row.get("fail_count", "0") not in ("", "0"):
            continue
        direction = row.get("direction", "")
        throughput = as_float(row, "throughput_gbps_median")
        if direction not in best or throughput > as_float(best[direction], "throughput_gbps_median"):
            best[direction] = row
    return best


def baseline_rows(rows: list[dict[str, str]]) -> dict[tuple[str, str], dict[str, str]]:
    result: dict[tuple[str, str], dict[str, str]] = {}
    for row in rows:
        if row.get("manifest_flush_policy") != "every_n_chunks":
            continue
        if row.get("manifest_flush_interval_chunks") not in ("16", ""):
            continue
        if row.get("final_verify_policy") != "full":
            continue
        if row.get("file_io_backend") != "posix":
            continue
        key = (row.get("direction", ""), row.get("checksum_algorithm", ""))
        result[key] = row
    return result


def render(rows: list[dict[str, str]], input_paths: list[str]) -> str:
    baselines = baseline_rows(rows)
    table_rows: list[list[str]] = []
    for row in sorted(
        rows,
        key=lambda item: (
            item.get("direction", ""),
            item.get("checksum_algorithm", ""),
            item.get("manifest_flush_policy", ""),
            item.get("manifest_flush_interval_chunks", ""),
            item.get("final_verify_policy", ""),
            item.get("file_io_buffer_size", ""),
        ),
    ):
        throughput = as_float(row, "throughput_gbps_median")
        baseline = baselines.get((row.get("direction", ""), row.get("checksum_algorithm", "")))
        baseline_throughput = as_float(baseline, "throughput_gbps_median") if baseline else 0.0
        dominant, ratio = dominant_stage(row)
        table_rows.append(
            [
                key_label(row),
                row.get("repeat_count", ""),
                row.get("fail_count", ""),
                f"{throughput:.3f}",
                pct_delta(throughput, baseline_throughput),
                row.get("elapsed_median", ""),
                dominant,
                f"{ratio * 100.0:.1f}%" if ratio > 0 else "",
            ]
        )

    stage_rows: list[list[str]] = []
    for direction, row in sorted(best_rows_by_direction(rows).items()):
        _, _, stages = stage_breakdown(row)
        stage_total = sum(value for _, value in stages)
        stage_rows.append(
            [
                direction,
                key_label(row),
                row.get("throughput_gbps_median", ""),
                row.get("elapsed_median", ""),
                ", ".join(
                    f"{name}={value:.3f}s/{percent(value, stage_total)}"
                    for name, value in stages
                    if value > 0
                ),
            ]
        )

    failed = [row for row in rows if row.get("fail_count", "0") not in ("", "0")]
    unstable = [
        row
        for row in rows
        if as_float(row, "throughput_gbps_min") > 0
        and as_float(row, "throughput_gbps_max") / as_float(row, "throughput_gbps_min") > 1.5
    ]

    stor_best = best_rows_by_direction(rows).get("stor")
    retr_best = best_rows_by_direction(rows).get("retr")
    conclusion_lines = [
        "- Defaults remain unchanged: POSIX backend, full final verify, preallocate off, every 16 chunk manifest flush.",
        "- Keep `verified_chunks`, `final_only`, and commit fsync modes opt-in until the private median data is reviewed case by case.",
    ]
    for label, row in (("STOR", stor_best), ("RETR", retr_best)):
        if not row:
            continue
        dominant, ratio = dominant_stage(row)
        if ratio >= 0.4:
            conclusion_lines.append(
                f"- {label}: dominant median measured stage is `{dominant}` at {ratio * 100.0:.1f}% of measured stage time; prioritize that path next."
            )
        elif ratio > 0:
            conclusion_lines.append(
                f"- {label}: no single stage exceeds 40%; continue diagnosis across storage, checksum, and final verify."
            )

    content = [
        "# Phase 4J POSIX Pipeline Diagnosis",
        "",
        "## Inputs",
        "",
        *[f"- `{path}`" for path in input_paths],
        "",
        "## Median Summary",
        "",
        markdown_table(
            [
                "case",
                "repeat",
                "fail",
                "median Gbps",
                "vs baseline",
                "elapsed s",
                "dominant stage",
                "dominant share of measured stages",
            ],
            table_rows,
        ),
        "## Best Passing Stage Breakdown",
        "",
        markdown_table(["direction", "case", "median Gbps", "elapsed s", "stage shares"], stage_rows),
        "## Data Quality",
        "",
        f"- Failed grouped rows: {len(failed)}",
        f"- High-variance grouped rows (max/min > 1.5): {len(unstable)}",
        "",
        "## Gate Conclusion",
        "",
        *conclusion_lines,
        "",
        "## Non-Goals Preserved",
        "",
        "- No raw FTP STOR/RETR.",
        "- No default io_uring, preallocate full, or verified_chunks.",
        "- No change to checksum, manifest, resume, final verify, or framed data semantics.",
        "",
    ]
    return "\n".join(content)


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Phase 4J private matrix summaries.")
    parser.add_argument("--matrix-summary-csv", action="append", required=True)
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
