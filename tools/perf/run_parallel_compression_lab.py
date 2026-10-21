#!/usr/bin/env python3
"""Isolated, commit-pinned TCP compression comparison on an SSH lab pair."""
import argparse
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import sys
import secrets
import shlex
import shutil
import socket
import ssl
import subprocess
import time

BASE = Path("/tmp/cpnetflux-runs/V3-PARALLEL-COMPRESSION-01")
MIB = 1024 * 1024
BINARIES = ("cpnetflux-gridftp-server", "cpnetflux-tree-upload-client",
            "cpnetflux-tree-download-client")
SERVER_SUPERVISOR = """
import pathlib, signal, subprocess, sys
def stop(signum, frame):
    raise SystemExit(128 + signum)
for sig in (signal.SIGHUP, signal.SIGTERM, signal.SIGALRM):
    signal.signal(sig, stop)
signal.alarm(1800)
p = subprocess.Popen(sys.argv[2:], stdin=subprocess.DEVNULL)
pathlib.Path(sys.argv[1]).write_text(str(p.pid))
try:
    sys.stdin.buffer.read()
finally:
    if p.poll() is None:
        p.terminate()
    try:
        p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        p.kill()
        p.wait()
"""


def checked(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, timeout=600,
                          **kwargs)


def owned(path):
    path = Path(path).resolve()
    if BASE not in path.parents or path == BASE:
        raise ValueError("path is outside this experiment's unique workspace")
    return path


def tree_hash(root):
    digest, count, total = hashlib.sha256(), 0, 0
    for path in sorted(p for p in root.rglob("*") if p.is_file()):
        relative = path.relative_to(root).as_posix()
        if ".cpnetflux." in relative or ".part." in relative:
            continue
        file_hash = hashlib.sha256()
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(MIB), b""):
                file_hash.update(block)
        size = path.stat().st_size
        digest.update(relative.encode() + b"\0" + str(size).encode() + b"\0" +
                      file_hash.hexdigest().encode() + b"\0")
        count += 1
        total += size
    return [digest.hexdigest(), count, total]


def helper(spec):
    root = owned(spec["root"])
    operation = spec["operation"]
    if operation == "mkdir":
        root.mkdir(mode=0o700, parents=True, exist_ok=False)
        return {"created": str(root)}
    if operation == "remove":
        if root.exists():
            shutil.rmtree(root)
        return {"removed": not root.exists()}
    if operation == "hash":
        return tree_hash(root)
    if operation == "binary_hash":
        return hashlib.sha256(root.read_bytes()).hexdigest()
    if operation != "snapshot":
        raise ValueError("unknown helper operation")
    route = json.loads(checked(["ip", "-j", "route", "get", spec["ip"]],
                               capture_output=True, text=True).stdout)[0]
    if route.get("dev") != spec["interface"]:
        raise RuntimeError("selected data IP does not route through the required interface")
    interface = Path("/sys/class/net") / spec["interface"]
    counters = {name: int((interface / "statistics" / name).read_text())
                for name in ("rx_bytes", "tx_bytes", "rx_errors", "tx_errors",
                             "rx_dropped", "tx_dropped")}
    pressure = {name: (Path("/proc/pressure") / name).read_text()
                for name in ("cpu", "io", "memory")
                if (Path("/proc/pressure") / name).exists()}
    result = {"route": route, "interface": spec["interface"], "counters": counters,
              "pressure": pressure, "load": os.getloadavg(),
              "free_bytes": shutil.disk_usage(root).free}
    pid_file = root / "server.pid"
    if pid_file.exists():
        pid = int(pid_file.read_text())
        fields = Path(f"/proc/{pid}/stat").read_text().split(")", 1)[1].split()
        result["server_cpu_seconds"] = (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")
        result["server_rss_bytes"] = int(fields[21]) * os.sysconf("SC_PAGE_SIZE")
    try:
        result["link_speed_mbps"] = int((interface / "speed").read_text())
    except (OSError, ValueError):
        result["link_speed_mbps"] = None
    return result


def make_file(path, length, random_fraction):
    path.parent.mkdir(parents=True, exist_ok=True)
    pattern = (b"CPNetFlux bounded parallel compression dataset\n" * 2048)[:65536]
    with path.open("wb") as handle:
        index = 0
        while length:
            size = min(length, 65536)
            random_size = int(size * random_fraction)
            block = hashlib.shake_256(str(index).encode()).digest(random_size)
            handle.write(block + pattern[:size - random_size])
            length -= size
            index += 1


def make_datasets(root, large_mib):
    for name, fraction in (("large-repeated", 0), ("large-random", 1)):
        make_file(root / name / "large.bin", large_mib * MIB, fraction)
    for index in range(2048):
        make_file(root / "dense" / str(index % 32) / f"{index}.bin", 65536, 0.5)
    make_file(root / "mixed" / "large.bin", large_mib * MIB // 2, 0.5)
    for index in range(512):
        make_file(root / "mixed" / str(index % 16) / f"{index}.bin", 8192, index % 2)
    (root / "mixed" / "empty").touch()
    return sorted(root.iterdir())


def validate_compression_observation(mode, observed):
    if mode == "off":
        if observed["compression_attempts"] != 0:
            raise RuntimeError("raw control unexpectedly attempted compression")
        return
    if not observed.get("compression_negotiated"):
        raise RuntimeError("requested compression was not negotiated")
    workers = observed["compression_workers_observed"]
    if not (0 <= workers <= 4 and
            observed["compression_max_in_flight_jobs_observed"] <= 16 and
            observed["compression_max_in_flight_bytes_observed"] <= 268435456):
        raise RuntimeError("compression concurrency evidence violates bounds")
    if observed["compression_attempts"] and workers == 0:
        raise RuntimeError("local compression attempts have no worker evidence")
    # Auto can reject an incompressible sample before starting any pool job.
    if not observed["compression_attempts"] and observed["compressed_frames"]:
        raise RuntimeError("compressed frames have no local compression attempts")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", help=argparse.SUPPRESS)
    parser.add_argument("--repository", type=Path)
    parser.add_argument("--commit")
    parser.add_argument("--build-artifacts", type=Path,
                        help="reuse this runner's manifest-verified artifacts from the same commit/archive")
    parser.add_argument("--peer-ssh")
    parser.add_argument("--peer-ip")
    parser.add_argument("--local-interface")
    parser.add_argument("--peer-interface")
    parser.add_argument("--large-mib", type=int, default=512)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--channels", type=int, default=8)
    parser.add_argument("--scheduler-policy", choices=("fixed", "adaptive"), default="fixed")
    parser.add_argument("--port", type=int, default=34261)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.helper:
        print(json.dumps(helper(json.loads(args.helper))))
        return
    if args.self_test:
        workspace = BASE / ("script-check-" + secrets.token_hex(6))
        workspace.mkdir(parents=True, mode=0o700)
        try:
            datasets = make_datasets(workspace / "payload", 1)
            assert len(datasets) == 4
            for dataset in datasets:
                observed = tree_hash(dataset)
                assert observed == tree_hash(dataset) and observed[1] > 0
                assert observed[2] <= 10 * 1024 ** 3
            try:
                owned(BASE.parent)
                raise AssertionError("cleanup accepted a parent directory")
            except ValueError:
                pass
            idle = {"compression_negotiated": True, "compression_attempts": 0,
                    "compressed_frames": 0, "compression_workers_observed": 0,
                    "compression_max_in_flight_jobs_observed": 0,
                    "compression_max_in_flight_bytes_observed": 0}
            validate_compression_observation("off", idle)
            validate_compression_observation("auto", idle)
            active = dict(idle, compression_attempts=1, compressed_frames=1,
                          compression_workers_observed=1,
                          compression_max_in_flight_jobs_observed=1,
                          compression_max_in_flight_bytes_observed=MIB)
            validate_compression_observation("auto", active)
            for invalid in (dict(active, compression_workers_observed=5),
                            dict(active, compression_workers_observed=0),
                            dict(idle, compression_negotiated=False),
                            dict(idle, compressed_frames=1)):
                try:
                    validate_compression_observation("auto", invalid)
                    raise AssertionError("invalid compression observation was accepted")
                except RuntimeError:
                    pass
            pid_file = workspace / "server.pid"
            supervisor = subprocess.Popen([sys.executable, "-c", SERVER_SUPERVISOR,
                str(pid_file), sys.executable, "-c", "import time; time.sleep(30)"],
                stdin=subprocess.PIPE)
            try:
                deadline = time.monotonic() + 5
                while not pid_file.exists() and time.monotonic() < deadline:
                    time.sleep(0.01)
                assert pid_file.exists()
            finally:
                supervisor.stdin.close()
                supervisor.wait(timeout=15)
            assert supervisor.returncode == 0
            assert not Path("/proc/" + pid_file.read_text()).exists()
            print(json.dumps({"result": "pass", "scope": "datasets/hash/path bounds/server EOF cleanup"}))
        finally:
            helper({"operation": "remove", "root": str(workspace)})
        return
    required = (args.repository, args.commit, args.peer_ssh, args.peer_ip,
                args.local_interface, args.peer_interface)
    if not all(required) or not re.fullmatch(r"[0-9a-f]{40}", args.commit or ""):
        parser.error("repository, full commit SHA, peer SSH/IP and both interfaces are required")
    if not re.fullmatch(r"[A-Za-z0-9_.@-]+", args.peer_ssh) or args.peer_ssh.startswith("-"):
        parser.error("invalid SSH alias")
    ipaddress.ip_address(args.peer_ip)
    if not 1 <= args.large_mib <= 10240 or not 3 <= args.repetitions <= 5 or not 1 <= args.channels <= 10:
        parser.error("large-mib must be 1..10240, repetitions 3..5, channels 1..10")
    if not 1024 <= args.port <= 64000:
        parser.error("port must be 1024..64000")
    workspace = BASE / (time.strftime("%Y%m%dT%H%M%S-") + secrets.token_hex(6))
    workspace.mkdir(mode=0o700, parents=True)
    script_relative = "src/tools/perf/run_parallel_compression_lab.py"
    ssh = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5", args.peer_ssh]
    peer_created, server = False, None

    def remote(command, **kwargs):
        return checked([*ssh, shlex.join([str(item) for item in command])], **kwargs)

    def peer_helper(operation, root=workspace, **kwargs):
        request = json.dumps({"operation": operation, "root": str(root), **kwargs})
        return json.loads(remote(["python3", workspace / script_relative, "--helper", request],
                                 capture_output=True, text=True).stdout)

    def copy(path, destination):
        checked(["scp", "-q", "-o", "BatchMode=yes", "-o", "ConnectTimeout=5",
                 path, args.peer_ssh + ":" + str(destination)])

    try:
        archive = workspace / "source.tar"
        with archive.open("wb") as handle:
            checked(["git", "-C", args.repository, "archive", args.commit], stdout=handle)
        (workspace / "src").mkdir()
        checked(["tar", "-xf", archive, "-C", workspace / "src"])
        # Use a unique remote directory; never replace a packaged service or checkout.
        remote(["python3", "-c", "import pathlib,sys; pathlib.Path(sys.argv[1]).mkdir(parents=True,mode=448,exist_ok=False)", workspace])
        peer_created = True
        copy(archive, workspace / "source.tar")
        remote(["mkdir", workspace / "src"])
        remote(["tar", "-xf", workspace / "source.tar", "-C", workspace / "src"])
        before = helper({"operation": "snapshot", "root": str(workspace),
                         "ip": args.peer_ip, "interface": args.local_interface})
        local_ip = before["route"].get("prefsrc", before["route"].get("src"))
        if not local_ip:
            raise RuntimeError("cannot determine selected local data IP")
        peer_before = peer_helper("snapshot", ip=local_ip, interface=args.peer_interface)
        required_free = max(8 * 1024 ** 3, args.large_mib * MIB * 4 + 2 * 1024 ** 3)
        if min(before["free_bytes"], peer_before["free_bytes"]) < required_free:
            raise RuntimeError("insufficient free space for bounded datasets/build/receiver")
        archive_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
        build = args.build_artifacts.resolve() if args.build_artifacts else workspace / "build"
        artifact_manifest = None
        if args.build_artifacts:
            artifact_manifest = json.loads((build / "artifact-manifest.json").read_text())
            if (artifact_manifest.get("commit") != args.commit or
                    artifact_manifest.get("archive_sha256") != archive_hash):
                raise RuntimeError("artifact provenance does not match the pinned source archive")
        else:
            with (workspace / "build.log").open("w") as log:
                checked(["cmake", "-S", workspace / "src", "-B", build,
                         "-DCMAKE_BUILD_TYPE=Release", "-DCPNETFLUX_BUILD_TESTS=OFF",
                         "-DCPNETFLUX_BUILD_DESKTOP=OFF", "-DCPNETFLUX_ENABLE_TLS=ON"],
                        stdout=log, stderr=subprocess.STDOUT)
                checked(["cmake", "--build", build, "-j2", "--target", *BINARIES],
                        stdout=log, stderr=subprocess.STDOUT)
        remote(["mkdir", workspace / "bin"])
        binary_hashes = {}
        for name in BINARIES:
            source = build / name
            local_hash = hashlib.sha256(source.read_bytes()).hexdigest()
            if (artifact_manifest is not None and
                    artifact_manifest.get("binary_sha256", {}).get(name) != local_hash):
                raise RuntimeError("reused build binary hash mismatch: " + name)
            copy(source, workspace / "bin" / name)
            if peer_helper("binary_hash", workspace / "bin" / name) != local_hash:
                raise RuntimeError("deployed binary hash mismatch")
            dependencies = remote(["ldd", workspace / "bin" / name], capture_output=True, text=True)
            if "not found" in dependencies.stdout + dependencies.stderr:
                raise RuntimeError("receiver runtime dependency is missing")
            binary_hashes[name] = local_hash
        if artifact_manifest is None:
            (build / "artifact-manifest.json").write_text(json.dumps({
                "commit": args.commit, "archive_sha256": archive_hash,
                "binary_sha256": binary_hashes,
                "build": "Release; TLS on; tests/desktop/io_uring off"}, indent=2))
        credentials = workspace / "credentials"
        credentials.mkdir(mode=0o700)
        token, cert, key = (credentials / name for name in ("token", "cert.pem", "key.pem"))
        token.write_text(secrets.token_hex(32) + "\n")
        token.chmod(0o600)
        checked(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                 "-keyout", key, "-out", cert, "-days", "1", "-subj", "/CN=cpnetflux-lab",
                 "-addext", "subjectAltName=IP:" + args.peer_ip],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        key.chmod(0o600)
        remote(["mkdir", "-m", "700", workspace / "credentials"])
        for path in (token, cert, key):
            copy(path, workspace / "credentials" / path.name)
        remote(["mkdir", workspace / "receive"])
        server_command = [workspace / "bin/cpnetflux-gridftp-server", "--host", args.peer_ip,
                          "--port", args.port, "--data-port-base", 35000,
                          "--root", workspace / "receive", "--checksum", "crc32c",
                          "--auth-mode", "token", "--auth-token-file", token,
                          "--tls-mode", "required", "--tls-cert-file", cert,
                          "--tls-key-file", key, "--data-tls-mode", "required"]
        supervisor = SERVER_SUPERVISOR
        with (workspace / "server.log").open("w") as log:
            server = subprocess.Popen([*ssh, shlex.join(["python3", "-c", supervisor,
                                       str(workspace / "server.pid"), *map(str, server_command)])],
                                      stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT)
        context = ssl.create_default_context(cafile=str(cert))
        deadline = time.monotonic() + 15
        while True:
            try:
                with socket.create_connection((args.peer_ip, args.port), timeout=2) as connection:
                    with context.wrap_socket(connection, server_hostname=args.peer_ip) as secured:
                        if not secured.recv(512).startswith(b"220"):
                            raise RuntimeError("unexpected greeting")
                break
            except OSError:
                if server.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("isolated TLS server did not start; inspect server.log")
                time.sleep(0.1)
        metadata = {"commit": args.commit, "binary_sha256": binary_hashes,
                    "archive_sha256": archive_hash,
                    "reused_artifacts": bool(args.build_artifacts),
                    "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    "build": "Release; TLS on; tests/desktop/io_uring off",
                    "transport": "TCP over selected IP interface; not RDMA verbs",
                    "scheduler": {"mode": "global", "policy": args.scheduler_policy,
                                  "capacity_gbps": 100, "default_rtt_ms": 1,
                                  "workitem_min_bytes": 512 * MIB,
                                  "workitem_max_bytes": 512 * MIB},
                    "local_before": before, "peer_before": peer_before,
                    "note": "warm cached files; network counters include SSH probe traffic"}
        (workspace / "environment.json").write_text(json.dumps(metadata, indent=2))
        datasets = make_datasets(workspace / "payload", args.large_mib)
        with (workspace / "results.jsonl").open("w") as evidence:
            for dataset in datasets:
                expected = tree_hash(dataset)
                if expected[2] > 10 * 1024 ** 3:
                    raise RuntimeError("dataset exceeds 10 GiB")
                for repetition in range(args.repetitions + 1):
                    modes = ("off", "auto") if repetition % 2 == 0 else ("auto", "off")
                    for mode in modes:
                        name = f"{dataset.name}-{repetition}-{mode}"
                        summary = workspace / (name + ".json")
                        local_before = helper({"operation": "snapshot", "root": str(workspace),
                                               "ip": args.peer_ip, "interface": args.local_interface})
                        remote_before = peer_helper("snapshot", ip=local_ip, interface=args.peer_interface)
                        cpu_file = workspace / (name + "-cpu.json")
                        time_binary = shutil.which("time")
                        if not time_binary:
                            raise RuntimeError("GNU time is required for per-client CPU/RSS evidence")
                        started = time.monotonic()
                        with (workspace / (name + ".log")).open("w") as log:
                            checked([time_binary, "-f",
                                     '{"user_seconds":%U,"system_seconds":%S,"max_rss_kib":%M}',
                                     "-o", cpu_file,
                                     build / "cpnetflux-tree-upload-client", "--host", args.peer_ip,
                                     "--port", args.port, "--source-dir", dataset, "--dest-dir", name,
                                     "--auth-mode", "token", "--auth-token-file", token,
                                     "--tls-mode", "required", "--tls-ca-file", cert,
                                     "--data-tls-mode", "required", "--checksum", "crc32c",
                                     "--control-reuse", "worker", "--data-session-reuse", "tree",
                                     "--file-scheduling", "dynamic", "--file-parallelism", args.channels,
                                     "--connections", "1", "--data-pending-window", "4",
                                     "--scheduler", "global",
                                     "--scheduler-policy", args.scheduler_policy,
                                     "--scheduler-capacity-gbps", "100",
                                     "--scheduler-default-rtt-ms", "1",
                                     "--scheduler-workitem-min-bytes", 512 * MIB,
                                     "--scheduler-workitem-max-bytes", 512 * MIB,
                                     "--scheduler-metrics-dir", workspace / "metrics" / name,
                                     "--chunk-size", 4 * MIB, "--compression", mode,
                                     "--transfer-timeout-ms", "180000", "--json-summary", summary],
                                    stdout=log, stderr=subprocess.STDOUT)
                        elapsed = time.monotonic() - started
                        client_cpu = json.loads(cpu_file.read_text())
                        remote_after = peer_helper("snapshot", ip=local_ip, interface=args.peer_interface)
                        local_after = helper({"operation": "snapshot", "root": str(workspace),
                                              "ip": args.peer_ip, "interface": args.local_interface})
                        observed = json.loads(summary.read_text())
                        actual = peer_helper("hash", workspace / "receive" / name)
                        if actual != expected or observed.get("result") != "pass":
                            raise RuntimeError("transfer or SHA-256 verification failed")
                        if observed.get("actual_mode") != "persistent_tree":
                            raise RuntimeError("V3 persistent tree was not used")
                        if (dataset.name != "dense" and expected[2] > 128 * MIB and
                                args.channels > 1 and
                                not sum(observed["persistent_lane_range_completed"])):
                            raise RuntimeError("large-file range work was not observed")
                        if (observed.get("actual_file_scheduling") != "dynamic" or
                                observed.get("scheduler_mode") != "global"):
                            raise RuntimeError("requested directory scheduling combination was not used")
                        validate_compression_observation(mode, observed)
                        row = {"dataset": dataset.name, "repetition": repetition,
                               "warmup": repetition == 0, "compression": mode, "sha256": actual,
                               "wall_seconds": elapsed, "logical_gbps": expected[2] * 8 / elapsed / 1e9,
                               "client_cpu": client_cpu,
                               "local_before": local_before, "local_after": local_after,
                               "peer_before": remote_before, "peer_after": remote_after,
                               "summary": observed}
                        row["cleanup"] = peer_helper("remove", workspace / "receive" / name)
                        evidence.write(json.dumps(row) + "\n")
                        evidence.flush()
                        print(json.dumps({"dataset": dataset.name, "mode": mode,
                                          "repetition": repetition, "seconds": elapsed,
                                          "wire_bytes": observed["wire_bytes"], "sha256": "pass"}), flush=True)
        print("Evidence: " + str(workspace))
    finally:
        if server:
            server.stdin.close()
            try:
                server.wait(timeout=15)
            except subprocess.TimeoutExpired:
                server.terminate()
                server.wait(timeout=10)
        cleanup = {}
        if peer_created:
            for name in ("receive", "credentials"):
                try:
                    cleanup["peer_" + name] = peer_helper("remove", workspace / name)
                except (OSError, subprocess.SubprocessError):
                    cleanup["peer_" + name] = {"removed": False}
        for name in ("payload", "credentials"):
            cleanup[name] = helper({"operation": "remove", "root": str(workspace / name)})
        (workspace / "cleanup.json").write_text(json.dumps(cleanup, indent=2))


if __name__ == "__main__":
    main()
