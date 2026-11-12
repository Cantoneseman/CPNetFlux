#!/usr/bin/env python3
"""Serial R770 pair attribution; no payload files or production processes."""
import argparse
import json
import os
import re
from pathlib import Path
import shlex
import signal
import statistics
import subprocess
import time

PROJECT = Path("/home/Su/projects/CPNetFlux")
ROOT = Path("/home/Su/experiments/CPNetFlux/2026-11-08-v3-memory-attribution-04")
INTERFACE = Path("/sys/class/net/eno17095np0/statistics")
MIN_MEMORY = 12 * 1024**3


def snapshot(peer):
    memory = {line.split()[0].rstrip(":"): int(line.split()[1]) * 1024
              for line in Path("/proc/meminfo").read_text().splitlines() if line.endswith(" kB")}
    vm = {line.split()[0]: int(line.split()[1]) for line in Path("/proc/vmstat").read_text().splitlines()
          if line.startswith(("pswpin ", "pswpout "))}
    route = subprocess.check_output(["ip", "route", "get", peer], text=True).strip()
    return {"available": memory["MemAvailable"], "vm": vm, "route": route,
            "net": {name: int((INTERFACE / name).read_text()) for name in ("rx_bytes", "tx_bytes")}}


def owned_usage(pid):
    # strace diagnostic runs have a child benchmark; include it in resource monitoring.
    pids = [pid]
    for parent in pids:
        try:
            pids.extend(int(value) for value in Path(f"/proc/{parent}/task/{parent}/children").read_text().split())
        except FileNotFoundError:
            continue
    swap = rss = observed = 0
    for process in pids:
        try:
            fields = {line.split()[0]: int(line.split()[1]) for line in Path(f"/proc/{process}/status").read_text().splitlines()
                      if line.startswith(("VmSwap:", "VmRSS:"))}
        except FileNotFoundError:
            continue
        observed += 1
        swap += fields.get("VmSwap:", 0)
        rss += fields.get("VmRSS:", 0)
    return {"swap_kib": swap, "rss_kib": rss, "observed": observed}


def monitor(process, before, output, trace):
    minimum, swap, rss, observations = before["available"], 0, 0, 0
    deadline = time.monotonic() + 180
    failure = ""
    while process.poll() is None:
        memory = next(int(line.split()[1]) * 1024 for line in Path("/proc/meminfo").read_text().splitlines()
                      if line.startswith("MemAvailable:"))
        usage = owned_usage(process.pid)
        minimum = min(minimum, memory)
        swap = max(swap, usage["swap_kib"])
        rss = max(rss, usage["rss_kib"])
        observations += usage["observed"] > 0
        if memory < MIN_MEMORY or swap or time.monotonic() > deadline:
            failure = "memory/swap/deadline guard"
            os.killpg(process.pid, signal.SIGTERM)
            break
        time.sleep(0.1)
    try:
        rc = process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        rc = process.wait(timeout=5)
    text = output.read_text()
    records = [json.loads(line) for line in text.splitlines() if line.startswith("{")]
    return {"rc": rc, "guard_failure": failure, "minimum_available": minimum,
            "owned_swap_kib": swap, "owned_rss_kib": rss, "observations": observations,
            "trace": trace, "benchmark": records[-1] if records else {}}


def launch(side, args, sample, port):
    local, peer = ("192.168.100.1", "192.168.100.2") if side == "server" else ("192.168.100.2", "192.168.100.1")
    before = snapshot(peer)
    if before["available"] < MIN_MEMORY:
        raise RuntimeError("preflight memory guard")
    output = ROOT / f"{sample}.{side}.log"
    command = [str(PROJECT / "build/cpnetflux-tcp-frame-bench"), side, local, peer, str(port),
               str(args.connections), str(args.bytes), args.mode, args.checksum, "65532"]
    if args.trace:
        command = ["strace", "-f", "-c", "-o", str(ROOT / f"{sample}.{side}.strace"), *command]
    log = output.open("w")
    process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    log.close()
    return process, before, output, command


def finish(process, before, output, command, args, peer):
    item = monitor(process, before, output, args.trace)
    after = snapshot(peer)
    item.update(before=before, after=after, command=command,
                pswpout_delta=after["vm"]["pswpout"] - before["vm"]["pswpout"],
                net_delta={name: after["net"][name] - before["net"][name] for name in before["net"]})
    return item


def one(args, sample, port):
    if not re.fullmatch(r"[A-Za-z0-9_-]+", sample):
        raise ValueError("sample name must be a plain identifier")
    if (ROOT / f"{sample}.json").exists():
        raise FileExistsError(f"sample {sample} already exists; preserve prior evidence")
    process, before, output, command = launch("server", args, sample, port)
    remote = None
    try:
        deadline = time.monotonic() + 10
        while "CPNETFLUX_TCP_FRAME_READY" not in output.read_text():
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("server readiness failed: " + output.read_text())
            time.sleep(0.05)
        remote_args = ["python3", str(PROJECT / "tools/perf/run_tcp_frame_memory.py"), "--peer",
                       "--sample", sample, "--port", str(port), "--connections", str(args.connections),
                       "--bytes", str(args.bytes), "--mode", args.mode, "--checksum", args.checksum]
        if args.trace:
            remote_args.append("--trace")
        remote_log = ROOT / f"{sample}.peer.json"
        with remote_log.open("w") as log:
            remote = subprocess.Popen(["ssh", "r770-2", shlex.join(remote_args)], stdout=log, stderr=subprocess.STDOUT)
        server = finish(process, before, output, command, args, "192.168.100.2")
        remote.wait(timeout=190)
        if remote.returncode:
            raise RuntimeError("peer failed: " + remote_log.read_text())
        client = json.loads(remote_log.read_text())
        valid = all(item["rc"] == 0 and not item["guard_failure"] and item["owned_swap_kib"] == 0 and
                    item["pswpout_delta"] == 0 and item["minimum_available"] >= MIN_MEMORY and
                    item["benchmark"].get("logical_bytes") == args.bytes and
                    item["benchmark"].get("receiver_acknowledged") and
                    "eno17095np0" in item["before"]["route"] for item in (server, client))
        valid = valid and server["net_delta"]["rx_bytes"] >= args.bytes and client["net_delta"]["tx_bytes"] >= args.bytes
        row = {"sample": sample, "mode": args.mode, "checksum": args.checksum,
               "connections": args.connections, "bytes": args.bytes, "trace": args.trace,
               "valid": valid, "server": server, "client": client}
        (ROOT / f"{sample}.json").write_text(json.dumps(row, indent=2) + "\n")
        print(json.dumps({"sample": sample, "valid": valid,
                          "gbps": client["benchmark"].get("throughput_gbps"),
                          "cpu": [item["benchmark"].get("process_cpu_seconds") for item in (server, client)]}), flush=True)
        if not valid:
            raise RuntimeError("sample failed resource/correctness gate")
        return row
    finally:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=5)
        if remote is not None and remote.poll() is None:
            remote.terminate()
            remote.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--peer", action="store_true")
    parser.add_argument("--sample", default="")
    parser.add_argument("--port", type=int, default=49400)
    parser.add_argument("--connections", type=int, default=4)
    parser.add_argument("--bytes", type=int, default=128 * 1024**3)
    parser.add_argument("--mode", choices=("raw", "v3"), default="raw")
    parser.add_argument("--checksum", choices=("none", "crc32c"), default="none")
    parser.add_argument("--trace", action="store_true")
    parser.add_argument("--repetitions", type=int, default=5)
    args = parser.parse_args()
    ROOT.mkdir(parents=True, exist_ok=True)
    if args.peer:
        process, before, output, command = launch("client", args, args.sample, args.port)
        print(json.dumps(finish(process, before, output, command, args, "192.168.100.1")))
        return
    if args.sample:
        one(args, args.sample, args.port)
        return
    rows = []
    modes = (("raw", "none"), ("v3", "none"), ("v3", "crc32c"))
    for rep in range(1, args.repetitions + 1):
        for mode, crc in (modes if rep % 2 else tuple(reversed(modes))):
            args.mode, args.checksum = mode, crc
            sample = f"c{args.connections}-{mode}-{crc}-r{rep}"
            rows.append(one(args, sample, args.port))
    summary = []
    for mode, crc in modes:
        group = [row for row in rows if (row["mode"], row["checksum"]) == (mode, crc)]
        speeds = [row["client"]["benchmark"]["throughput_gbps"] for row in group]
        cpu = [sum(row[side]["benchmark"]["process_cpu_seconds"] for side in ("client", "server")) /
               (args.bytes / 1024**3) for row in group]
        cv = 100 * statistics.pstdev(speeds) / statistics.mean(speeds) if len(speeds) > 1 else None
        summary.append({"mode": mode, "checksum": crc, "connections": args.connections,
                        "samples": len(speeds), "median_gbps": statistics.median(speeds),
                        "cv_pct": cv, "stable": len(speeds) >= 5 and cv <= 5,
                        "minimum_gbps": min(speeds), "maximum_gbps": max(speeds),
                        "cpu_seconds_per_gib": statistics.median(cpu), "all_valid": all(row["valid"] for row in group)})
    (ROOT / f"summary-c{args.connections}.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
