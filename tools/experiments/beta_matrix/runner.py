from __future__ import annotations

import argparse
import csv
import json
import os
import time
from pathlib import Path
from typing import Any

from .model import (
    LinkProfile,
    WorkloadProfile,
    build_method_matrix,
    classify_regime,
    choose_b5_preset,
    choose_b6_preset,
    plan_method,
)


SUMMARY_FIELDS = [
    "dataset",
    "domain",
    "experiment_group",
    "method",
    "transport",
    "compression",
    "compression_scope",
    "status",
    "reason",
    "control_reuse_mode",
    "file_parallelism",
    "connections",
    "planner_preset",
    "regime",
    "dominant_component",
    "estimated_seconds",
    "command_hint",
]

DATASET_FIELDS = [
    "dataset",
    "domain",
    "description",
    "file_count",
    "total_bytes",
    "total_mib",
    "size_min",
    "size_max",
    "size_p50",
    "size_p90",
    "size_mean",
    "regime",
    "b5_preset",
    "b5_estimated_seconds",
    "b6_preset",
    "b6_estimated_seconds",
]

METHOD_FIELDS = [
    "method",
    "experiment_group",
    "transport",
    "compression",
    "compression_scope",
    "beta_v1_status",
    "description",
]


def compact_timestamp() -> str:
    return time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())


def default_fdt_root(repo_root: Path) -> Path:
    return Path(os.environ.get("CPNETFLUX_FDT_ROOT", repo_root.parent / "fast data transfer"))


def default_manifest_path(repo_root: Path) -> Path:
    return default_fdt_root(repo_root) / "data" / "fixtures" / "hpc" / "manifest.json"


def _write_csv(path: Path, fields: list[str], rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def _write_jsonl(path: Path, events: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        for event in events:
            handle.write(json.dumps(event, ensure_ascii=False, sort_keys=True) + "\n")


def load_profiles(manifest_path: Path, names: list[str] | None = None) -> list[WorkloadProfile]:
    data = json.loads(manifest_path.read_text(encoding="utf-8"))
    datasets = data.get("datasets", {})
    selected = set(names or datasets.keys())
    missing = sorted(selected - set(datasets.keys()))
    if missing:
        raise ValueError("unknown dataset(s): " + ",".join(missing))
    profiles: list[WorkloadProfile] = []
    for name, row in sorted(datasets.items()):
        if name in selected:
            profiles.append(WorkloadProfile.from_manifest_row(name, row))
    if not profiles:
        raise ValueError("no datasets selected")
    return profiles


def dataset_rows(profiles: list[WorkloadProfile]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for profile in profiles:
        b5_name, _, b5_est = choose_b5_preset(profile, LinkProfile())
        b6_name, _, b6_est = choose_b6_preset(profile, LinkProfile())
        rows.append(
            {
                "dataset": profile.dataset,
                "domain": profile.domain,
                "description": profile.description,
                "file_count": profile.file_count,
                "total_bytes": profile.total_bytes,
                "total_mib": f"{profile.total_bytes / (1024 * 1024):.3f}",
                "size_min": profile.size_min,
                "size_max": profile.size_max,
                "size_p50": f"{profile.size_p50:.1f}",
                "size_p90": f"{profile.size_p90:.1f}",
                "size_mean": f"{profile.size_mean:.1f}",
                "regime": classify_regime(profile),
                "b5_preset": b5_name,
                "b5_estimated_seconds": f"{b5_est['t_job_est']:.6f}",
                "b6_preset": b6_name,
                "b6_estimated_seconds": f"{b6_est['t_job_est']:.6f}",
            }
        )
    return rows


def method_rows() -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for spec in build_method_matrix():
        rows.append(
            {
                "method": spec.method,
                "experiment_group": spec.experiment_group,
                "transport": spec.transport,
                "compression": spec.compression,
                "compression_scope": spec.compression_scope,
                "beta_v1_status": "unsupported" if spec.method == "cpnetflux_b2_read_pipeline_proxy" else "planned",
                "description": spec.description,
            }
        )
    return rows


def plan_rows(profiles: list[WorkloadProfile], link: LinkProfile) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for profile in profiles:
        for spec in build_method_matrix():
            plan = plan_method(profile, link, spec)
            row = plan.to_row()
            row["domain"] = profile.domain
            rows.append({field: row.get(field, "") for field in SUMMARY_FIELDS})
    return rows


def write_final_summary(path: Path, audit: dict[str, Any], profiles: list[WorkloadProfile]) -> None:
    dataset_text = ", ".join(profile.dataset for profile in profiles)
    text = f"""# CPNetFlux Beta 本地 dry-run 汇总

- 状态：{audit["status"]}
- 数据集：{dataset_text}
- 矩阵行数：{audit["row_count"]}
- unsupported 行数：{audit["unsupported_count"]}
- 云端传输：未执行
- GridFTP 对比：只生成计划；正式实验前必须通过 XTransfer-GridFTP/GCT preflight，不能用 scp/rsync 替代。
- 压缩路径：CPSS/gzip/lz4 均在 staging 层，CPNetFlux C++ 数据热路径不接入压缩逻辑。
- 控制连接复用：仅 `--control-reuse worker` opt-in；默认 `off` 保持 Alpha RC 行为。
"""
    path.write_text(text, encoding="utf-8")


def run_dry_run(
    *,
    manifest_path: Path,
    output_dir: Path,
    compressor_root: Path | None = None,
    datasets: list[str] | None = None,
    link: LinkProfile | None = None,
) -> dict[str, Any]:
    manifest_path = manifest_path.resolve()
    output_dir = output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    profiles = load_profiles(manifest_path, datasets)
    active_link = link or LinkProfile()
    summary = plan_rows(profiles, active_link)
    datasets_out = dataset_rows(profiles)
    methods_out = method_rows()
    unsupported_count = sum(1 for row in summary if row["status"] == "unsupported")
    events = [
        {
            "event": "dry_run_start",
            "manifest_path": str(manifest_path),
            "dataset_count": len(profiles),
            "method_count": len(build_method_matrix()),
        }
    ]
    events.extend({"event": "plan_row", **row} for row in summary)
    events.append({"event": "dry_run_complete", "row_count": len(summary), "unsupported_count": unsupported_count})

    _write_csv(output_dir / "summary.csv", SUMMARY_FIELDS, summary)
    _write_csv(output_dir / "dataset_summary.csv", DATASET_FIELDS, datasets_out)
    _write_csv(output_dir / "method_comparison.csv", METHOD_FIELDS, methods_out)
    _write_jsonl(output_dir / "live_metrics.jsonl", events)

    audit = {
        "status": "pass",
        "dry_run": True,
        "manifest_path": str(manifest_path),
        "output_dir": str(output_dir),
        "dataset_count": len(profiles),
        "method_count": len(build_method_matrix()),
        "row_count": len(summary),
        "unsupported_count": unsupported_count,
        "compressor_root": str(compressor_root.resolve()) if compressor_root else "",
        "compressor_root_ok": bool(compressor_root and (compressor_root / "Compressor" / "cli.py").is_file()),
        "cloud_execution": "not_started",
        "gridftp_execution": "not_started",
        "output_files": [
            "summary.csv",
            "dataset_summary.csv",
            "method_comparison.csv",
            "live_metrics.jsonl",
            "audit.json",
            "final_summary_zh.md",
        ],
    }
    (output_dir / "audit.json").write_text(
        json.dumps(audit, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    write_final_summary(output_dir / "final_summary_zh.md", audit, profiles)
    return audit


def parse_csv_list(text: str | None) -> list[str] | None:
    if not text:
        return None
    return [item.strip() for item in text.split(",") if item.strip()]


def build_parser() -> argparse.ArgumentParser:
    repo_root = Path(__file__).resolve().parents[3]
    parser = argparse.ArgumentParser(description="Prepare the CPNetFlux Beta cross-domain experiment matrix.")
    parser.add_argument("--dry-run", action="store_true", help="generate matrix plans without cloud execution")
    parser.add_argument("--fdt-root", type=Path, default=default_fdt_root(repo_root))
    parser.add_argument("--fdt-manifest", type=Path, default=None)
    parser.add_argument("--compressor-root", type=Path, default=None)
    parser.add_argument("--datasets", default=None, help="comma-separated dataset names")
    parser.add_argument("--link-rtt-ms", type=float, default=13.0)
    parser.add_argument("--link-bandwidth-mbps", type=float, default=44.0)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=repo_root / "tools" / "experiments" / "beta_matrix" / "results" / f"{compact_timestamp()}_dry-run",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if not args.dry_run:
        parser.error("Beta local phase currently supports --dry-run only")
    manifest = args.fdt_manifest or (args.fdt_root / "data" / "fixtures" / "hpc" / "manifest.json")
    audit = run_dry_run(
        manifest_path=manifest,
        output_dir=args.output_dir,
        compressor_root=args.compressor_root,
        datasets=parse_csv_list(args.datasets),
        link=LinkProfile(rtt_ms=args.link_rtt_ms, bandwidth_mbps=args.link_bandwidth_mbps),
    )
    print(f"output_dir={audit['output_dir']}")
    print(f"summary_csv={Path(audit['output_dir']) / 'summary.csv'}")
    print(f"audit_json={Path(audit['output_dir']) / 'audit.json'}")
    print(f"result={audit['status']} row_count={audit['row_count']} unsupported_count={audit['unsupported_count']}")
    return 0 if audit["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
