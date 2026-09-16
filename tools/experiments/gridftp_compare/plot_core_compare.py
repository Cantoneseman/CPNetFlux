#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_SUMMARY = REPO_ROOT / "tools" / "perf" / "results" / "20260902Tcore_cpnetflux_compare" / "summary.csv"
DEFAULT_COMPARISON_SUMMARY = (
    REPO_ROOT / "tools" / "experiments" / "gridftp_compare" / "results" / "core_20260901T094944Z" / "summary.csv"
)


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def gridftp_status_note(path: Path) -> str:
    if not path.is_file():
        return "GridFTP comparison summary missing; no pass throughput available"
    rows = load_rows(path)
    pass_rows = [row for row in rows if row.get("system") == "gridftp" and row.get("result") == "pass"]
    if pass_rows:
        return "GridFTP pass data available in comparison summary"
    return "GridFTP in this run was blocked_remote_auth; no pass throughput points were produced"


def to_int(row: dict[str, str], key: str) -> int:
    value = row.get(key, "")
    if value == "":
        raise ValueError(f"missing {key} in row {row.get('case_id', '<unknown>')}")
    return int(value)


def to_float(row: dict[str, str], key: str) -> float:
    value = row.get(key, "")
    if value == "":
        raise ValueError(f"missing {key} in row {row.get('case_id', '<unknown>')}")
    return float(value)


def find_row(
    rows: list[dict[str, str]],
    *,
    dataset: str,
    direction: str,
    file_parallelism: int,
    per_file_connections: int,
) -> dict[str, str]:
    for row in rows:
        if (
            row.get("dataset") == dataset
            and row.get("direction") == direction
            and to_int(row, "file_parallelism") == file_parallelism
            and to_int(row, "per_file_connections") == per_file_connections
        ):
            return row
    raise KeyError(
        f"row not found: dataset={dataset}, direction={direction}, "
        f"file_parallelism={file_parallelism}, per_file_connections={per_file_connections}"
    )


def series_from_rows(
    rows: list[dict[str, str]],
    *,
    dataset: str,
    direction: str,
    x_key: str,
    points: list[tuple[int, int]],
) -> tuple[list[int], list[float]]:
    xs: list[int] = []
    ys: list[float] = []
    for file_parallelism, per_file_connections in points:
        row = find_row(
            rows,
            dataset=dataset,
            direction=direction,
            file_parallelism=file_parallelism,
            per_file_connections=per_file_connections,
        )
        if x_key == "connections":
            x_value = to_int(row, "per_file_connections")
        elif x_key == "budget":
            x_value = to_int(row, "file_parallelism") * to_int(row, "per_file_connections")
        else:
            raise ValueError(f"unsupported x_key: {x_key}")
        xs.append(x_value)
        ys.append(to_float(row, "median_logical_goodput_mbps"))
    return xs, ys


def configure_matplotlib() -> None:
    plt.rcParams.update(
        {
            "font.family": "sans-serif",
            "font.sans-serif": ["Noto Sans CJK SC", "DejaVu Sans"],
            "axes.unicode_minus": False,
            "axes.linewidth": 1.2,
            "axes.labelsize": 14,
            "xtick.labelsize": 12,
            "ytick.labelsize": 12,
            "legend.fontsize": 12,
            "legend.framealpha": 1.0,
            "legend.edgecolor": "0.70",
            "legend.facecolor": "white",
            "lines.linewidth": 2.0,
            "savefig.dpi": 300,
        }
    )


def style_axis(ax: plt.Axes) -> None:
    ax.grid(True, which="major", axis="both", color="0.86", linewidth=0.7)
    ax.tick_params(direction="out", length=5, width=1.2, pad=6)
    for spine in ax.spines.values():
        spine.set_linewidth(1.2)


def draw_series(
    ax: plt.Axes,
    xs: list[int],
    ys: list[float],
    *,
    label: str,
    linestyle: str,
    marker: str,
) -> None:
    ax.plot(
        xs,
        ys,
        color="black",
        linestyle=linestyle,
        marker=marker,
        markersize=8,
        markerfacecolor="white",
        markeredgecolor="black",
        markeredgewidth=1.4,
        label=label,
    )


def build_figure(rows: list[dict[str, str]], output_dir: Path) -> tuple[Path, Path]:
    configure_matplotlib()

    fig, axes = plt.subplots(1, 2, figsize=(12.2, 4.9))

    specs = [
        (
            axes[0],
            "connections",
            [
                ("single_256MiB", "local_to_remote", "CPNetFlux 单文件上传", ":", "o", [(1, 1), (1, 2), (1, 4), (1, 8)]),
                ("single_256MiB", "remote_to_local", "CPNetFlux 单文件下载", "--", "s", [(1, 1), (1, 2), (1, 4), (1, 8)]),
            ],
            "每文件连接数",
            "(a) CPNetFlux 单文件吞吐",
        ),
        (
            axes[1],
            "budget",
            [
                ("tree_mixed_256MiB", "local_to_remote", "CPNetFlux 混合上传", "-", "D", [(1, 1), (1, 2), (2, 2), (4, 2)]),
                ("tree_mixed_256MiB", "remote_to_local", "CPNetFlux 混合下载", "-.", "^", [(1, 1), (1, 2), (2, 2), (4, 2)]),
            ],
            "总流并发",
            "(b) CPNetFlux 混合目录吞吐",
        ),
    ]

    legend_handles = []
    legend_labels = []

    for ax, x_key, series_specs, xlabel, panel_label in specs:
        style_axis(ax)
        all_values: list[float] = []
        all_xs: list[int] = []
        for dataset, direction, label, linestyle, marker, points in series_specs:
            xs, ys = series_from_rows(rows, dataset=dataset, direction=direction, x_key=x_key, points=points)
            draw_series(ax, xs, ys, label=label, linestyle=linestyle, marker=marker)
            all_values.extend(ys)
            all_xs.extend(xs)
        ax.set_xlabel(xlabel)
        ax.set_ylabel("平均吞吐 (Mbps)")
        ax.set_xticks(sorted(dict.fromkeys(all_xs)))
        y_min = min(all_values)
        y_max = max(all_values)
        pad = max(0.08, (y_max - y_min) * 0.18)
        ax.set_ylim(y_min - pad, y_max + pad)
        ax.text(
            0.5,
            -0.28,
            panel_label,
            transform=ax.transAxes,
            ha="center",
            va="top",
            fontsize=13,
        )
        handles, labels = ax.get_legend_handles_labels()
        legend_handles.extend(handles)
        legend_labels.extend(labels)

    fig.text(
        0.5,
        0.84,
        gridftp_status_note(DEFAULT_COMPARISON_SUMMARY),
        ha="center",
        va="center",
        fontsize=11,
        color="0.35",
    )

    fig.legend(
        legend_handles[:4],
        legend_labels[:4],
        loc="upper center",
        ncol=4,
        frameon=True,
        fancybox=True,
        borderpad=0.6,
        handlelength=2.2,
        columnspacing=1.2,
        bbox_to_anchor=(0.5, 0.995),
    )
    fig.subplots_adjust(left=0.07, right=0.98, top=0.77, bottom=0.19, wspace=0.20)

    png_path = output_dir / "cpnetflux_core_compare.png"
    pdf_path = output_dir / "cpnetflux_core_compare.pdf"
    fig.savefig(png_path, bbox_inches="tight")
    fig.savefig(pdf_path, bbox_inches="tight")
    plt.close(fig)
    return png_path, pdf_path


def main() -> int:
    parser = argparse.ArgumentParser(description="Plot the CPNetFlux core comparison matrix.")
    parser.add_argument("--summary-csv", type=Path, default=DEFAULT_SUMMARY, help="path to summary.csv")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="directory for output figures; defaults to the summary CSV directory",
    )
    args = parser.parse_args()

    summary_path = args.summary_csv.resolve()
    if not summary_path.is_file():
        raise SystemExit(f"summary CSV not found: {summary_path}")

    output_dir = args.output_dir.resolve() if args.output_dir else summary_path.parent
    output_dir.mkdir(parents=True, exist_ok=True)

    rows = load_rows(summary_path)
    png_path, pdf_path = build_figure(rows, output_dir)
    print(f"wrote {png_path}")
    print(f"wrote {pdf_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
