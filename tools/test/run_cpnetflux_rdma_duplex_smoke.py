#!/usr/bin/env python3
"""Opt-in hardware regression for concurrent RDMA buffered writes and reads."""
import argparse
import json
import os
from pathlib import Path
import selectors
import socket
import subprocess
import sys


def summary(output, expected_role, size):
    rows = [json.loads(line.split(" ", 1)[1]) for line in output.splitlines()
            if line.startswith("CPNETFLUX_RDMA_SOCKET_BENCH ")]
    assert len(rows) == 1, output
    row = rows[0]
    assert row["result"] == "pass" and row["role"] == expected_role, row
    assert row["logical_bytes"] == size, row
    assert row["send_bytes"] == row["wire_bytes"] + 1, row
    assert row["receive_bytes"] == row["wire_bytes"] + 1, row
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--bind-address", default=os.environ.get("CPNETFLUX_TEST_RDMA_BIND", ""))
    parser.add_argument("--bytes", type=int, default=(64 << 20) + 17)
    parser.add_argument("--repetitions", type=int, default=3)
    args = parser.parse_args()
    if not args.bind_address:
        print("SKIP: set CPNETFLUX_TEST_RDMA_BIND to a local RDMA IPv4 address")
        return 77
    if args.bytes <= 0 or args.repetitions <= 0:
        parser.error("bytes and repetitions must be positive")
    executable = str(args.build_dir.resolve() / "cpnetflux-rdma-socket-bench")
    for repetition in range(args.repetitions):
        with socket.socket() as probe:
            probe.bind((args.bind_address, 0))
            port = probe.getsockname()[1]
        server = subprocess.Popen(
            [executable, "duplex-server", args.bind_address, str(port), str(args.bytes)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(server.stdout, selectors.EVENT_READ)
                if not selector.select(timeout=10):
                    raise RuntimeError("RDMA bench did not announce listen readiness")
                ready = server.stdout.readline()
            if ready.strip() != "CPNETFLUX_RDMA_BENCH_READY":
                raise RuntimeError("RDMA server failed before readiness: " + ready)
            client = subprocess.run(
                [executable, "duplex-client", args.bind_address, str(port),
                 str(args.bytes), args.bind_address],
                capture_output=True, text=True, timeout=75)
            output, _ = server.communicate(timeout=75)
            assert client.returncode == 0, client.stdout + client.stderr
            assert server.returncode == 0, ready + output
            client_row = summary(client.stdout, "duplex-client", args.bytes)
            server_row = summary(output, "duplex-server", args.bytes)
            print(json.dumps({"repetition": repetition + 1, "client": client_row,
                              "server": server_row}, sort_keys=True))
        finally:
            if server.poll() is None:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
    return 0


if __name__ == "__main__":
    sys.exit(main())
