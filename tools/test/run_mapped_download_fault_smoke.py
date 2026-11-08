#!/usr/bin/env python3
"""Isolated CLI failures must not publish; surviving checkpoints must resume."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import file_sha256, free_port, stop_server, wait_for_control


def established_connections(pid):
    inodes = set()
    for descriptor in Path(f"/proc/{pid}/fd").iterdir():
        try:
            link = os.readlink(descriptor)
        except FileNotFoundError:
            continue
        if link.startswith("socket:["):
            inodes.add(link[8:-1])
    return sum(row.split()[3] == "01" and row.split()[9] in inodes
               for table in ("tcp", "tcp6")
               for row in Path(f"/proc/{pid}/net/{table}").read_text().splitlines()[1:])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--fault-library", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    mem = dict(line.split(":", 1) for line in Path("/proc/meminfo").read_text().splitlines())
    assert int(mem["MemAvailable"].split()[0]) >= 12 * 1024 ** 2
    assert os.statvfs("/dev/shm").f_bavail * os.statvfs("/dev/shm").f_frsize >= 3 * 1024 ** 3
    env = {k: v for k, v in os.environ.items() if not k.startswith("CPNETFLUX_")}
    env.update(CPNETFLUX_STAGE_READ_CONCURRENCY="0", CPNETFLUX_STAGE_WRITE_CONCURRENCY="0",
               CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION="inline", CPNETFLUX_RANGE_DIRECT_WRITE="parallel",
               CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE="mmap")
    reports = []
    with tempfile.TemporaryDirectory(prefix="cpnetflux-mapped-fault-", dir="/dev/shm") as directory:
        root = Path(directory)
        source = root / "server/payload/payload.bin"
        source.parent.mkdir(parents=True)
        block = bytes(range(256)) * 4096
        with source.open("wb") as output:
            for _ in range(1024):
                output.write(block)
        expected = file_sha256(source)
        port, log = free_port(), root / "server.log"
        with log.open("w") as output:
            server = subprocess.Popen([str(build / "cpnetflux-gridftp-server"),
                "--host", "127.0.0.1", "--port", str(port), "--root", str(root / "server"),
                "--data-port-base", str(clamp_passive_data_port_base(free_port())),
                "--connections", "4", "--checksum", "crc32c", "--auth-mode", "anonymous",
                "--tls-mode", "off", "--data-tls-mode", "off"],
                stdout=output, stderr=subprocess.STDOUT, env=env)
        try:
            wait_for_control(port)
            for mode in ("fail", "truncate"):
                destination = root / mode
                summary = root / (mode + ".json")
                command = [str(build / "cpnetflux-tree-download-client"), "--host", "127.0.0.1",
                    "--local-bind-address", "127.0.0.1", "--port", str(port),
                    "--source-dir", "payload", "--dest-dir", str(destination),
                    "--connections", "4", "--file-parallelism", "4", "--control-reuse", "worker",
                    "--data-session-reuse", "tree", "--posix-write-strategy", "direct",
                    "--file-io-buffer-size", "0", "--checksum", "crc32c", "--buffer-size", "262144",
                    "--range-checkpoint-batch-chunks", "16", "--final-verify-policy", "verified_chunks",
                    "--phase-timing", "on", "--transfer-timeout-ms", "15000", "--json-summary", str(summary)]
                faulty = env | {"LD_PRELOAD": str(args.fault_library.resolve()),
                                "CPNETFLUX_MAPPED_FAULT_ROOT": str(destination) + "/",
                                "CPNETFLUX_MAPPED_FAULT": mode}
                result = subprocess.run(command, env=faulty, capture_output=True, text=True, timeout=25)
                assert result.returncode == (1 if mode == "fail" else -signal.SIGBUS), (mode, result)
                if mode == "fail":
                    assert "cpnetflux-test: injected mmap ENOMEM" in result.stderr, result.stderr
                assert not (destination / "payload.bin").exists(), mode
                if summary.exists():
                    assert json.loads(summary.read_text())["result"] == "fail"
                deadline = time.monotonic() + 5
                while established_connections(server.pid) and time.monotonic() < deadline:
                    time.sleep(0.05)
                assert not established_connections(server.pid), "peer sockets did not exit after EOF"
                manifests = list(destination.rglob("*manifest*"))
                retained = [p for p in manifests if p.is_file() and "verified_chunks=" in p.read_text()]
                assert retained, (mode, result.stderr, summary.read_text() if summary.exists() else "no summary",
                                  list(root.rglob("*")))
                if mode == "truncate":
                    assert any("verified_chunks=\n" not in p.read_text() for p in retained)
                resumed = subprocess.run(command + ["--resume"], env=env, capture_output=True, text=True, timeout=25)
                assert resumed.returncode == 0, (mode, resumed.stderr)
                receipt = json.loads(summary.read_text())
                assert receipt["result"] == "pass" and receipt["actual_transport"] == "v3", receipt
                assert file_sha256(destination / "payload.bin") == expected
                timings = receipt["file_timings"]
                assert timings and all(t["receiver_write_backend"] == "mmap_experimental" for t in timings)
                assert all(t["mapped_write"]["active_mapped_bytes"] == 0 for t in timings)
                if mode == "truncate":
                    assert receipt["resume_skipped_bytes"] > 0, receipt
                reports.append({"fault": mode, "exit": result.returncode,
                                "resume": "pass", "peer_established_after_eof": 0,
                                "skipped_bytes": receipt["resume_skipped_bytes"]})
                # Only this test's private directory and files are removed.
                for path in sorted(destination.rglob("*"), reverse=True):
                    path.unlink() if path.is_file() else path.rmdir()
                destination.rmdir()
        finally:
            stop_server(server, log)
    print(json.dumps({"result": "pass", "faults": reports, "payload_cleaned": True}))


if __name__ == "__main__":
    main()
