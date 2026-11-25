#!/usr/bin/env python3
"""Markdown summary of one bench.sh run: median Mbit/s of verified transfers per case."""
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

TOOLS = ["gridftp", "cpn_raw", "cpn_adaptive"]


def main(run_dir: Path) -> None:
    rows = list(csv.DictReader(open(run_dir / "results.csv")))
    cases = defaultdict(lambda: defaultdict(list))
    for row in rows:
        cases[(row["dataset"], row["direction"])][row["tool"]].append(row)

    print(f"# {run_dir.name}\n")
    print("Median Mbit/s of verified transfers (verified/total runs).\n")
    print("| dataset | direction | " + " | ".join(TOOLS) + " |")
    print("|---|---|" + "---|" * len(TOOLS))
    for (dataset, direction), by_tool in sorted(cases.items()):
        cells = []
        for tool in TOOLS:
            runs = by_tool.get(tool, [])
            good = [float(r["mbit_per_s"]) for r in runs if r["verified"] == "yes"]
            median = f"{statistics.median(good):.1f}" if good else "-"
            cells.append(f"{median} ({len(good)}/{len(runs)})" if runs else "")
        print(f"| {dataset} | {direction} | " + " | ".join(cells) + " |")

    print("\nAdaptive compression (cpn_adaptive): wire/logical bytes and per-file decisions.\n")
    for path in sorted(run_dir.glob("cpn_adaptive_*.json")):
        summary = json.load(open(path))
        logical = summary.get("compressed_logical_bytes", 0)
        wire = summary.get("compressed_wire_bytes", 0)
        reasons = {k: v for k, v in summary.get("compression_adaptive_file_reasons", {}).items() if v}
        ratio = f"{wire / logical:.2f}" if logical else "-"
        print(f"- {path.stem}: ratio {ratio} over {logical} bytes, reasons {reasons or '{}'}")

    iperf = run_dir / "iperf.csv"
    if iperf.exists():
        print("\nLink baseline (iperf3, Mbit/s):\n")
        for label, mode, mbps in csv.reader(open(iperf)):
            print(f"- {label} {mode}: {mbps}")
    failed = [r for r in rows if r["verified"] != "yes"]
    if failed:
        print("\nFailed or unverified runs (see <tool>_<dataset>_<direction>_r<rep>.log):\n")
        for r in failed:
            print(f"- r{r['rep']} {r['tool']} {r['dataset']} {r['direction']}: rc={r['rc']}")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
