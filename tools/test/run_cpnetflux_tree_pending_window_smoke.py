#!/usr/bin/env python3
"""Bounded pending-window smoke/perf check for a 128 x 1MiB directory."""
import argparse
import hashlib
import json
import subprocess
import tempfile
import time
from pathlib import Path

from tree_smoke_common import free_port, wait_for_control, stop_server
from gridftp_port_window import clamp_passive_data_port_base


def tree_hash(root: Path):
    h = hashlib.sha256()
    count = 0
    total = 0
    for path in sorted(
        item for item in root.rglob("*")
        if item.is_file() and ".cpnetflux." not in item.name and ".part." not in item.name
    ):
        relative = path.relative_to(root).as_posix()
        data = path.read_bytes()
        count += 1
        total += len(data)
        h.update(
            relative.encode()
            + b"\0"
            + str(len(data)).encode()
            + b"\0"
            + hashlib.sha256(data).hexdigest().encode()
            + b"\0"
        )
    return h.hexdigest(), count, total


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--windows", default="1,2,4")
    args = parser.parse_args()
    windows = [int(value) for value in args.windows.split(",")]

    with tempfile.TemporaryDirectory(prefix="cpnetflux-pending-window-") as temporary:
        root = Path(temporary)
        source = root / "source"
        server_root = root / "server"
        source.mkdir()
        server_root.mkdir()
        block = bytes((index * 17) % 251 for index in range(1024 * 1024))
        for index in range(128):
            (source / f"f{index:03}.bin").write_bytes(block)
        expected = tree_hash(source)
        expected_hash, expected_count, expected_bytes = expected

        port = free_port()
        data_base = clamp_passive_data_port_base(free_port())
        log = root / "server.log"
        with log.open("w") as output:
            server = subprocess.Popen(
                [
                    str(args.build_dir / "cpnetflux-gridftp-server"),
                    "--host", "127.0.0.1",
                    "--port", str(port),
                    "--data-port-base", str(data_base),
                    "--root", str(server_root),
                    "--connections", "1",
                    "--checksum", "none",
                ],
                stdout=output,
                stderr=subprocess.STDOUT,
            )
        try:
            wait_for_control(port)
            rows = []
            for window in windows:
                target = f"upload-w{window}"
                summary = root / f"{target}.json"
                upload_command = [
                    str(args.build_dir / "cpnetflux-tree-upload-client"),
                    "--host", "127.0.0.1", "--port", str(port),
                    "--source-dir", str(source), "--dest-dir", target,
                    "--checksum", "none", "--control-reuse", "worker",
                    "--data-session-reuse", "tree", "--data-pending-window", str(window),
                    "--json-summary", str(summary),
                ]
                start = time.monotonic()
                result = subprocess.run(upload_command, text=True, capture_output=True, timeout=120)
                elapsed = time.monotonic() - start
                assert result.returncode == 0, result.stdout + result.stderr
                item = json.loads(summary.read_text())
                actual = tree_hash(server_root / target)
                assert item["data_pending_window"] == window
                assert item["data_pending_high_watermark"] <= window
                assert item["completed_files"] == expected_count and actual == expected
                item["wall_seconds"] = elapsed
                item["direction"] = "upload"
                rows.append(item)

                destination = root / f"download-w{window}"
                summary = root / f"download-w{window}.json"
                download_command = [
                    str(args.build_dir / "cpnetflux-tree-download-client"),
                    "--host", "127.0.0.1", "--port", str(port),
                    "--source-dir", target, "--dest-dir", str(destination),
                    "--checksum", "none", "--control-reuse", "worker",
                    "--data-session-reuse", "tree", "--data-pending-window", str(window),
                    "--json-summary", str(summary),
                ]
                start = time.monotonic()
                result = subprocess.run(download_command, text=True, capture_output=True, timeout=120)
                elapsed = time.monotonic() - start
                assert result.returncode == 0, result.stdout + result.stderr
                item = json.loads(summary.read_text())
                actual = tree_hash(destination)
                assert item["data_pending_window"] == window
                assert item["data_pending_high_watermark"] == 0
                assert item["completed_files"] == expected_count and actual == expected
                item["wall_seconds"] = elapsed
                item["direction"] = "download"
                rows.append(item)
            print(json.dumps({
                "expected": {
                    "tree_hash": expected_hash,
                    "files": expected_count,
                    "bytes": expected_bytes,
                },
                "results": rows,
            }, sort_keys=True))
        finally:
            stop_server(server, log)


if __name__ == "__main__":
    main()
