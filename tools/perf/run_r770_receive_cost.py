#!/usr/bin/env python3
"""Fixed R770 receive A/B and, if admitted, one 5 GiB pilot."""
import contextlib
import hashlib
import json
import os
from pathlib import Path
import random
import re
import resource
import shlex
import shutil
import signal
import statistics
import subprocess
import sys
import time

import receive_network as network
import run_small_file_baseline as baseline
from run_summary_default_gate import verify_chunks

GIB = 1024**3
MIB = 1024**2
RUN = "2026-11-24-r770-receive-cost-01"
ROOT = Path("/dev/shm") / ("cpnetflux-s0-" + RUN)
OUT = Path("/home/Su/experiments/CPNetFlux") / RUN
PORT = "45483"
BASE = "61320"
IFACE = "eno17095np0"
HZ = os.sysconf("SC_CLK_TCK")
ERRORS = ("rx_missed_errors", "rx_dropped", "tx_dropped", "rx_errors", "tx_errors")
MLX = ("rx_out_of_buffer", "rx_crc_errors_phy", "rx_discards_phy", "tx_discards_phy")
HCA = ("port_rcv_errors", "port_xmit_discards", "port_rcv_remote_physical_errors",
       "local_link_integrity_errors", "excessive_buffer_overrun_errors",
       "link_downed", "symbol_error")


def record(path, value):
    with (OUT / path).open("x") as stream:
        json.dump(value, stream, indent=2)
        stream.write("\n")


def entries(path):
    result = {}
    for line in Path(path).read_text().splitlines():
        fields = line.split()
        if len(fields) > 1 and fields[1].isdigit():
            result[fields[0].rstrip(":")] = int(fields[1])
    return result


def resource_sample():
    mem = entries("/proc/meminfo")
    pressure = Path("/proc/pressure/memory").read_text()
    cg_root = Path("/sys/fs/cgroup")
    cg = cg_root / Path("/proc/self/cgroup").read_text().strip().split("::", 1)[1].lstrip("/")
    groups = {}
    cgroup_path = str(cg)
    for path in (cg, *cg.parents):
        if not (path == cg_root or cg_root in path.parents):
            break
        if (path / "memory.events").exists():
            groups[str(path)] = {
                "events": entries(path / "memory.events"),
                **{key: (path / key).read_text().strip() for key in (
                    "memory.current", "memory.high", "memory.max", "memory.swap.current")},
            }
    return {
        "available_kib": mem["MemAvailable"], "free_kib": mem["MemFree"],
        "swap_free_kib": mem["SwapFree"], "pswpout": baseline.pswpout(),
        "psi_full_us": int(re.search(r"(?m)^full .*total=(\d+)", pressure)[1]),
        "cgroup_path": cgroup_path,
        "tmpfs_free": shutil.disk_usage("/dev/shm").free, "cgroups": groups,
    }


def violations(before, after, full_psi_tolerance_us=0):
    reasons = []
    if after["available_kib"] < 12 * GIB // 1024 or after["tmpfs_free"] <= 0:
        reasons.append("available memory or tmpfs")
    if after["psi_full_us"] - before["psi_full_us"] > full_psi_tolerance_us:
        reasons.append("host full memory PSI")
    cgroup_path = before.get("cgroup_path") or after.get("cgroup_path")
    if before.get("cgroup_path") and after.get("cgroup_path") and before["cgroup_path"] != after["cgroup_path"]:
        reasons.append("experiment cgroup scope changed")
    if cgroup_path is None and before["cgroups"]:
        common = before["cgroups"].keys() & after["cgroups"].keys()
        cgroup_path = max(common, key=lambda path: path.count("/"), default=None)
    if cgroup_path is not None:
        a = before["cgroups"].get(cgroup_path)
        b = after["cgroups"].get(cgroup_path)
        if a is None or b is None:
            reasons.append("experiment cgroup resource counters unavailable")
        elif a["events"] != b["events"] or int(b["memory.swap.current"]) > int(a["memory.swap.current"]):
            reasons.append(cgroup_path + " memory events or swap")
    elif before["cgroups"] or after["cgroups"]:
        reasons.append("experiment cgroup scope unavailable")
    return reasons


def require_capacity(value, size):
    reserve = size + 256 * MIB
    if value["available_kib"] * 1024 < 12 * GIB + reserve or value["tmpfs_free"] < reserve:
        raise RuntimeError("insufficient memory or tmpfs headroom")
    for group in value["cgroups"].values():
        for key in ("memory.max", "memory.high"):
            if group[key] != "max" and int(group[key]) - int(group["memory.current"]) < reserve:
                raise RuntimeError("insufficient cgroup headroom")


def snapshot():
    value = resource_sample()
    stats = Path("/sys/class/net") / IFACE / "statistics"
    mlx = network.parse_ethtool(network.command("ethtool", "-S", IFACE))
    hca = Path("/sys/class/infiniband/rocep11s0/ports/1/counters")
    value["counters"] = {
        **{key: int((stats / key).read_text()) for key in (*ERRORS, "rx_bytes", "tx_bytes")},
        **{key: mlx[key] for key in MLX},
        **{key: int((hca / key).read_text()) for key in HCA},
    }
    value["rx_queue_packets"] = {key: val for key, val in mlx.items()
                                 if re.fullmatch(r"rx\d+_packets", key)}
    cpu = Path("/proc/stat").read_text().splitlines()[0].split()
    value["host_busy_cpu_s"] = sum(int(cpu[i]) for i in (1, 2, 3, 6, 7, 8)) / HZ
    value["tcp_retrans"] = network.parse_netstat(Path("/proc/net/snmp").read_text())["Tcp.RetransSegs"]
    return value


def changes(before, after):
    return {key: after[key] - before[key] for key in before}


def comparable(before, after, traffic=False, full_psi_tolerance_us=0):
    problems = violations(before, after, full_psi_tolerance_us)
    delta = changes(before["counters"], after["counters"])
    problems += [f"{key} {value:+d}" for key, value in delta.items()
                 if key not in ("rx_bytes", "tx_bytes") and value]
    if traffic and not (delta["rx_bytes"] > 0 and delta["tx_bytes"] > 0):
        problems.append("expected direct-interface traffic missing")
    return problems, delta


def identity():
    here = baseline.node()
    local, peer = f"192.168.100.{here}", f"192.168.100.{3-here}"
    ports = network.command("ss", "-Hlnp", "sport", "=", "2121")
    service = re.search(r"pid=(\d+)", ports)
    if not service or local + ":2121" not in ports:
        raise RuntimeError("expected original service listener absent")
    route = network.command("ip", "route", "get", peer, "from", local)
    if "dev " + IFACE not in route or network.command(
            "ss", "-Htn", "state", "established", "( sport = :2121 or dport = :2121 )").strip():
        raise RuntimeError("route or active 2121 session")
    running = network.command("ps", "-eo", "pid=,comm=,args=")
    app_names = {"cpnetflux-gridf", "cpnetflux-tree-", "globus-url-copy", "globus-gridftp-"}
    for line in running.splitlines():
        pid, name, *_ = line.split(maxsplit=2)
        if name in app_names and pid != service[1]:
            raise RuntimeError("another transfer process: " + pid + " " + name)
    nic = network.inventory()
    current = nic["ring"].split("Current hardware settings:", 1)[1]
    if re.findall(r"(?m)^(?:RX|TX):\s*(\d+)", current) != ["1024", "1024"]:
        raise RuntimeError("unexpected NIC ring settings")
    return {
        "route": route, "link": network.command("ip", "-o", "link", "show", "dev", IFACE),
        "rss_fields": network.command("ethtool", "-n", IFACE, "rx-flow-hash", "tcp4"),
        "nic": nic, "production_pid": int(service[1]),
        "production_start": baseline.proc_info(int(service[1]))["start_ticks"],
        "thp": {name: Path("/sys/kernel/mm/transparent_hugepage", name).read_text().strip()
                for name in ("enabled", "shmem_enabled")},
        "swap_devices": Path("/proc/swaps").read_text().splitlines()[0:1] + [
            line.split()[0] for line in Path("/proc/swaps").read_text().splitlines()[1:]],
    }


def peer(*args):
    return ["ssh", "-n", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", "r770-2",
            shlex.join(["python3", "-B", str(Path(__file__).resolve()), *map(str, args)])]


def ask(*args):
    return json.loads(subprocess.check_output(peer(*args), text=True, timeout=65))


def sha(path):
    start = time.monotonic()
    with path.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"sha256": digest, "bytes": path.stat().st_size, "sha_seconds": time.monotonic() - start}


def seed(size):
    baseline.check_root(ROOT)
    before = resource_sample()
    require_capacity(before, size)
    source = baseline.owned(ROOT, "source")
    source.mkdir()
    rng = random.Random(20261124)
    start = time.monotonic()
    with (source / "f000000.bin").open("xb") as stream:
        for i in range(size // MIB):
            stream.write(rng.randbytes(MIB))
            if i % 64 == 63 and (reasons := violations(before, resource_sample())):
                raise RuntimeError(str(reasons))
    value = sha(source / "f000000.bin")
    value["generate_and_sha_seconds"] = time.monotonic() - start
    after = resource_sample()
    value.update(before=before, after=after)
    value["violations"] = violations(before, after)
    print(json.dumps(value), flush=True)


def environment(arm, profile):
    env = baseline.environment(receiver_profile=profile, range_timing=profile)
    env.update(CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION="inline",
               CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE=arm,
               CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES="1048576",
               CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND="1500000000",
               CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND="off",
               CPNETFLUX_SENDER_CPU_PROFILE="1" if profile else "0",
               CPNETFLUX_MAPPED_WRITE_PROFILE="1" if profile and arm == "mmap" else "0")
    return env


@contextlib.contextmanager
def owned_process(argv, log, env):
    with log.open("xb") as stream:
        proc = subprocess.Popen(argv, stdout=stream, stderr=subprocess.STDOUT,
                                env=env, start_new_session=True)
        try:
            yield proc
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait()


def observe(proc, readings, initial):
    value = resource_sample()
    try:
        value["process"] = baseline.proc_info(proc.pid)
    except (FileNotFoundError, ProcessLookupError):
        value["process"] = None
    value["threads"] = network.thread_snapshot(proc.pid)
    readings.append(value)
    reasons = violations(initial, value)
    if value["process"] and value["process"]["swap_bytes"]:
        reasons.append("owned process swap")
    if reasons:
        raise RuntimeError(str(reasons))


def server(sample, profile):
    ip = "192.168.100.2"
    baseline.check_root(ROOT)
    argv = [str(baseline.PROJECT / "build/cpnetflux-gridftp-server"), "--root", str(ROOT),
            "--host", ip, "--port", PORT, "--data-port-base", BASE, "--connections", "4",
            "--chunk-size", str(MIB), "--buffer-size", "65536", "--checksum", "crc32c",
            "--manifest-flush-policy", "every_n_chunks", "--manifest-flush-interval-chunks", "16",
            "--final-verify-policy", "verified_chunks", "--commit-sync-policy", "none",
            "--file-io-backend", "posix", "--access-policy", "read-only", "--auth-mode", "anonymous",
            "--tls-mode", "off", "--data-tls-mode", "off", "--data-transport", "tcp",
            "--event-log", str(OUT / (sample + ".server-events.jsonl"))]
    env = environment("pwrite", profile)
    initial, readings = snapshot(), []
    ready = False
    usage_before = None
    try:
        with owned_process(argv, OUT / (sample + ".server.log"), env) as proc:
            deadline = time.monotonic() + 75
            while proc.poll() is None:
                if (OUT / (sample + ".stop")).exists():
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError("sender window")
                if not ready and ip + ":" + PORT in network.command("ss", "-Hln", "sport", "=", PORT):
                    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
                    print('{"ready": true}', flush=True)
                    ready = True
                observe(proc, readings, initial)
                time.sleep(0.025)
            else:
                raise RuntimeError("sender process exited before stop")
    finally:
        usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
        after = snapshot()
        record(sample + ".server.json", dict(command=argv, before=initial, after=after,
            observations=readings, usage=cpu_usage(usage_before, usage_after) if usage_before else None))
    print(json.dumps({"before": initial, "after": after}), flush=True)


def cpu_usage(before, after):
    return {"user_s": after.ru_utime - before.ru_utime,
            "system_s": after.ru_stime - before.ru_stime, "peak_rss_kib": after.ru_maxrss}


def transfer(sample, arm, profile, target):
    summary = OUT / (sample + ".summary.json")
    argv = [str(baseline.PROJECT / "build/cpnetflux-tree-download-client"), "--host", "192.168.100.2",
            "--port", PORT, "--local-bind-address", "192.168.100.1", "--source-dir", "source",
            "--dest-dir", str(target), "--connections", "4", "--file-parallelism", "4",
            "--control-reuse", "worker", "--data-session-reuse", "tree", "--file-scheduling", "dynamic",
            "--data-pending-window", "2", "--checksum", "crc32c", "--chunk-size", str(MIB),
            "--buffer-size", "65536", "--data-frame-bytes", "262144", "--data-transport", "tcp",
            "--final-verify-policy", "verified_chunks", "--commit-sync-policy", "none",
            "--manifest-flush-policy", "every_n_chunks", "--manifest-flush-interval-chunks", "16",
            "--range-checkpoint-batch-chunks", "16", "--posix-write-strategy", "direct",
            "--file-io-buffer-size", "0", "--compression", "off", "--auth-mode", "anonymous",
            "--tls-mode", "off", "--data-tls-mode", "off", "--reconnect-attempts", "0",
            "--phase-timing", "on" if profile else "off", "--json-summary", str(summary)]
    initial, readings = snapshot(), []
    observed_error = None
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    with owned_process(argv, OUT / (sample + ".client.log"), environment(arm, profile)) as proc:
        try:
            while True:
                try:
                    proc.wait(timeout=.025)
                    elapsed = time.monotonic() - started
                    break
                except subprocess.TimeoutExpired:
                    observe(proc, readings, initial)
                    if time.monotonic() - started > 45:
                        raise TimeoutError("receiver window")
        except Exception as error:
            elapsed = time.monotonic() - started
            observed_error = repr(error)
    usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
    immediate = snapshot()
    time.sleep(.35)
    settled = snapshot()
    return dict(command=argv, elapsed_s=elapsed, exit_code=proc.returncode, before=initial,
                immediate=immediate, after=settled, observations=readings, error=observed_error,
                usage=cpu_usage(usage_before, usage_after),
                summary=json.loads(summary.read_text()) if summary.exists() else {})


def verify(result, slot, expected, arm, profile):
    val = result["summary"]
    size = expected["bytes"]
    fields = {"result": "pass", "actual_transport": "v3", "data_transport": "tcp",
              "bytes_transferred": size, "completed_files": 1, "failed_files": 0, "changed_files": 0,
              "data_connect_count": 4, "compression_policy": "off",
              "control_tls_verified": False, "data_tls_verified": False}
    if result["exit_code"] or any(val.get(k) != v for k, v in fields.items()):
        raise RuntimeError("unexpected client terminal: " + str({k: val.get(k) for k in fields}))
    lanes = val["lane_endpoints"]
    if len(lanes) != 4 or not all(
            lane["data_local_address"] == "192.168.100.1" and lane["data_peer_address"] == "192.168.100.2"
            and 1048576 <= lane["tcp_receive_buffer_bytes"] <= 2097152 for lane in lanes):
        raise RuntimeError("unexpected four-lane endpoint or receive buffer")
    actual = sha(slot / "output/f000000.bin")
    if any(actual[key] != expected[key] for key in ("sha256", "bytes")):
        raise RuntimeError("external SHA/size mismatch")
    checks = baseline.check_manifests(slot, 1, size)
    actual["verified_chunks"] = verify_chunks(checks["member_example"], size)
    actual["manifest"] = checks["member_example"]
    timings = val.get("file_timings", [])
    if profile:
        backend = "mmap_experimental" if arm == "mmap" else "configured_file_io"
        if not timings or any(t["receiver_payload_execution"] != "inline" or
                              t["receiver_write_backend"] != backend or
                              not t["receiver_cpu_observation"] for t in timings):
            raise RuntimeError("missing matching receiver phase data")
        if arm == "mmap" and sum(t["mapped_write"]["written_bytes"] for t in timings) != size:
            raise RuntimeError("mmap coverage missing")
    return actual


def sample(label, arm, expected, profile=False):
    slot = baseline.owned(ROOT, label)
    slot.mkdir()
    result = dict(label=label, arm=arm, profile=profile, accepted=False)
    remote_stdout = OUT / (label + ".sender-monitor.log")
    try:
        require_capacity(resource_sample(), expected["bytes"])
        with owned_process(peer("serve", label, int(profile)), remote_stdout, os.environ) as sender:
            try:
                deadline = time.monotonic() + 8
                while '{"ready": true}' not in remote_stdout.read_text(errors="replace"):
                    if sender.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("isolated sender failed to listen")
                    time.sleep(.03)
                result["client"] = transfer(label, arm, profile, slot / "output")
            finally:
                ask("stop", label)
                sender.wait(timeout=8)
        result["server"] = ask("server-receipt", label)
        result["server_events"] = ask("events", label)
        sender_log = ask("sender-log", label)
        (OUT / (label + ".sender.log")).write_text(sender_log)
        sender_rows = [json.loads(line.split(" ", 1)[1]) for line in sender_log.splitlines()
                       if line.startswith("CPNETFLUX_PERSISTENT_TIMING ")]
        result["server_channels"] = [row["channel_index"] for row in sender_rows]
        result["server_terminal_checked_by_client"] = True
        if sorted(result["server_channels"]) != list(range(4)) or "CPNETFLUX_V3_BATCH_TERMINAL " in sender_log:
            raise RuntimeError("incomplete sender channel timings or server batch error")
        client, tx = result["client"], result["server"]
        problems = [client["error"]] if client["error"] else []
        for role, record_value in (("client", client), ("server", tx)):
            errors, delta = comparable(record_value["before"], record_value["after"], traffic=True)
            net_layer = record_value["after"]["rx_queue_packets"]
            record_value["counter_delta"] = delta
            record_value["rx_queue_packet_delta"] = changes(record_value["before"]["rx_queue_packets"], net_layer)
            problems.extend(role + " " + error for error in errors)
            relevant = delta["rx_bytes" if role == "client" else "tx_bytes"]
            if relevant < expected["bytes"]:
                problems.append(role + " interface bytes below payload")
        result["problems"] = problems
        result["integrity"] = verify(client, slot, expected, arm, profile)
        if sender.returncode:
            problems.append("sender monitor reported failure")
        if problems:
            raise RuntimeError(str(problems))
        result["accepted"] = True
    except Exception as error:
        result["error"] = repr(error)
        raise
    finally:
        baseline.remove_owned(ROOT, slot)
        record(label + ".receipt.json", result)
        print(json.dumps({key: result[key] for key in ("label", "accepted")}), flush=True)
    return result


def init():
    OUT.mkdir(mode=0o700, exist_ok=True)
    ROOT.mkdir(mode=0o700)
    (ROOT / baseline.MARKER).write_text(baseline.TOKEN)


def clean():
    baseline.check_root(ROOT)
    for path in ROOT.iterdir():
        if path.name != baseline.MARKER:
            baseline.remove_owned(ROOT, path)
    (ROOT / baseline.MARKER).unlink()
    ROOT.rmdir()
    return not ROOT.exists()


def build_receipt(commit):
    result = baseline.receipt(commit)
    cache = (baseline.PROJECT / "build/CMakeCache.txt").read_text().splitlines()
    result["build_version_and_generator"] = [line for line in cache
        if line.startswith(("CMAKE_PROJECT_VERSION:STATIC=", "CMAKE_GENERATOR:INTERNAL="))]
    return result


def run(commit):
    builds = {"r1": build_receipt(commit), "r2": ask("receipt", commit)}
    if builds["r1"] != builds["r2"]:
        raise RuntimeError("two-node build receipt mismatch")
    record("matching-builds.json", builds)
    preflight = {"r1": identity(), "r2": ask("identity")}
    before = {"r1": snapshot(), "r2": ask("snapshot")}
    time.sleep(2)
    after = {"r1": snapshot(), "r2": ask("snapshot")}
    record("preflight.json", dict(identity=preflight, before=before, after=after))
    for role in before:
        errors, _ = comparable(before[role], after[role])
        if errors:
            raise RuntimeError(role + " idle " + str(errors))
        require_capacity(after[role], 5 * GIB)
    init()
    peer_initialized = False
    try:
        ask("init")
        peer_initialized = True
        expected = ask("seed", 512 * MIB)
        record("source-512m.json", expected)
        if expected["violations"]:
            raise RuntimeError(str(expected["violations"]))
        order = ("pwrite", "mmap", "mmap", "pwrite")
        rows = [sample(f"ab-{index}-{arm}", arm, expected)
                for index, arm in enumerate(order, start=1)]
        for arm in ("pwrite", "mmap"):
            sample("profile-" + arm, arm, expected, profile=True)
        medians = {arm: statistics.median(row["client"]["elapsed_s"]
                   for row in rows if row["arm"] == arm) for arm in ("pwrite", "mmap")}
        choice = min(medians, key=medians.get)
        record("pilot-choice.json", dict(median_seconds=medians, chosen=choice))
        ask("drop-source")
        expected = ask("seed", 5 * GIB)
        record("source-5g.json", expected)
        if expected["violations"]:
            raise RuntimeError(str(expected["violations"]))
        sample("pilot-5g-" + choice, choice, expected)
    finally:
        cleared = {"r1": clean(), "r2": ask("clean") if peer_initialized else None}
        after_identity = {"r1": identity(), "r2": ask("identity")}
        tail = {"r1": snapshot(), "r2": ask("snapshot")}
        invariants = {node: preflight[node] == after_identity[node] for node in preflight}
        record("cleanup.json", dict(cleared=cleared, identity=after_identity, settings_equal=invariants,
                                   snapshot=tail))
        if not all(invariants.values()):
            raise RuntimeError("preflight/postflight invariant mismatch")


def main():
    action, *args = sys.argv[1:]
    if action == "run":
        if baseline.node() != 1:
            raise RuntimeError("run starts on R770-1")
        run(*args)
        return
    if baseline.node() != 2:
        raise RuntimeError("peer actions run on R770-2")
    if action == "seed":
        seed(int(args[0]))
        return
    sample_name = args[0] if args and action in ("serve", "stop", "server-receipt", "events", "sender-log") else None
    if sample_name and not re.fullmatch(r"(ab-[1-4]|profile|pilot-5g)-(pwrite|mmap)", sample_name):
        raise RuntimeError("invalid sample name")
    if action == "serve":
        server(sample_name, args[1] == "1")
        return
    if action == "receipt":
        value = build_receipt(*args)
    elif action == "snapshot":
        value = snapshot()
    elif action == "identity":
        value = identity()
    elif action == "init":
        init()
        value = True
    elif action == "stop":
        (OUT / (sample_name + ".stop")).touch(exist_ok=False)
        value = True
    elif action == "server-receipt":
        value = json.loads((OUT / (sample_name + ".server.json")).read_text())
    elif action == "sender-log":
        value = (OUT / (sample_name + ".server.log")).read_text(errors="replace")
    elif action == "events":
        value = [json.loads(line) for line in (OUT / (sample_name + ".server-events.jsonl")).read_text().splitlines()]
    elif action == "drop-source":
        baseline.remove_owned(ROOT, baseline.owned(ROOT, "source"))
        value = True
    elif action == "clean":
        value = clean()
    else:
        raise RuntimeError(action)
    print(json.dumps(value), flush=True)


if __name__ == "__main__":
    os.umask(0o077)
    main()
