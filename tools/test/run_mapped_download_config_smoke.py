#!/usr/bin/env python3
"""Rejected experimental configurations must fail before control connection."""
import os
import pathlib
import socket
import subprocess
import sys
import tempfile


def main():
    binary = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="cpnetflux-mapped-config-") as root:
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
            base = [str(binary), "--host", "127.0.0.1", "--port", str(port),
                    "--source-dir", "payload", "--dest-dir", root + "/out"]
            cases = [("unknown", [], {}), ("mmap", [], {}),
                     ("mmap", ["--connections", "4", "--control-reuse", "worker",
                               "--posix-write-strategy", "direct"], {}),
                     ("mmap", ["--connections", "4", "--control-reuse", "worker",
                               "--file-parallelism", "4", "--posix-write-strategy", "direct"],
                      {"CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION": "inline",
                       "CPNETFLUX_STAGE_WRITE_CONCURRENCY": "1"}),
                     ("mmap", ["--connections", "4", "--control-reuse", "worker",
                               "--file-parallelism", "4", "--posix-write-strategy", "direct"],
                      {"CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION": "queued",
                       "CPNETFLUX_PAYLOAD_PIPELINE_DEPTH": "4"})]
            for backend, args, overrides in cases:
                env = {k: v for k, v in os.environ.items() if not k.startswith("CPNETFLUX_")}
                env.update(overrides)
                env["CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE"] = backend
                result = subprocess.run(base + args, env=env, capture_output=True, text=True, timeout=5)
                assert result.returncode == 2 and "experimental download write" in result.stderr, (
                    backend, args, result.returncode, result.stderr)
            # Existing option parsing may create an empty destination directory.
            assert not any(pathlib.Path(root, "out").rglob("*"))
    print("mapped download configuration rejection: 5 passed")


if __name__ == "__main__":
    main()
