"""Render the same pages, synthetic evidence, windows and scaling per variant."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parents[1]
CASES = [("tasks", 0, 0), ("tasks", 1, 0), ("tasks", 2, 0), ("tasks", 3, 0),
         ("scanning", 2, 0), ("failed", 2, 0), ("offline", 0, 0)]
B_CASES = [("tasks", 1, 1), ("tasks", 1, 2), ("tasks", 4, 0),
           ("tasks", 5, 0), ("tasks", 6, 0), ("offline", 6, 0)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--variants", nargs="+", default=["original", "a", "b"])
    args = parser.parse_args()
    rows = []
    for variant in args.variants:
        cases = CASES + (B_CASES if variant == "b" else [])
        for width, height in [(1280, 720), (1366, 768), (1920, 1080)]:
            for scale in ["1", "1.25", "1.5"]:
                for scenario, page, create_step in cases:
                    command = [sys.executable, "-X", "utf8", str(HERE / "preview.py"),
                        "--variant", variant, "--scenario", scenario, "--page", str(page),
                        "--create-step", str(create_step),
                        "--width", str(width), "--height", str(height), "--scale", scale,
                        "--offscreen", "--smoke-ms", "180"]
                    if (width, scale) == (1366, "1") or (width, scale, scenario, page, create_step) == (1280, "1.5", "tasks", 0, 0):
                        step_suffix = f"-s{create_step}" if create_step else ""
                        shot = HERE / "evidence" / f"{variant}-{scenario}-p{page}{step_suffix}-{width}-{scale}.png"
                        command += ["--screenshot", str(shot)]
                    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", timeout=15)
                    if result.returncode:
                        raise RuntimeError(f"{variant}/{width}/{scale}/{scenario}/{page}:\n{result.stderr}\n{result.stdout}")
                    row = json.loads(result.stdout.strip().splitlines()[-1])
                    assert row["real_agent_connected"] is False and row["qml_errors"] == []
                    rows.append(row)
                print(f"{variant}: {width}x{height} scale={scale}: {len(cases)} pages/states passed", flush=True)
    output = HERE / "docs" / "render-results.json"
    if output.is_file():
        previous = json.loads(output.read_text(encoding="utf-8"))
        rows = [r for r in previous if r["variant"] not in args.variants] + rows
    output.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"{len(rows)} Qt/QML renders passed. Physical OS DPI, GNOME and real transfers are separate validation.")


if __name__ == "__main__":
    main()
