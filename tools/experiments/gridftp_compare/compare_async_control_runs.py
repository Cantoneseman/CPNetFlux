#!/usr/bin/env python3
"""Compare fixed async-control depth runs against matched GridFTP cases."""
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from pathlib import Path
from typing import Any

KEY_FIELDS = ("dataset", "direction", "file_parallelism", "per_file_connections", "repeat_index")
def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def read_run_rows(run: Path) -> list[dict[str, str]]:
    path = run / "results.csv" if run.is_dir() else run
    if not path.is_file():
        raise FileNotFoundError(f"missing results.csv: {path}")
    return read_rows(path)


def read_plan(run: Path) -> dict[str, Any] | None:
    if not run.is_dir():
        return None
    path = run / "case_plan.json"
    if not path.is_file():
        return None
    with path.open(encoding="utf-8") as handle:
        value = json.load(handle)
    return value if isinstance(value, dict) else None


def parse_depth_run(value: str) -> tuple[int, Path]:
    depth_text, separator, path_text = value.partition("=")
    if not separator or not depth_text.isdigit() or not path_text:
        raise ValueError(f"--depth-run must be DEPTH=PATH: {value!r}")
    depth = int(depth_text)
    if depth not in {1, 2, 4}:
        raise ValueError(f"depth run must be 1, 2, or 4: {value!r}")
    return depth, Path(path_text)


def case_key(row: dict[str, Any]) -> tuple[str, ...]:
    return tuple(str(row.get(field, "")) for field in KEY_FIELDS)


def expected_keys(*, systems: list[str], directions: list[str], repeats: int, dataset: str, file_parallelism: int, connections: int) -> set[tuple[str, ...]]:
    return {
        (dataset, direction, str(file_parallelism), str(connections), str(repeat))
        for direction in directions
        for repeat in range(repeats)
    }


def validate_plan(
    run: Path,
    *,
    systems: list[str],
    depths: set[int],
    directions: list[str],
    repeats: int,
    dataset: str,
    file_parallelism: int,
    connections: int,
) -> tuple[str, str]:
    plan = read_plan(run)
    if plan is None:
        return "UNVERIFIED", "case_plan.json missing"
    cases = plan.get("cases")
    if not isinstance(cases, list):
        return "PLAN_MISMATCH", "case_plan.json cases is not a list"
    expected_total = len(systems) * len(directions) * repeats
    if plan.get("case_count") != expected_total or len(cases) != expected_total:
        return "PLAN_MISMATCH", f"expected {expected_total} planned cases, found case_count={plan.get('case_count')!r}, list={len(cases)}"
    seen: set[tuple[str, str, tuple[str, ...]]] = set()
    for item in cases:
        if not isinstance(item, dict):
            return "PLAN_MISMATCH", "case entry is not an object"
        system = str(item.get("system", ""))
        depth = int(item.get("control_pipeline_depth", -1))
        key = case_key(item)
        marker = (system, str(depth), key)
        if marker in seen:
            return "PLAN_MISMATCH", f"duplicate planned key: {marker}"
        seen.add(marker)
        if system not in systems or depth not in depths:
            return "PLAN_MISMATCH", f"unexpected system/depth: {system}/{depth}"
        if key not in expected_keys(
            systems=systems,
            directions=directions,
            repeats=repeats,
            dataset=dataset,
            file_parallelism=file_parallelism,
            connections=connections,
        ):
            return "PLAN_MISMATCH", f"unexpected case key: {key}"
    expected = {
        (system, str(depth), key)
        for system in systems
        for depth in depths
        for key in expected_keys(
            systems=systems,
            directions=directions,
            repeats=repeats,
            dataset=dataset,
            file_parallelism=file_parallelism,
            connections=connections,
        )
    }
    if seen != expected:
        return "PLAN_MISMATCH", f"planned set differs: missing={len(expected - seen)} extra={len(seen - expected)}"
    return "PASS", f"{expected_total} planned cases"


def index_rows(rows: list[dict[str, str]], system: str, depth: int) -> dict[tuple[str, ...], dict[str, str]]:
    selected: dict[tuple[str, ...], dict[str, str]] = {}
    for row in rows:
        if row.get("system") != system:
            continue
        try:
            row_depth = int(row.get("control_pipeline_depth", "0"))
        except ValueError:
            row_depth = -1
        if row_depth != depth:
            continue
        key = case_key(row)
        if key in selected:
            raise ValueError(f"duplicate observed {system} depth={depth} case key: {key}")
        selected[key] = row
    return selected


def valid_goodput(row: dict[str, str] | None) -> float | None:
    if not row or row.get("result") != "pass":
        return None
    if row.get("hash_match", "").lower() != "true":
        return None
    if row.get("integrity_status") and row.get("integrity_status") != "pass":
        return None
    try:
        value = float(row.get("logical_goodput_mbps", ""))
    except ValueError:
        return None
    return value if math.isfinite(value) and value > 0 else None


def optional_float(row: dict[str, str] | None, fields: tuple[str, ...]) -> float | None:
    if not row:
        return None
    for field in fields:
        value = row.get(field, "")
        if value is None or value == "":
            continue
        try:
            number = float(value)
        except (TypeError, ValueError):
            continue
        if math.isfinite(number) and number >= 0:
            return number
    return None


def client_summary(run: Path | None, row: dict[str, str] | None) -> dict[str, Any]:
    if run is None or not row or not row.get("case_id"):
        return {}
    path = run / "cases" / row["case_id"] / "client_summary.json"
    if not path.is_file():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    return value if isinstance(value, dict) else {}


def median(values: list[float]) -> float | None:
    return statistics.median(values) if values else None


def compare_rows(
    baseline_rows: list[dict[str, str]],
    depth_rows: dict[int, list[dict[str, str]]],
    *,
    expected_repeats: int = 3,
    directions: list[str] | None = None,
    dataset: str = "tree_dense_128MiB",
    file_parallelism: int = 1,
    connections: int = 1,
    summary_sources: dict[tuple[str, int], Path] | None = None,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    directions = directions or ["local_to_remote", "remote_to_local"]
    summary_sources = summary_sources or {}
    expected = {
        (dataset, direction, str(file_parallelism), str(connections), str(repeat))
        for direction in directions
        for repeat in range(expected_repeats)
    }
    baseline_grid = index_rows(baseline_rows, "gridftp", 0)
    baseline_cp = index_rows(baseline_rows, "cpnetflux", 0)
    indexed = {depth: index_rows(rows, "cpnetflux", depth) for depth, rows in depth_rows.items()}
    summaries: list[dict[str, Any]] = []
    repeat_rows: list[dict[str, Any]] = []
    for direction in directions:
        config = (dataset, direction, str(file_parallelism), str(connections))
        expected_direction = {key for key in expected if key[1] == direction}
        grid_keys = {key for key in baseline_grid if key[:4] == config}
        base_keys = {key for key in baseline_cp if key[:4] == config}
        grid_values = [valid_goodput(baseline_grid.get(key)) for key in sorted(expected_direction & grid_keys)]
        base_values = [valid_goodput(baseline_cp.get(key)) for key in sorted(expected_direction & base_keys)]
        grid_valid = [v for v in grid_values if v is not None]
        base_valid = [v for v in base_values if v is not None]
        grid_elapsed_values = [
            value for key in sorted(expected_direction)
            if valid_goodput(baseline_grid.get(key)) is not None
            for value in [optional_float(baseline_grid.get(key), ("elapsed_seconds",))]
            if value is not None
        ]
        base_elapsed_values = [
            value for key in sorted(expected_direction)
            if valid_goodput(baseline_cp.get(key)) is not None
            for value in [optional_float(baseline_cp.get(key), ("elapsed_seconds",))]
            if value is not None
        ]
        grid_median = median(grid_valid)
        base_median = median(base_valid)
        grid_elapsed_median = median(grid_elapsed_values)
        base_elapsed_median = median(base_elapsed_values)
        grid_complete = grid_keys == expected_direction and len(grid_valid) == expected_repeats
        base_complete = base_keys == expected_direction and len(base_valid) == expected_repeats
        for key in sorted(expected_direction):
            row = baseline_grid.get(key)
            repeat_rows.append(_repeat_row("gridftp", 0, row, key, valid_goodput(row), client_summary(summary_sources.get(("gridftp", 0)), row)))
            row = baseline_cp.get(key)
            repeat_rows.append(_repeat_row("cpnetflux", 0, row, key, valid_goodput(row), client_summary(summary_sources.get(("cpnetflux", 0)), row)))
        for depth in sorted(indexed):
            rows = indexed[depth]
            keys = {key for key in rows if key[:4] == config}
            values = [valid_goodput(rows.get(key)) for key in sorted(expected_direction & keys)]
            valid = [v for v in values if v is not None]
            cp_elapsed_values = [
                value for key in sorted(expected_direction & keys)
                if valid_goodput(rows.get(key)) is not None
                for value in [optional_float(rows.get(key), ("elapsed_seconds",))]
                if value is not None
            ]
            cp_median = median(valid)
            cp_elapsed_median = median(cp_elapsed_values)
            complete = keys == expected_direction and len(valid) == expected_repeats
            ratio = cp_median / grid_median if complete and grid_complete and base_complete else None
            delta = cp_median / base_median - 1.0 if complete and base_complete and base_median else None
            status = "COMPLETE" if complete and grid_complete and base_complete else "INCOMPLETE"
            if keys != expected_direction:
                missing = len(expected_direction - keys)
                extra = len(keys - expected_direction)
                detail = f"missing={missing}, extra={extra}"
            elif not complete:
                detail = f"valid={len(valid)}/{expected_repeats}"
            elif not grid_complete:
                detail = f"GridFTP valid={len(grid_valid)}/{expected_repeats}"
            elif not base_complete:
                detail = f"depth0 valid={len(base_valid)}/{expected_repeats}"
            else:
                detail = ""
            for key in sorted(expected_direction):
                row = rows.get(key)
                repeat_rows.append(_repeat_row("cpnetflux", depth, row, key, valid_goodput(row), client_summary(summary_sources.get(("cpnetflux", depth)), row)))
            summaries.append({
                "dataset": dataset,
                "direction": direction,
                "file_parallelism": file_parallelism,
                "per_file_connections": connections,
                "depth": depth,
                "expected_repeats": expected_repeats,
                "gridftp_observed": len(grid_keys),
                "gridftp_valid": len(grid_valid),
                "cpnetflux_depth0_observed": len(base_keys),
                "cpnetflux_depth0_valid": len(base_valid),
                "cpnetflux_observed": len(keys),
                "cpnetflux_valid": len(valid),
                "gridftp_median_mbps": grid_median,
                "cpnetflux_depth0_median_mbps": base_median,
                "cpnetflux_median_mbps": cp_median,
                "cpnetflux_gridftp_ratio": ratio,
                "depth_vs_depth0_delta": delta,
                "gridftp_elapsed_median_seconds": grid_elapsed_median,
                "gridftp_wall_per_file_median_seconds": grid_elapsed_median / 128.0 if grid_elapsed_median is not None else None,
                "cpnetflux_depth0_elapsed_median_seconds": base_elapsed_median,
                "cpnetflux_depth0_wall_per_file_median_seconds": base_elapsed_median / 128.0 if base_elapsed_median is not None else None,
                "cpnetflux_elapsed_median_seconds": cp_elapsed_median,
                "cpnetflux_wall_per_file_median_seconds": cp_elapsed_median / 128.0 if cp_elapsed_median is not None else None,
                "wall_per_file_decreased_vs_depth0": (
                    cp_elapsed_median / 128.0 < base_elapsed_median / 128.0
                    if cp_elapsed_median is not None and base_elapsed_median is not None
                    and len(cp_elapsed_values) == expected_repeats
                    and len(base_elapsed_values) == expected_repeats
                    and complete and base_complete
                    else None
                ),
                "goodput_improved_vs_depth0": (
                    cp_median > base_median
                    if cp_median is not None and base_median is not None and complete and base_complete
                    else None
                ),
                "performance_gate": "not_evaluated",
                "status": status,
                "detail": detail,
            })
    return summaries, repeat_rows


def _repeat_row(system: str, depth: int, row: dict[str, str] | None, key: tuple[str, ...], goodput: float | None, summary: dict[str, Any]) -> dict[str, Any]:
    elapsed = optional_float(row, ("elapsed_seconds",))
    control_prepare = optional_float(summary, ("control_prepare_seconds",))
    transfer_complete_wait = optional_float(summary, ("transfer_complete_wait_seconds",))
    return {
        "system": system,
        "depth": depth,
        "dataset": key[0],
        "direction": key[1],
        "file_parallelism": key[2],
        "per_file_connections": key[3],
        "repeat_index": key[4],
        "result": row.get("result", "missing") if row else "missing",
        "hash_match": row.get("hash_match", "") if row else "",
        "logical_goodput_mbps": goodput,
        "elapsed_seconds": elapsed,
        "runner_wall_per_file_seconds_amortized": elapsed / 128.0 if elapsed is not None else None,
        "control_prepare_seconds": control_prepare,
        "transfer_complete_wait_seconds": transfer_complete_wait,
        "client_stage_status": "observed" if control_prepare is not None or transfer_complete_wait is not None else "missing",
    }


def write_report(
    results: list[dict[str, Any]],
    repeat_rows: list[dict[str, Any]],
    output: Path,
    *,
    plan_status: dict[str, tuple[str, str]],
) -> bool:
    output.parent.mkdir(parents=True, exist_ok=True)
    summary_path = output.with_suffix(".csv")
    repeat_path = output.with_name(output.stem + ".repeats.csv")
    summary_fields = [
        "dataset", "direction", "file_parallelism", "per_file_connections", "depth",
        "expected_repeats", "gridftp_observed", "gridftp_valid", "cpnetflux_depth0_observed",
        "cpnetflux_depth0_valid", "cpnetflux_observed", "cpnetflux_valid", "gridftp_median_mbps",
        "cpnetflux_depth0_median_mbps", "cpnetflux_median_mbps", "cpnetflux_gridftp_ratio",
        "depth_vs_depth0_delta", "gridftp_elapsed_median_seconds", "gridftp_wall_per_file_median_seconds",
        "cpnetflux_depth0_elapsed_median_seconds", "cpnetflux_depth0_wall_per_file_median_seconds",
        "cpnetflux_elapsed_median_seconds", "cpnetflux_wall_per_file_median_seconds",
        "wall_per_file_decreased_vs_depth0", "goodput_improved_vs_depth0", "performance_gate", "status", "detail",
    ]
    with summary_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=summary_fields)
        writer.writeheader()
        for row in results:
            writer.writerow({field: "" if row.get(field) is None else row.get(field, "") for field in summary_fields})
    repeat_fields = list(repeat_rows[0]) if repeat_rows else [
        "system", "depth", "dataset", "direction", "file_parallelism", "per_file_connections",
        "repeat_index", "result", "hash_match", "logical_goodput_mbps", "elapsed_seconds",
        "runner_wall_per_file_seconds_amortized", "control_prepare_seconds", "transfer_complete_wait_seconds", "client_stage_status",
    ]
    with repeat_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=repeat_fields)
        writer.writeheader()
        for row in repeat_rows:
            writer.writerow({field: "" if row.get(field) is None else row.get(field, "") for field in repeat_fields})
    lines = [
        "# CPNetFlux async-control depth comparison",
        "",
        "Matrix: tree_dense_128MiB, file_parallelism=1, per_file_connections=1, two directions, fixed repeats.",
        "Each row keeps runner wall elapsed_seconds and logical_goodput_mbps separate from optional client stage timing.",
        "runner_wall_per_file_seconds_amortized is elapsed_seconds / 128; it is not a per-file latency measurement.",
        "",
        "## Plan validation",
        "",
    ]
    for name, (status, detail) in sorted(plan_status.items()):
        lines.append(f"- {name}: {status} ({detail})")
    lines.extend([
        "",
        "| Direction | Depth | GridFTP median Mbps | CPNetFlux median Mbps | CPNetFlux/GridFTP | CPNetFlux wall/128 median s | Depth0 wall/128 median s | wall/128 decreased | goodput improved | valid/expected | Status |",
        "|---|---:|---:|---:|---:|---:|---:|---|---|---:|---|",
    ])
    def fmt(value: Any) -> str:
        return "-" if value is None else f"{value:.3f}"
    for row in results:
        lines.append(
            f"| {row['direction']} | {row['depth']} | {fmt(row['gridftp_median_mbps'])} | {fmt(row['cpnetflux_median_mbps'])} | {fmt(row['cpnetflux_gridftp_ratio'])} | {fmt(row['cpnetflux_wall_per_file_median_seconds'])} | {fmt(row['cpnetflux_depth0_wall_per_file_median_seconds'])} | {row['wall_per_file_decreased_vs_depth0']} | {row['goodput_improved_vs_depth0']} | {row['cpnetflux_valid']}/{row['expected_repeats']} | {row['status']} {row['detail']} |"
        )
    lines.extend([
        "",
        "COMPLETE means the planned data is complete and integrity-valid; it is not a performance pass and no 90% threshold is applied here.",
        "Runner wall/128 is an amortized same-bytes comparison. A lower wall/128 or higher goodput is reported as evidence only; the performance gate remains not_evaluated, so the matrix must not expand before review.",
        "Client stage timing is read from each case client_summary.json (control_prepare_seconds and transfer_complete_wait_seconds); missing timing is not treated as zero or pass.",
        f"Per-repeat evidence: `{repeat_path}`.",
        f"Summary CSV: `{summary_path}`.",
        "",
    ])
    output.write_text("\n".join(lines), encoding="utf-8")
    return bool(results) and all(row["status"] == "COMPLETE" for row in results) and all(status == "PASS" for status, _ in plan_status.values())


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-run", type=Path, help="depth0 run directory containing CPNetFlux and GridFTP")
    parser.add_argument("--depth-run", action="append", default=[], help="DEPTH=RUN_DIR, repeat for depth 1/2/4")
    parser.add_argument("--expected-repeats", type=int, default=3)
    parser.add_argument("--expected-directions", default="local_to_remote,remote_to_local")
    parser.add_argument("--dataset", default="tree_dense_128MiB")
    parser.add_argument("--file-parallelism", type=int, default=1)
    parser.add_argument("--per-file-connections", type=int, default=1)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--depth0-results", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--depth1-results", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.expected_repeats <= 0:
        parser.error("--expected-repeats must be positive")
    directions = [item.strip() for item in args.expected_directions.split(",") if item.strip()]
    plan_status: dict[str, tuple[str, str]] = {}
    if args.baseline_run:
        baseline_run = args.baseline_run
        depth_runs = dict(parse_depth_run(value) for value in args.depth_run)
        if set(depth_runs) != {1, 2, 4}:
            parser.error("new mode requires --depth-run for depths 1, 2, and 4")
        baseline_rows = read_run_rows(baseline_run)
        depth_rows = {depth: read_run_rows(path) for depth, path in depth_runs.items()}
        plan_status["baseline"] = validate_plan(
            baseline_run,
            systems=["cpnetflux", "gridftp"],
            depths={0},
            directions=directions,
            repeats=args.expected_repeats,
            dataset=args.dataset,
            file_parallelism=args.file_parallelism,
            connections=args.per_file_connections,
        )
        for depth, path in sorted(depth_runs.items()):
            plan_status[f"depth{depth}"] = validate_plan(
                path,
                systems=["cpnetflux"],
                depths={depth},
                directions=directions,
                repeats=args.expected_repeats,
                dataset=args.dataset,
                file_parallelism=args.file_parallelism,
                connections=args.per_file_connections,
            )
    elif args.depth0_results and args.depth1_results:
        baseline_rows = read_rows(args.depth0_results)
        depth_rows = {1: read_rows(args.depth1_results)}
        plan_status["legacy"] = ("UNVERIFIED", "legacy CSV inputs have no case_plan.json")
    else:
        parser.error("provide --baseline-run plus three --depth-run values")
    try:
        results, repeat_rows = compare_rows(
            baseline_rows,
            depth_rows,
            expected_repeats=args.expected_repeats,
            directions=directions,
            dataset=args.dataset,
            file_parallelism=args.file_parallelism,
            connections=args.per_file_connections,
            summary_sources=(
                {
                    ("gridftp", 0): baseline_run,
                    ("cpnetflux", 0): baseline_run,
                    **{("cpnetflux", depth): path for depth, path in depth_runs.items()},
                }
                if args.baseline_run
                else {}
            ),
        )
    except (ValueError, OSError, json.JSONDecodeError) as exc:
        print(f"comparison failed: {exc}")
        return 3
    complete = write_report(results, repeat_rows, args.output, plan_status=plan_status)
    return 0 if complete else 2


if __name__ == "__main__":
    raise SystemExit(main())
