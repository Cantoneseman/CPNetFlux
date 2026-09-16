#!/usr/bin/env python3
import argparse
import sys
import tempfile
from pathlib import Path

from tree_smoke_common import free_port, make_tree, start_server, stop_server, tree_hash, run_checked


def main() -> int:
    parser = argparse.ArgumentParser(description="Run CPNetFlux tree download smoke.")
    parser.add_argument("--build-dir", default="build")
    args = parser.parse_args()
    build_dir = Path(args.build_dir)
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tree-download.") as temp_text:
        temp = Path(temp_text)
        server_root = temp / "server-root"
        source = server_root / "dataset"
        source.mkdir(parents=True)
        make_tree(source)
        dest = temp / "downloaded"
        control_port = free_port()
        data_port = free_port()
        server_log = temp / "server.log"
        server = start_server(build_dir, server_root, control_port, data_port, server_log)
        try:
            run_checked(
                [
                    str(build_dir / "cpnetflux-tree-download-client"),
                    "--host",
                    "127.0.0.1",
                    "--port",
                    str(control_port),
                    "--source-dir",
                    "dataset",
                    "--dest-dir",
                    str(dest),
                    "--connections",
                    "2",
                ]
            )
            expected, files, total = tree_hash(source)
            actual, _, _ = tree_hash(dest)
            if expected != actual:
                raise RuntimeError(f"tree hash mismatch: {expected} != {actual}")
            print(f"tree download smoke passed files={files} total_bytes={total} tree_hash={actual}")
        finally:
            stop_server(server, server_log)
    return 0


if __name__ == "__main__":
    sys.exit(main())
