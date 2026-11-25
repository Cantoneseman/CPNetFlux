#!/usr/bin/env python3
"""Run one fixed 512 MiB control+data TLS V3 comfort-profile transfer on R770."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import socket
import ssl
import subprocess
import sys
import time

import run_r770_receive_cost as costs
import run_small_file_baseline as baseline

MIB = 1024 * 1024
GIB = 1024 * MIB
SIZE = 512 * MIB
CHUNK = MIB
FRAME = 256 * 1024
RATE = 1_500_000_000
IFACE = "eno17095np0"
RUN_ID = "2026-11-25-r770-tls-comfort-02"
PORT = "45841"
DATA_PORT_BASE = "61420"
ROOT = Path("/dev/shm") / ("cpnetflux-tlscomfort-" + RUN_ID)
OUT = Path("/home/Su/experiments/CPNetFlux") / RUN_ID
PEER_OUT = OUT.with_name(OUT.name + "-peer")
MARKER = ".cpnetflux-tls-comfort-owner"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def hash_file(path: Path) -> dict:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(MIB), b""):
            digest.update(block)
    return {"sha256": digest.hexdigest(), "bytes": path.stat().st_size}


def make_source(path: Path) -> dict:
    block = bytes((index * 131 + 17) % 256 for index in range(MIB))
    digest = hashlib.sha256()
    with path.open("xb") as stream:
        for _ in range(SIZE // MIB):
            stream.write(block)
            digest.update(block)
    return {"sha256": digest.hexdigest(), "bytes": SIZE}


def owned_root(create: bool) -> None:
    if ROOT.parent != Path("/dev/shm") or not ROOT.name.startswith("cpnetflux-tlscomfort-"):
        raise RuntimeError("invalid private tmpfs root")
    if create:
        ROOT.mkdir(mode=0o700)
        (ROOT / MARKER).write_text(RUN_ID + "\n", encoding="ascii")
    else:
        require(ROOT.is_dir() and (ROOT / MARKER).read_text(encoding="ascii") == RUN_ID + "\n",
                "owned tmpfs marker is missing or changed")


def remove_root() -> bool:
    if not ROOT.exists():
        return True
    owned_root(False)
    for child in ROOT.iterdir():
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()
    ROOT.rmdir()
    return not ROOT.exists()


def peer(*args: str) -> list[str]:
    remote = ["python3", "-B", str(Path(__file__).resolve()), "--action", *map(str, args)]
    return ["ssh", "-n", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "r770-2",
            shlex.join(remote)]


def ask(*args: str, timeout: int = 120):
    output = subprocess.check_output(peer(*args), text=True, timeout=timeout)
    return json.loads(output)


def build_receipt(commit: str) -> dict:
    result = baseline.receipt(commit)
    result["runtime_sha256"] = {
        name: baseline.sha(baseline.PROJECT / "build" / name)
        for name in ("cpnetflux-gridftp-server", "cpnetflux-tree-download-client")
    }
    return result


def readiness(cert: Path) -> None:
    context = ssl.create_default_context(cafile=str(cert))
    context.check_hostname = True
    deadline = time.monotonic() + 20
    last_error = None
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("192.168.100.2", int(PORT)), timeout=1) as raw:
                with context.wrap_socket(raw, server_hostname="192.168.100.2") as secure:
                    require(secure.recv(512).startswith(b"220"), "unexpected TLS control greeting")
                    return
        except (OSError, ssl.SSLError) as error:
            last_error = repr(error)
            time.sleep(0.1)
    raise RuntimeError("TLS control listener did not become ready: " + str(last_error))


def make_certificate(root: Path) -> tuple[Path, Path]:
    cert, key = root / "server.crt", root / "server.key"
    subprocess.run([
        "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-sha256",
        "-days", "1", "-keyout", str(key), "-out", str(cert),
        "-subj", "/CN=192.168.100.2", "-addext", "subjectAltName=IP:192.168.100.2",
    ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    key.chmod(0o600)
    return cert, key


def client_command(cert: Path, target: Path, summary: Path) -> list[str]:
    return [str(baseline.PROJECT / "build/cpnetflux-tree-download-client"),
        "--host", "192.168.100.2", "--port", PORT, "--local-bind-address", "192.168.100.1",
        "--source-dir", "source", "--dest-dir", str(target), "--connections", "4",
        "--file-parallelism", "4", "--control-reuse", "worker", "--data-session-reuse", "tree",
        "--data-pending-window", "2", "--checksum", "crc32c", "--chunk-size", str(CHUNK),
        "--buffer-size", "65536", "--data-frame-bytes", str(FRAME), "--data-transport", "tcp",
        "--compression", "off", "--final-verify-policy", "verified_chunks",
        "--commit-sync-policy", "none", "--manifest-flush-policy", "every_n_chunks",
        "--manifest-flush-interval-chunks", "16", "--range-checkpoint-batch-chunks", "16",
        "--posix-write-strategy", "direct", "--file-io-buffer-size", "0",
        "--auth-mode", "anonymous", "--tls-mode", "required", "--tls-ca-file", str(cert),
        "--tls-server-name", "192.168.100.2", "--data-tls-mode", "required",
        "--reconnect-attempts", "0", "--phase-timing", "on", "--json-summary", str(summary)]


def clean_environment(**values: str) -> dict[str, str]:
    env = {key: value for key, value in os.environ.items() if not key.startswith("CPNETFLUX_")}
    env.update(values)
    return env


def verify_pacing(log: Path) -> list[dict]:
    prefix = "CPNETFLUX_SENDER_PACING "
    text = log.read_text(errors="replace") if isinstance(log, Path) else log
    rows = [json.loads(line[len(prefix):]) for line in text.splitlines()
            if line.startswith(prefix)]
    require(rows and {row["lane"] for row in rows} == set(range(4)), "missing four-lane pacing receipts")
    for row in rows:
        require(row["rate_bytes_per_second"] == RATE and row["spacing_violations"] == 0,
                "application pacing receipt failed")
        require(row["wait_backend"] == "timerfd" and row["timer_fd_creates"] == 1,
                "unexpected application pacing backend")
    return rows


def verify_client(summary: dict, target: Path, expected: dict) -> dict:
    require(summary.get("result") == "pass" and summary.get("actual_transport") == "v3",
            "client did not complete V3")
    require(summary.get("connections") == 4 and summary.get("data_transport") == "tcp",
            "unexpected transport or lane count")
    require(summary.get("control_tls_verified") is True and summary.get("data_tls_verified") is True,
            "control and data TLS were not both verified")
    require(summary.get("data_frame_logical_bytes") == FRAME, "256 KiB DATA was not negotiated")
    lanes = summary.get("lane_endpoints", [])
    require(len(lanes) == 4 and {lane["channel_index"] for lane in lanes} == set(range(4)),
            "four lane endpoint receipt missing")
    for lane in lanes:
        require(lane["data_local_address"] == "192.168.100.1" and
                lane["data_peer_address"] == "192.168.100.2", "data lane bypassed direct link")
        require(1_048_576 <= lane["tcp_receive_buffer_bytes"] <= 2_097_152,
                "1 MiB SO_RCVBUF request was not effective")
    timings = [row for row in summary.get("file_timings", []) if row.get("range_id")]
    require(4 <= len(timings) <= 8 and sum(row["length"] for row in timings) == SIZE,
            "range coverage differs from 512 MiB")
    for row in timings:
        require(row["receiver_payload_execution"] == "inline" and
                row["receiver_write_backend"] == "mmap_experimental", "buffered inline mmap missing")
        mapped = row.get("mapped_write", {})
        require(mapped.get("direct_receive_calls", 0) == 0 and
                mapped.get("direct_received_bytes", 0) == 0, "direct-to-mmap receive was enabled")
    actual = hash_file(target / "payload.bin")
    require(actual == expected, "independent destination SHA/size mismatch")
    manifest = target / "payload.bin.cpnetflux.download.manifest"
    fields = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            fields[key] = value
    chunks = [item for item in fields.get("verified_chunks", "").split(",") if item]
    require(fields.get("state") == "committed" and len(chunks) == SIZE // CHUNK,
            "manifest did not commit all 512 verified chunks")
    return {"destination": actual, "verified_chunks": len(chunks), "manifest_state": fields["state"],
            "receive_buffer_bytes": [lane["tcp_receive_buffer_bytes"] for lane in lanes],
            "range_count": len(timings), "direct_receive_calls": 0}


def check_network(before: dict, after: dict, role: str) -> dict:
    problems, delta = costs.comparable(before, after, traffic=True)
    payload_counter = "rx_bytes" if role == "receiver" else "tx_bytes"
    if delta[payload_counter] < SIZE:
        problems.append(role + " direct-link byte count below payload size")
    retrans = after["tcp_retrans"] - before["tcp_retrans"]
    if retrans:
        problems.append(role + " TCP retransmissions " + str(retrans))
    return {"problems": problems, "delta": delta, "tcp_retrans_delta": retrans,
            "rx_queue_packet_delta": costs.changes(before["rx_queue_packets"], after["rx_queue_packets"])}


def run_client(command: list[str], env: dict[str, str], log: Path,
               r1_memory_before: dict, r2_memory_before: dict) -> dict:
    r1_samples, r2_samples = [], []
    last_peer_sample = 0.0
    started = time.monotonic()
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, env=env)
        try:
            while process.poll() is None:
                now = time.monotonic()
                r1 = costs.resource_sample()
                r1_samples.append(r1)
                require(not costs.violations(r1_memory_before, r1),
                        "R1 memory/swap gate failed during transfer")
                if now - last_peer_sample >= 1.0:
                    r2 = ask("resource")
                    r2_samples.append(r2)
                    require(not costs.violations(r2_memory_before, r2),
                            "R2 memory/swap gate failed during transfer")
                    last_peer_sample = now
                if now - started > 180:
                    raise TimeoutError("TLS comfort transfer exceeded 180 seconds")
                time.sleep(0.2)
            return_code = process.wait()
        except BaseException:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            raise
    return {"exit_code": return_code, "elapsed_seconds": time.monotonic() - started,
            "r1_memory_samples": r1_samples, "r2_memory_samples": r2_samples}


def run(commit: str) -> None:
    require(baseline.node() == 1, "run action must execute on R770-1")
    require(re.fullmatch(r"[0-9a-f]{40}", commit) is not None, "full commit SHA required")
    require(not OUT.exists() and not PEER_OUT.exists() and not ROOT.exists(), "task-owned path already exists")
    OUT.mkdir(mode=0o700, parents=True)
    (OUT / "run-id.txt").write_text(RUN_ID + "\n", encoding="ascii")
    owned_root(True)
    peer_initialized = False
    server_started = False
    local_identity = None
    remote_identity = None
    try:
        local_identity = costs.identity()
        remote_identity = ask("identity")
        (OUT / "preflight-identity.json").write_text(
            json.dumps({"r1": local_identity, "r2": remote_identity}, indent=2))
        local_build = build_receipt(commit)
        remote_build = ask("receipt", commit)
        require(local_build == remote_build, "R1/R2 commit or runtime SHA mismatch")
        (OUT / "build-receipt.json").write_text(json.dumps({"r1": local_build, "r2": remote_build}, indent=2))
        require(shutil.disk_usage("/dev/shm").free > SIZE + GIB, "insufficient R1 tmpfs capacity")
        before = {"r1": costs.snapshot()}
        require(before["r1"]["available_kib"] * 1024 > 12 * GIB + SIZE + GIB,
                "insufficient R1 available memory")
        remote_before = ask("snapshot")
        require(remote_before["available_kib"] * 1024 > 12 * GIB + SIZE + GIB,
                "insufficient R2 available memory")
        costs.require_capacity(before["r1"], SIZE)
        source_dir = ROOT / "server-root" / "source"
        target = ROOT / "download"
        source_dir.mkdir(parents=True)
        target.mkdir()
        cert, key = make_certificate(ROOT)
        peer_preflight = ask("peer-preflight", str(ROOT), RUN_ID)
        remote_identity = peer_preflight["identity"]
        (OUT / "preflight-identity.json").write_text(
            json.dumps({"r1": local_identity, "r2": remote_identity}, indent=2))
        peer_initialized = True
        remote_paths = ask("prepare", str(ROOT), RUN_ID)
        subprocess.run(["scp", str(cert), f"r770-2:{remote_paths['cert']}"], check=True)
        subprocess.run(["scp", str(key), f"r770-2:{remote_paths['key']}"], check=True)
        expected = ask("seed", SIZE)
        source = source_dir / "payload.bin"
        expected_local = make_source(source)
        require(expected_local == expected, "R1/R2 deterministic payload SHA mismatch")
        (OUT / "source.json").write_text(json.dumps(expected, indent=2))
        control_log = OUT / "client-control.log"
        server_started = True
        print(json.dumps({"stage": "start_tls_server", "run_id": RUN_ID}), flush=True)
        remote_start = ask("start", commit, timeout=30)
        (OUT / "server-start.json").write_text(json.dumps(remote_start, indent=2))
        readiness(cert)
        remote_before = ask("snapshot")
        remote_memory_before = ask("resource")
        local_memory_before = costs.resource_sample()
        env = clean_environment(
            CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION="inline",
            CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE="mmap",
            CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE="off",
            CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES="1048576",
            CPNETFLUX_RECEIVER_WRITE_BATCH="1",
            CPNETFLUX_STAGE_READ_CONCURRENCY="0",
            CPNETFLUX_STAGE_WRITE_CONCURRENCY="0",
            CPNETFLUX_RANGE_DIRECT_WRITE="parallel")
        summary_path = OUT / "client-summary.json"
        command = client_command(cert, target, summary_path)
        (OUT / "client-command.json").write_text(json.dumps(command, indent=2))
        before["r1-transfer"] = costs.snapshot()
        execution = run_client(command, env, control_log, local_memory_before, remote_memory_before)
        elapsed = execution["elapsed_seconds"]
        after_r1 = costs.snapshot()
        remote_after = ask("snapshot")
        server_result = ask("stop-and-receipt")
        server_started = False
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        integrity = verify_client(summary, target, expected)
        require(execution["exit_code"] == 0, control_log.read_text(errors="replace")[-8000:])
        pacing = verify_pacing(server_result["server_log"])
        r1_network = check_network(before["r1-transfer"], after_r1, "receiver")
        r2_network = server_result["server_network"]
        integrity["network"] = {"r1": r1_network, "r2": r2_network}
        require(not r1_network["problems"] and not r2_network["problems"],
                str({"r1": r1_network["problems"], "r2": r2_network["problems"]}))
        require(not costs.violations(before["r1-transfer"], after_r1), "R1 memory/swap gate failed")
        require(not costs.violations(remote_memory_before, ask("resource")), "R2 memory/swap gate failed")
        require(not server_result["memory_violations"], "R2 memory/swap gate failed")
        report = {
            "result": "pass", "kind": "functional TLS compatibility, not performance comparison",
            "run_id": RUN_ID, "commit": commit, "profile": {
                "data_transport": "TCP", "lanes": 4, "receiver": "buffered inline mmap",
                "direct_to_mmap": "off", "data_frame_bytes": FRAME, "receive_buffer_request": MIB,
                "application_pacing_bytes_per_second_per_lane": RATE, "checksum": "CRC32C",
                "chunk_bytes": CHUNK, "checkpoint_chunks": 16, "filesystem": "tmpfs",
                "control_tls": "required", "data_tls": "required", "auth": "anonymous",
            },
            "input": expected, "integrity": integrity, "elapsed_seconds": elapsed,
            "memory_samples": {"r1": execution["r1_memory_samples"],
                               "r2": execution["r2_memory_samples"]},
            "pacing_receipts": pacing, "server": server_result,
            "resource": {"r1_before": before["r1"], "r1_after": after_r1,
                         "r2_before": remote_before, "r2_after": remote_after,
                         "r2_memory_before": remote_memory_before},
            "system_identity_before": {"r1": local_identity, "r2": remote_identity},
            "cleanup": {"pending": True},
        }
        (OUT / "result.json").write_text(json.dumps(report, indent=2))
    finally:
        if server_started:
            try:
                ask("stop")
            except Exception:
                pass
        if peer_initialized:
            try:
                remote_cleanup = ask("cleanup")
                (OUT / "peer-cleanup.json").write_text(json.dumps(remote_cleanup, indent=2))
            except Exception as error:
                (OUT / "peer-cleanup-error.txt").write_text(repr(error))
        removed = remove_root()
        identity_after = costs.identity() if local_identity is not None else None
        peer_identity_after = ask("identity") if peer_initialized and remote_identity is not None else None
        identities_match = (local_identity == identity_after and remote_identity == peer_identity_after
                            if local_identity is not None and remote_identity is not None else False)
        if OUT.exists():
            (OUT / "postflight-identity.json").write_text(
                json.dumps({"r1": identity_after, "r2": peer_identity_after,
                            "r1_matches": local_identity == identity_after,
                            "r2_matches": remote_identity == peer_identity_after}, indent=2))
        if (OUT / "result.json").exists():
            report = json.loads((OUT / "result.json").read_text())
            report["cleanup"] = {"r1_tmpfs_removed": removed,
                                 "r2": json.loads((OUT / "peer-cleanup.json").read_text())
                                     if (OUT / "peer-cleanup.json").exists() else "failed",
                                 "system_identity_restored": identities_match}
            (OUT / "result.json").write_text(json.dumps(report, indent=2))
        peer_clean = (not peer_initialized or
                      ((OUT / "peer-cleanup.json").exists() and
                       json.loads((OUT / "peer-cleanup.json").read_text()).get("r2_tmpfs_removed") is True))
        cleanup_ok = removed and (identities_match if peer_initialized else True) and peer_clean
        if (OUT / "result.json").exists():
            report = json.loads((OUT / "result.json").read_text())
            report["cleanup"]["complete"] = cleanup_ok
            (OUT / "result.json").write_text(json.dumps(report, indent=2))
        require(cleanup_ok, "cleanup or system identity verification failed")


def peer_action(action: str, args: list[str]) -> None:
    require(baseline.node() == 2, "peer action must execute on R770-2")
    if action == "receipt":
        print(json.dumps(build_receipt(args[0])))
    elif action == "snapshot":
        print(json.dumps(costs.snapshot()))
    elif action == "resource":
        print(json.dumps(costs.resource_sample()))
    elif action == "identity":
        print(json.dumps(costs.identity()))
    elif action == "peer-preflight":
        require(args[0] == str(ROOT) and args[1] == RUN_ID, "unexpected peer task identity")
        require(not OUT.exists() and not ROOT.exists(), "peer task-owned path already exists")
        require(shutil.disk_usage("/dev/shm").free > SIZE + GIB, "insufficient R2 tmpfs capacity")
        memory = costs.resource_sample()
        require(memory["available_kib"] * 1024 > 12 * GIB + SIZE + GIB,
                "insufficient R2 available memory")
        print(json.dumps({"identity": costs.identity(), "tmpfs_free": shutil.disk_usage("/dev/shm").free,
                          "memory": memory}))
    elif action == "prepare":
        require(args[0] == str(ROOT), "unexpected peer tmpfs root")
        run_id = args[1]
        require(run_id == RUN_ID and not OUT.exists() and not ROOT.exists(), "peer task path already exists")
        require(shutil.disk_usage("/dev/shm").free > SIZE + GIB, "insufficient R2 tmpfs capacity")
        OUT.mkdir(mode=0o700, parents=True)
        owned_root(True)
        print(json.dumps({"root": str(ROOT), "cert": str(ROOT / "server.crt"), "key": str(ROOT / "server.key")}))
    elif action == "seed":
        source = ROOT / "server-root" / "source"
        source.mkdir(parents=True)
        print(json.dumps(make_source(source / "payload.bin")))
    elif action == "start":
        require(args[0] == baseline.output("git", "-C", str(baseline.PROJECT), "rev-parse", "HEAD"),
                "R2 source commit changed")
        cert, key = ROOT / "server.crt", ROOT / "server.key"
        key.chmod(0o600)
        server_log = OUT / "server.log"
        event_log = OUT / "server-events.jsonl"
        argv = [str(baseline.PROJECT / "build/cpnetflux-gridftp-server"),
            "--root", str(ROOT / "server-root"), "--host", "192.168.100.2", "--port", PORT,
            "--data-port-base", DATA_PORT_BASE, "--connections", "4", "--chunk-size", str(CHUNK),
            "--buffer-size", "65536", "--checksum", "crc32c", "--manifest-flush-policy", "every_n_chunks",
            "--manifest-flush-interval-chunks", "16", "--final-verify-policy", "verified_chunks",
            "--commit-sync-policy", "none", "--access-policy", "read-only", "--auth-mode", "anonymous",
            "--tls-mode", "required", "--tls-client-auth", "none", "--tls-cert-file", str(cert),
            "--tls-key-file", str(key), "--data-tls-mode", "required", "--data-transport", "tcp",
            "--event-log", str(event_log)]
        env = clean_environment(CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND=str(RATE),
                                CPNETFLUX_RANGE_CHECKPOINT_BATCH="16",
                                CPNETFLUX_RANGE_DIRECT_WRITE="parallel")
        with server_log.open("w", encoding="utf-8") as output:
            process = subprocess.Popen(argv, stdout=output, stderr=subprocess.STDOUT,
                                       env=env, start_new_session=True)
        (OUT / "server.pid").write_text(str(process.pid) + "\n", encoding="ascii")
        (OUT / "server.start_ticks").write_text(str(baseline.proc_info(process.pid)["start_ticks"]),
                                                 encoding="ascii")
        (OUT / "server.before.json").write_text(json.dumps(costs.snapshot()))
        (OUT / "server.memory-before.json").write_text(json.dumps(costs.resource_sample()))
        print(json.dumps({"pid": process.pid, "command": argv}))
    elif action == "stop":
        stop_peer_server()
        print(json.dumps({"stopped": True}))
    elif action == "stop-and-receipt":
        stop_peer_server()
        log = (OUT / "server.log").read_text(errors="replace")
        counters_before = json.loads((OUT / "server.before.json").read_text())
        counters_after = costs.snapshot()
        memory_before = json.loads((OUT / "server.memory-before.json").read_text())
        memory_after = costs.resource_sample()
        receipt = {"server_log": log, "server_counter_before": counters_before,
                   "server_counter_after": counters_after,
                   "server_network": check_network(counters_before, counters_after, "sender"),
                   "memory_violations": costs.violations(memory_before, memory_after)}
        (OUT / "server.receipt.json").write_text(json.dumps(receipt, indent=2))
        print(json.dumps(receipt))
    elif action == "cleanup":
        stop_peer_server()
        removed = remove_root()
        require(removed, "failed to remove R2 task-owned tmpfs root")
        print(json.dumps({"r2_tmpfs_removed": removed, "server_evidence": str(OUT)}))
    else:
        raise RuntimeError("unknown peer action: " + action)


def stop_peer_server() -> None:
    pid_file = OUT / "server.pid"
    if not pid_file.exists():
        return
    pid = int(pid_file.read_text())
    expected_start = int((OUT / "server.start_ticks").read_text())
    try:
        actual_start = baseline.proc_info(pid)["start_ticks"]
    except (FileNotFoundError, ProcessLookupError):
        actual_start = None
    if actual_start != expected_start:
        return
    os.killpg(pid, signal.SIGTERM)
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        try:
            baseline.proc_info(pid)
            state = Path("/proc") / str(pid) / "stat"
            fields = state.read_text().rsplit(")", 1)[1].split()
            if fields[0] == "Z":
                return
        except (FileNotFoundError, ProcessLookupError):
            return
        time.sleep(0.05)
    os.killpg(pid, signal.SIGKILL)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--action", choices=("run", "receipt", "snapshot", "resource", "identity",
        "peer-preflight", "prepare", "seed",
        "start", "stop", "stop-and-receipt", "cleanup"), required=True)
    parser.add_argument("args", nargs="*")
    args = parser.parse_args()
    if args.action == "run":
        run(*args.args)
    else:
        peer_action(args.action, args.args)


if __name__ == "__main__":
    main()
