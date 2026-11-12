#!/usr/bin/env python3
"""Bounded real-loopback correctness; loopback is not a zero-copy speed test."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def stop(process):
    if process.poll() is None:
        process.kill()
    process.communicate(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    args = parser.parse_args()
    binary = args.build_dir.resolve() / "cpnetflux-tcp-send-bench"
    shim = args.build_dir.resolve() / "libcpnetflux_tcp_send_fault_shim.so"
    rows = []
    with tempfile.TemporaryDirectory(prefix="cpnetflux-tcp-send-smoke-") as folder:
        root = Path(folder)
        # Vary every region; never compare two paths using one constant hot buffer.
        pattern = hashlib.shake_256(b"cpnetflux-send32").digest(65537)
        for index, (mode, lanes, size, fault) in enumerate(
            [(m, n, size, "") for m in ("copied", "zerocopy") for n in (1, 4)
             for size in (n, 2 * 1024 * 1024 + 37)] +
            [("zerocopy", 1, 2 * 1024 * 1024 + 37, f)
             for f in ("retry", "zero", "enobufs", "peer", "corrupt", "drop", "ctrunc", "unrelated")]
        ):
            source, target = root / f"source-{index}", root / f"target-{index}"
            source.write_bytes((pattern * ((size + len(pattern) - 1) // len(pattern)))[:size])
            expected = digest(source)
            port = free_port()
            def command(role, path):
                return [str(binary), role, "127.0.0.1", "127.0.0.1", str(port),
                        str(lanes), str(size), mode, str(path), "3000"]
            server = subprocess.Popen(command("server", target), stdout=subprocess.PIPE,
                                      stderr=subprocess.PIPE, text=True)
            try:
                assert select.select([server.stdout], [], [], 5)[0], "readiness timeout"
                assert server.stdout.readline().strip() == "CPNETFLUX_TCP_SEND_READY"
                env = {k: v for k, v in os.environ.items() if not k.startswith("CPNETFLUX_")}
                if fault:
                    env.update(LD_PRELOAD=str(shim), CPNETFLUX_TCP_SEND_TEST_FAULT=fault)
                client = subprocess.run(command("client", source), env=env, capture_output=True,
                                        text=True, timeout=10)
                out, error = server.communicate(timeout=10)
                if fault in ("zero", "enobufs", "peer", "corrupt", "drop", "ctrunc", "unrelated"):
                    assert client.returncode != 0 and server.returncode != 0, (fault, client.stdout, out)
                    assert '"receiver_acknowledged":true' not in client.stdout
                    if fault == "enobufs":
                        assert "ENOBUFS; no fallback" in client.stderr
                    if fault == "drop":
                        assert "deadline expired" in client.stderr, client.stderr
                    if fault == "corrupt":
                        assert "DATA CRC mismatch" in error, error
                    if fault == "ctrunc":
                        assert "truncated error-queue control" in client.stderr, client.stderr
                    if fault == "unrelated":
                        assert "unrelated socket error" in client.stderr, client.stderr
                else:
                    assert client.returncode == 0, client.stderr
                    assert server.returncode == 0, error
                    sent, received = json.loads(client.stdout), json.loads(out)
                    for row in (sent, received):
                        assert row["bytes"] == size and row["v3"] and row["receiver_acknowledged"]
                        assert row["diagnostic_only"] and not row["checkpoint_publication"]
                    assert sent["frames"] == received["frames"]
                    assert digest(target) == expected
                    assert sent["pool_allocated_bytes"] == lanes * 32 * 69632
                    if mode == "zerocopy":
                        assert sent["successful_send_calls"] == sent["copied_release_calls"] + sent["unmarked_release_calls"]
                        assert sent["copied_release_calls"] > 0
                    if fault == "retry":
                        assert sent["retry_calls"] >= 2 and sent["short_writes"] >= 1
                assert digest(source) == expected
                rows.append({"mode": mode, "lanes": lanes, "bytes": size, "fault": fault or None})
            finally:
                stop(server)
                source.unlink(missing_ok=True)
                target.unlink(missing_ok=True)
        existing = root / "existing"
        existing.write_bytes(b"keep")
        bad = [str(binary), "server", "127.0.0.1", "127.0.0.1", "12345", "1", "4", "copied", str(existing), "1000"]
        assert subprocess.run(bad, capture_output=True, timeout=5).returncode != 0
        assert existing.read_bytes() == b"keep"
        for position, value in ((1, "bad"), (2, "hostname"), (4, "0"), (5, "5"),
                                (6, "1073741825"), (7, "rdma"), (9, "0")):
            invalid = bad.copy()
            invalid[position] = value
            assert subprocess.run(invalid, capture_output=True, timeout=5).returncode != 0
        alias = root / "alias"
        alias.symlink_to(existing)
        bad[1] = "client"
        bad[8] = str(alias)
        failure = subprocess.run(bad, capture_output=True, text=True, timeout=5)
        assert failure.returncode != 0 and "safe file open failed" in failure.stderr
    print(json.dumps({"cases": rows, "owned_fixtures_removed": True,
                      "loopback_correctness_only": True, "existing_and_invalid_rejected": True}))


if __name__ == "__main__":
    main()
