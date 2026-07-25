#!/usr/bin/env python3
"""Analyze Phase 4L stability and RETR sender/receiver breakdown summaries."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def load_rows(paths: list[str]) -> list[dict[str, str]]:
    rows: list[dict[str, str]] = []
    for path in paths:
        with open(path, newline="", encoding="utf-8") as handle:
            for row in csv.DictReader(handle):
                row["_source_csv"] = path
                rows.append(row)
    return rows


def as_float(row: dict[str, str], field: str) -> float:
    try:
        return float(row.get(field, "") or 0.0)
    except ValueError:
        return 0.0


def pct(part: float, total: float) -> float:
    if total <= 0.0:
        return 0.0
    return (part / total) * 100.0


def share(part: float, values: list[float]) -> float:
    total = sum(value for value in values if value > 0.0)
    return pct(part, total)


def case_name(row: dict[str, str]) -> str:
    return (
        f"{row.get('direction')} {row.get('checksum_algorithm')} "
        f"fv={row.get('final_verify_policy')}->{row.get('final_verify_policy_effective')} "
        f"mfp={row.get('manifest_flush_policy')} "
        f"pws={row.get('posix_write_strategy')}->{row.get('posix_write_strategy_effective')} "
        f"fiobuf={row.get('file_io_buffer_size')}"
    )


def table(headers: list[str], rows: list[list[str]]) -> str:
    if not rows:
        return "_No rows._\n"
    lines = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    lines.extend("| " + " | ".join(row) + " |" for row in rows)
    return "\n".join(lines) + "\n"


def stor_rows(rows: list[dict[str, str]]) -> str:
    selected = [row for row in rows if row.get("direction") == "stor"]
    selected.sort(
        key=lambda row: (
            -as_float(row, "receiver_temp_write_seconds_median"),
            -as_float(row, "throughput_gbps_spread_pct"),
        )
    )
    output: list[list[str]] = []
    for row in selected[:16]:
        elapsed = as_float(row, "elapsed_median")
        temp_write = as_float(row, "receiver_temp_write_seconds_median") or as_float(
            row, "temp_write_seconds_median"
        )
        checksum = as_float(row, "receiver_checksum_seconds_median") or as_float(
            row, "checksum_seconds_median"
        )
        manifest = as_float(row, "receiver_manifest_flush_seconds_median") or as_float(
            row, "manifest_flush_seconds_median"
        )
        final_verify = as_float(row, "receiver_final_verify_seconds_median") or as_float(
            row, "final_verify_seconds_median"
        )
        output.append(
            [
                case_name(row),
                f"{as_float(row, 'throughput_gbps_median'):.3f}",
                f"{as_float(row, 'throughput_gbps_spread_pct'):.1f}%",
                f"{elapsed:.3f}",
                f"{temp_write:.3f} ({pct(temp_write, elapsed):.1f}% wall)",
                f"{checksum:.3f} ({pct(checksum, elapsed):.1f}% wall)",
                f"{manifest:.3f} ({pct(manifest, elapsed):.1f}% wall)",
                f"{final_verify:.3f} ({pct(final_verify, elapsed):.1f}% wall)",
                row.get("unstable_spread_gt_20pct", ""),
            ]
        )
    return table(
        [
            "case",
            "median Gbps",
            "spread",
            "elapsed",
            "temp write",
            "checksum",
            "manifest",
            "final verify",
            "unstable",
        ],
        output,
    )


def retr_rows(rows: list[dict[str, str]]) -> str:
    selected = [row for row in rows if row.get("direction") == "retr"]
    selected.sort(
        key=lambda row: (
            -max(
                as_float(row, "sender_network_send_seconds_median"),
                as_float(row, "receiver_download_temp_write_seconds_median"),
            ),
            -as_float(row, "throughput_gbps_spread_pct"),
        )
    )
    output: list[list[str]] = []
    for row in selected[:16]:
        elapsed = as_float(row, "elapsed_median")
        send = as_float(row, "sender_network_send_seconds_median")
        source_read = as_float(row, "sender_source_read_seconds_median")
        recv_write = as_float(row, "receiver_download_temp_write_seconds_median")
        receiver_verify = as_float(row, "receiver_final_verify_seconds_median")
        if send >= recv_write and send >= source_read:
            next_focus = "sender network send"
        elif recv_write >= source_read:
            next_focus = "receiver download write"
        else:
            next_focus = "sender source read"
        key_stages = [send, recv_write, source_read, receiver_verify]
        output.append(
            [
                case_name(row),
                f"{as_float(row, 'throughput_gbps_median'):.3f}",
                f"{as_float(row, 'throughput_gbps_spread_pct'):.1f}%",
                f"{elapsed:.3f}",
                f"{send:.3f} ({share(send, key_stages):.1f}% key)",
                f"{recv_write:.3f} ({share(recv_write, key_stages):.1f}% key)",
                f"{source_read:.3f} ({share(source_read, key_stages):.1f}% key)",
                f"{receiver_verify:.3f} ({share(receiver_verify, key_stages):.1f}% key)",
                next_focus,
            ]
        )
    return table(
        [
            "case",
            "median Gbps",
            "spread",
            "elapsed",
            "sender send",
            "receiver write",
            "source read",
            "final verify",
            "next focus",
        ],
        output,
    )


def unstable_rows(rows: list[dict[str, str]]) -> str:
    selected = [
        row
        for row in rows
        if row.get("unstable_spread_gt_20pct") == "1"
        or row.get("unstable_minmax_outlier") == "1"
        or row.get("stage_throughput_mismatch") == "1"
        or int(float(row.get("fail_count", "0") or 0)) > 0
    ]
    selected.sort(key=lambda row: -as_float(row, "throughput_gbps_spread_pct"))
    output: list[list[str]] = []
    for row in selected[:24]:
        output.append(
            [
                case_name(row),
                f"{as_float(row, 'throughput_gbps_median'):.3f}",
                f"{as_float(row, 'throughput_gbps_min'):.3f}",
                f"{as_float(row, 'throughput_gbps_max'):.3f}",
                f"{as_float(row, 'throughput_gbps_spread_pct'):.1f}%",
                row.get("unstable_spread_gt_20pct", ""),
                row.get("unstable_minmax_outlier", ""),
                row.get("stage_throughput_mismatch", ""),
                row.get("fail_count", ""),
            ]
        )
    return table(
        [
            "case",
            "median",
            "min",
            "max",
            "spread",
            "spread>20",
            "min/max",
            "stage mismatch",
            "fail",
        ],
        output,
    )


def find_best(rows: list[dict[str, str]], *, direction: str, checksum: str) -> dict[str, str] | None:
    candidates = [
        row
        for row in rows
        if row.get("direction") == direction
        and row.get("checksum_algorithm") == checksum
        and int(float(row.get("fail_count", "0") or 0)) == 0
    ]
    if not candidates:
        return None
    return max(candidates, key=lambda row: as_float(row, "throughput_gbps_median"))


def find_default_row(rows: list[dict[str, str]], *, direction: str, checksum: str) -> dict[str, str] | None:
    for row in rows:
        if (
            row.get("direction") == direction
            and row.get("checksum_algorithm") == checksum
            and row.get("final_verify_policy") == "full"
            and row.get("manifest_flush_policy") == "every_n_chunks"
            and row.get("posix_write_strategy") == "auto"
            and row.get("file_io_buffer_size") == "0"
            and int(float(row.get("fail_count", "0") or 0)) == 0
        ):
            return row
    return None


def recommendation_rows(rows: list[dict[str, str]]) -> str:
    output: list[list[str]] = []
    default_row = find_default_row(rows, direction="stor", checksum="crc32c")
    if default_row is not None:
        output.append(
            [
                "Default general",
                "Keep defaults / no opt-in recommendation",
                f"{as_float(default_row, 'throughput_gbps_median'):.3f}",
                f"{as_float(default_row, 'throughput_gbps_spread_pct'):.1f}%",
                "Baseline is the current default row; Phase 4L does not change runtime defaults.",
            ]
        )
    else:
        output.append(["Default general", "Keep defaults / no opt-in recommendation", "", "", "No valid default row."])

    for label, direction, checksum, note in [
        ("RETR + crc32c", "retr", "crc32c", "May document opt-in only if spread is acceptable and both sender/receiver remain correct."),
        ("checksum=none", "retr", "none", "Performance comparison only; not a reliable resume recommendation."),
    ]:
        best = find_best(rows, direction=direction, checksum=checksum)
        if best is None:
            output.append([label, "none", "", "", "No recommendation; no valid rows."])
            continue
        spread = as_float(best, "throughput_gbps_spread_pct")
        unstable = best.get("unstable_spread_gt_20pct") == "1" or best.get("unstable_minmax_outlier") == "1"
        if unstable or spread > 20.0:
            recommendation = "Keep defaults / no opt-in recommendation"
        else:
            recommendation = (
                f"Opt-in: pws={best.get('posix_write_strategy')} "
                f"fiobuf={best.get('file_io_buffer_size')} "
                f"fv={best.get('final_verify_policy')} "
                f"mfp={best.get('manifest_flush_policy')}"
            )
        output.append(
            [
                label,
                recommendation,
                f"{as_float(best, 'throughput_gbps_median'):.3f}",
                f"{spread:.1f}%",
                note,
            ]
        )
    conservative = find_default_row(rows, direction="stor", checksum="crc32c")
    output.append(
        [
            "Conservative recovery",
            "Keep defaults / no opt-in recommendation",
            f"{as_float(conservative, 'throughput_gbps_median'):.3f}" if conservative else "",
            f"{as_float(conservative, 'throughput_gbps_spread_pct'):.1f}%" if conservative else "",
            "Keep full final verify and every_n_chunks manifest flush; opt-in verified_chunks/final_only is not a conservative default.",
        ]
    )
    return table(["scenario", "recommendation", "reference median Gbps", "spread", "notes"], output)


def render(rows: list[dict[str, str]], inputs: list[str]) -> str:
    fail_count = sum(int(float(row.get("fail_count", "0") or 0)) for row in rows)
    unstable_count = sum(1 for row in rows if row.get("unstable_spread_gt_20pct") == "1")
    mismatch_count = sum(1 for row in rows if row.get("stage_throughput_mismatch") == "1")
    lines = [
        "# Phase 4L Stability and RETR Breakdown",
        "",
        "## Inputs",
        "",
        *[f"- `{path}`" for path in inputs],
        "",
        "## Executive Summary",
        "",
        f"- Summary rows: `{len(rows)}`; grouped fail count: `{fail_count}`.",
        f"- High spread rows (`>20%`): `{unstable_count}`.",
        f"- Stage/throughput mismatch rows: `{mismatch_count}`.",
        "- Defaults remain unchanged: POSIX backend, `posix_write_strategy=auto`, `file_io_buffer_size=0`, full final verify, every-16 manifest flush, no commit fsync.",
        "- Opt-in recommendations below are documentation-only; no runtime defaults are changed.",
        "- STOR percentages use wall-clock elapsed time. RETR sender/receiver stage times can be connection-accumulated on different sides, so RETR percentages are shares of listed key stages rather than wall-clock percentages.",
        "",
        "## STOR Top Bottleneck Table",
        "",
        stor_rows(rows),
        "",
        "## RETR Sender / Receiver Breakdown",
        "",
        retr_rows(rows),
        "",
        "## High-Variance / Suspicious Rows",
        "",
        unstable_rows(rows),
        "",
        "## Opt-in Recommendation Matrix",
        "",
        recommendation_rows(rows),
        "",
        "## Gate Conclusion",
        "",
        "- If STOR rows remain high-spread and temp write dominates, treat the source as storage/writeback or page-cache pressure before changing code defaults.",
        "- If RETR sender network send dominates the best repeat-stable rows, next work should inspect send scheduling/backpressure before more receiver write tweaks.",
        "- If receiver download write dominates RETR rows, keep using POSIX writeback diagnostics and consider storage-side opt-ins only per scenario.",
        "- Do not default-enable `verified_chunks`, `final_only`, `coalesced`, preallocate full, commit fsync, or io_uring from Phase 4L data alone.",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze Phase 4L private matrix summaries.")
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
