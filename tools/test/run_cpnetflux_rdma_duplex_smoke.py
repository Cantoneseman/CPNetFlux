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
import time


def summary(output, expected_role, size):
    rows = [json.loads(line.split(" ", 1)[1]) for line in output.splitlines()
            if line.startswith("CPNETFLUX_RDMA_SOCKET_BENCH ")]
    assert len(rows) == 1, output
    row = rows[0]
    assert row["result"] == "pass" and row["role"] == expected_role, row
    assert row["logical_bytes"] == size, row
    assert row["send_bytes"] == row["wire_bytes"] + 1, row
    assert row["receive_bytes"] == row["wire_bytes"] + 1, row
    # Verify the effective hardware policy on both active and passive QPs.
    # Encoding zero advertises a 655 ms retry delay, not a zero-delay retry.
    assert row["negotiated_min_rnr_timer"] == 12, row
    assert row["connection_id"] > 0 and row["qp_num"] > 0, row
    assert row["receive_completions"] == row["receive_reposts"], row
    assert row["receive_post_calls"] == (
        row["configured_receive_queue_depth"] + row["receive_reposts"]), row
    assert row["receive_bytes"] == row["receive_bytes_consumed"], row
    return row


def diagnostics(output, expected_role, summary_row):
    traces = [json.loads(line.split(" ", 1)[1]) for line in output.splitlines()
              if line.startswith("CPNETFLUX_RDMA_TRACE ")]
    assert traces, output
    events = [row["event"] for row in traces]
    setup = ["qp_resources_created", "send_pool_allocated", "send_pool_registered",
             "receive_pool_allocated", "receive_pool_registered", "receive_preposted"]
    required = [*setup, "queue_pair_initialized", "cm_established_wait", "close_begin", "disconnect_end",
                "qp_destroyed", "close_end"]
    for event in required:
        assert events.count(event) == 1, (event, events)
    positions = [events.index(event) for event in required]
    assert positions == sorted(positions), events
    assert all(row["role"] == expected_role for row in traces), traces
    connection_ids = {row["connection_id"] for row in traces}
    qp_numbers = {row["qp_num"] for row in traces}
    assert len(connection_ids) == 1 and 0 not in connection_ids, traces
    assert len(qp_numbers) == 1 and 0 not in qp_numbers, traces
    initialized = traces[events.index("queue_pair_initialized")]
    components = [traces[events.index(event)]["duration_ns"] for event in setup]
    assert all(value > 0 for value in components), components
    assert sum(components) <= initialized["duration_ns"], (components, initialized)
    pools = [json.loads(line.split(" ", 1)[1]) for line in output.splitlines()
             if line.startswith("CPNETFLUX_RDMA_POOL ")]
    assert [row["event"] for row in pools] == ["send", "receive"], pools
    for pool, count in zip(pools, [summary_row["configured_send_queue_depth"],
            summary_row["configured_receive_queue_depth"] + 32]):
        assert pool["address"] % (2 << 20) == 0, pool
        assert pool["bytes"] == count * summary_row["slot_bytes"], pool
        assert pool["bytes"] <= pool["mapping_bytes"] <= pool["bytes"] + (2 << 20) + 4096, pool
        assert pool["connection_id"] == summary_row["connection_id"], pool
        assert pool["qp_num"] == summary_row["qp_num"] and pool["role"] == expected_role, pool
        assert isinstance(pool["hugepage_hint_accepted"], bool), pool
        assert pool["hugepage_hint_accepted"] or pool["hugepage_hint_errno"] != 0, pool
    closed = traces[events.index("close_begin")]
    assert initialized["observed_posted_receive_depth"] == (
        summary_row["configured_receive_queue_depth"]), initialized
    assert closed["receive_completions"] == summary_row["receive_completions"], closed
    assert closed["reposts"] == summary_row["receive_reposts"], closed
    assert closed["bytes_consumed"] == summary_row["receive_bytes_consumed"], closed
    assert "minimum_observed_posted_receive_depth" in closed, closed
    assert "free_low_watermark" in closed and "ready_high_watermark" in closed, closed


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
    child_env = os.environ.copy()
    child_env["CPNETFLUX_RDMA_DIAGNOSTICS"] = "1"
    for repetition in range(args.repetitions):
        with socket.socket() as probe:
            probe.bind((args.bind_address, 0))
            port = probe.getsockname()[1]
        server = subprocess.Popen(
            [executable, "duplex-server", args.bind_address, str(port), str(args.bytes)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=child_env)
        try:
            startup = []
            ready = False
            started = time.monotonic()
            with selectors.DefaultSelector() as selector:
                selector.register(server.stdout, selectors.EVENT_READ)
                while time.monotonic() - started < 10:
                    remaining = 10 - (time.monotonic() - started)
                    if not selector.select(timeout=remaining):
                        break
                    line = server.stdout.readline()
                    if not line:
                        break
                    if line.strip() == "CPNETFLUX_RDMA_BENCH_READY":
                        ready = True
                        break
                    startup.append(line)
            if not ready:
                raise RuntimeError(
                    "RDMA server failed before readiness: " + "".join(startup))
            client = subprocess.run(
                [executable, "duplex-client", args.bind_address, str(port),
                 str(args.bytes), args.bind_address],
                capture_output=True, text=True, timeout=75, env=child_env)
            output, _ = server.communicate(timeout=75)
            output = "".join(startup) + output
            assert client.returncode == 0, client.stdout + client.stderr
            assert server.returncode == 0, ready + output
            client_row = summary(client.stdout, "duplex-client", args.bytes)
            server_row = summary(output, "duplex-server", args.bytes)
            diagnostics(client.stdout + client.stderr, "active", client_row)
            diagnostics(output, "passive", server_row)
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
