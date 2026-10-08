#!/usr/bin/env python3
"""Compare async-control pipeline depth 0/1 against matched GridFTP cases."""
from __future__ import annotations

import argparse
import csv
import statistics
from pathlib import Path
from typing import Any

KEY_FIELDS = ("dataset", "direction", "file_parallelism", "per_file_connections", "repeat_index")
CONFIG_FIELDS = KEY_FIELDS[:4]


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        return list(csv.DictReader(handle))


def index_rows(rows: list[dict[str, str]], system: str, depth: int | None) -> dict[tuple[str, ...], dict[str, str]]:
    selected: dict[tuple[str, ...], dict[str, str]] = {}
    for row in rows:
        if row.get("system") != system:
            continue
        if depth is not None and row.get("control_pipeline_depth", "0") != str(depth):
            continue
        key = tuple(row.get(field, "") for field in KEY_FIELDS)
        if key in selected:
            raise ValueError(f"duplicate {system} case key: {key}")
        selected[key] = row
    return selected


def valid_goodput(row: dict[str, str] | None) -> float | None:
    if not row or row.get("result") != "pass" or row.get("hash_match", "").lower() != "true":
        return None
    try:
        value = float(row.get("logical_goodput_mbps", ""))
    except ValueError:
        return None
    return value if value > 0 else None


def compare_rows(off_rows: list[dict[str, str]], pipeline_rows: list[dict[str, str]]) -> list[dict[str, Any]]:
    baseline = index_rows(off_rows, "gridftp", None)
    depth0 = index_rows(off_rows, "cpnetflux", 0)
    depth1 = index_rows(pipeline_rows, "cpnetflux", 1)
    configs = sorted({key[:4] for key in baseline} | {key[:4] for key in depth0} | {key[:4] for key in depth1})
    results: list[dict[str, Any]] = []
    for config in configs:
        keys = [key for key in sorted(baseline) if key[:4] == config]
        grids = [valid_goodput(baseline.get(key)) for key in keys]
        off = [valid_goodput(depth0.get(key)) for key in keys]
        pipe = [valid_goodput(depth1.get(key)) for key in keys]
        grid_values = [v for v in grids if v is not None]
        off_values = [v for v in off if v is not None]
        pipe_values = [v for v in pipe if v is not None]
        n_expected = 3
        complete = (
            len(keys) == n_expected
            and len(grid_values) == n_expected
            and len(off_values) == n_expected
            and len(pipe_values) == n_expected
        )
        grid_median = statistics.median(grid_values) if grid_values else None
        off_median = statistics.median(off_values) if off_values else None
        pipe_median = statistics.median(pipe_values) if pipe_values else None
        off_ratio = off_median / grid_median if complete and grid_median else None
        pipe_ratio = pipe_median / grid_median if complete and grid_median else None
        if not complete:
            status = "INCOMPLETE"
        elif pipe_ratio is not None and pipe_ratio >= 0.90:
            status = "PASS_90_PERCENT"
        else:
            status = "BELOW_90_PERCENT"
        results.append({
            "dataset": config[0], "direction": config[1],
            "file_parallelism": config[2], "per_file_connections": config[3],
            "gridftp_n": len(grid_values), "cpnetflux_depth0_n": len(off_values),
            "cpnetflux_depth1_n": len(pipe_values),
            "gridftp_median_mbps": grid_median,
            "cpnetflux_depth0_median_mbps": off_median,
            "cpnetflux_depth0_ratio": off_ratio,
            "cpnetflux_depth1_median_mbps": pipe_median,
            "cpnetflux_depth1_ratio": pipe_ratio,
            "status": status,
        })
    return results


def write_report(results: list[dict[str, Any]], output: Path) -> bool:
    fields = [
        "dataset", "direction", "file_parallelism", "per_file_connections",
        "gridftp_n", "cpnetflux_depth0_n", "cpnetflux_depth1_n",
        "gridftp_median_mbps", "cpnetflux_depth0_median_mbps", "cpnetflux_depth0_ratio",
        "cpnetflux_depth1_median_mbps", "cpnetflux_depth1_ratio", "status",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    csv_path = output.with_suffix(".csv")
    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in results:
            writer.writerow({key: "" if row[key] is None else row[key] for key in fields})
    complete = bool(results) and all(row["status"] == "PASS_90_PERCENT" for row in results)
    lines = [
        "# CPNetFlux 异步目录控制调度对比",
        "",
        "口径：按目录、方向、并行度配置分组，取 3 次 logical goodput 中位数；只有该配置全部 3 次退出成功且 source/destination hash 相同才纳入。depth=0 与 depth=1 分别除以同一批 GridFTP 中位数。",
        "",
        "| 数据集 | 方向 | 文件并发 | 每文件连接 | GridFTP Mbps | CPNetFlux depth=0 / GridFTP | depth=1 / GridFTP | 判定 |",
        "|---|---|---:|---:|---:|---:|---:|---|",
    ]
    for row in results:
        def fmt(value: Any) -> str:
            return "—" if value is None else f"{value:.3f}"
        r0 = fmt(row["cpnetflux_depth0_ratio"])
        r1 = fmt(row["cpnetflux_depth1_ratio"])
        lines.append(
            f"| {row['dataset']} | {row['direction']} | {row['file_parallelism']} | {row['per_file_connections']} | {fmt(row['gridftp_median_mbps'])} | {r0} | {r1} | {row['status']} |"
        )
    lines.extend([
        "",
        f"总体结论：{'全部配置达到 90%' if complete else '尚未全部达到 90% 或数据不完整'}。",
        f"逐配置 CSV：`{csv_path}`。",
        "吞吐以外的协议/证据字段仍需同时看各 runner 的 results.csv；本报告不把缺失测量算作通过。",
        "",
    ])
    output.write_text("\n".join(lines), encoding="utf-8")
    return complete


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--depth0-results", type=Path, required=True, help="run containing CPNetFlux depth=0 and GridFTP")
    parser.add_argument("--depth1-results", type=Path, required=True, help="run containing CPNetFlux depth=1")
    parser.add_argument("--output", type=Path, required=True, help="Markdown report path")
    args = parser.parse_args()
    results = compare_rows(read_rows(args.depth0_results), read_rows(args.depth1_results))
    complete = write_report(results, args.output)
    return 0 if complete else (2 if results and all(row["status"] != "INCOMPLETE" for row in results) else 3)


if __name__ == "__main__":
    raise SystemExit(main())
