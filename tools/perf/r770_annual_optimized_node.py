#!/usr/bin/env python3
"""Node actions for fixed 1 GiB and 5 GiB annual comparisons."""
import json
import os
from pathlib import Path
import random
import re
import resource
import shutil
import signal
import stat
import subprocess
import sys
import time

import run_r770_receive_cost as rx
import r770_tcp_diag as diag

SECURITY_RUN = "2026-11-24-r770-annual-5g-security-02"
REVERSE_RUN = "2026-11-24-r770-annual-5g-reverse-02"
ANNUAL_5G_REJECTED_SECURITY_RUN = "2026-11-27-r770-annual-5g-security-03"
ANNUAL_5G_REJECTED_SECURITY_RETRY_RUN = "2026-11-27-r770-annual-5g-security-04"
ANNUAL_5G_REJECTED_SECURITY_FIXED_RUN = "2026-11-27-r770-annual-5g-security-05"
ANNUAL_5G_REJECTED_SECURITY_D5_RUN = "2026-11-27-r770-annual-5g-security-06"
ANNUAL_5G_REJECTED_SECURITY_LATEST_RUN = "2026-11-27-r770-annual-5g-security-07"
ANNUAL_5G_REJECTED_SECURITY_PREOPT_RUN = "2026-11-27-r770-annual-5g-security-08"
ANNUAL_5G_REJECTED_SECURITY_RECEIPT_RUN = "2026-11-27-r770-annual-5g-security-09"
ANNUAL_5G_REJECTED_SECURITY_RESOURCE_RUN = "2026-11-27-r770-annual-5g-security-10"
ANNUAL_5G_REJECTED_SECURITY_SCOPE_RUN = "2026-11-27-r770-annual-5g-security-11"
ANNUAL_5G_SECURITY_RUN = "2026-11-27-r770-annual-5g-security-12"
SECURITY_FULL_PSI_TOLERANCE_US = 5000
ANNUAL_5G_REJECTED_FORWARD_RUN = "2026-11-27-r770-annual-5g-main-forward-03"
ANNUAL_5G_REJECTED_FORWARD_FIXED_RUN = "2026-11-27-r770-annual-5g-main-forward-04"
ANNUAL_5G_REJECTED_FORWARD_D5_RUN = "2026-11-27-r770-annual-5g-main-forward-05"
ANNUAL_5G_FORWARD_RUN = "2026-11-27-r770-annual-5g-main-forward-06"
ANNUAL_5G_REJECTED_REVERSE_RUN = "2026-11-27-r770-annual-5g-main-reverse-03"
ANNUAL_5G_REJECTED_REVERSE_PARTIAL_RUN = "2026-11-27-r770-annual-5g-main-reverse-04"
ANNUAL_5G_REJECTED_REVERSE_SOURCE_RUN = "2026-11-27-r770-annual-5g-main-reverse-05"
ANNUAL_5G_REVERSE_RUN = "2026-11-27-r770-annual-5g-main-reverse-06"
ANNUAL_5G_RUNS = (ANNUAL_5G_SECURITY_RUN, ANNUAL_5G_FORWARD_RUN, ANNUAL_5G_REVERSE_RUN)
COMFORT_DIAG_RUN = "2026-11-27-r770-tcp-diag-validation-02"
COMFORT_SECURITY_RUN = "2026-11-27-r770-annual-comfort-security-1g-16"
COMFORT_FORWARD_RUN = "2026-11-27-r770-annual-comfort-main-forward-1g-16"
COMFORT_REVERSE_RUN = "2026-11-27-r770-annual-comfort-main-reverse-1g-16"
COMFORT_RUNS = (COMFORT_SECURITY_RUN, COMFORT_FORWARD_RUN, COMFORT_REVERSE_RUN)
RUN_SIZES = {
    "2026-11-24-r770-annual-optimized-5g-01": 5 * 2**30,
    SECURITY_RUN: 5 * 2**30,
    REVERSE_RUN: 5 * 2**30,
    ANNUAL_5G_SECURITY_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_RETRY_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_FIXED_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_PREOPT_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_RECEIPT_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_RESOURCE_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_SCOPE_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_D5_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_SECURITY_LATEST_RUN: 5 * 2**30,
    ANNUAL_5G_FORWARD_RUN: 5 * 2**30,
    ANNUAL_5G_REVERSE_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_REVERSE_SOURCE_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_FORWARD_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_FORWARD_FIXED_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_FORWARD_D5_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_REVERSE_RUN: 5 * 2**30,
    ANNUAL_5G_REJECTED_REVERSE_PARTIAL_RUN: 5 * 2**30,
    COMFORT_DIAG_RUN: 128 * 2**20,
    COMFORT_SECURITY_RUN: 2**30,
    COMFORT_FORWARD_RUN: 2**30,
    COMFORT_REVERSE_RUN: 2**30,
}
RUN = os.environ.get("CPNETFLUX_ANNUAL_RUN", "2026-11-24-r770-annual-optimized-5g-01")
if RUN not in RUN_SIZES:
    raise ValueError("unknown annual run directory")
OUT = Path("/home/Su/experiments/CPNetFlux") / RUN
ROOT = Path("/dev/shm") / ("cpnetflux-s0-" + RUN)
CRED = OUT / "credentials"
BUILD = rx.baseline.PROJECT / "build"
SIZE = RUN_SIZES[RUN]
SMOKE = 128 * 2**20
MODES = ("mmap", "grid", "private", "anon", "clear", "tls")
CP_MODES = ("mmap", "clear", "tls")
IS_COMFORT = RUN in COMFORT_RUNS or RUN == COMFORT_DIAG_RUN
IS_SECURITY_5G = RUN == ANNUAL_5G_SECURITY_RUN
IS_DIAG_VALIDATION = RUN == COMFORT_DIAG_RUN
HOST = rx.baseline.node()
IP = f"192.168.100.{HOST}"


def cmd(argv, env=None, timeout=15):
    return subprocess.run(argv, env=env, text=True, capture_output=True,
                          check=True, timeout=timeout).stdout.strip()


def slot(label):
    return rx.baseline.owned(ROOT, label)


def prepare():
    OUT.mkdir(mode=0o700, exist_ok=True)
    if OUT.is_symlink() or OUT.stat().st_uid != os.getuid():
        raise RuntimeError("invalid evidence directory")
    ROOT.mkdir(mode=0o700)
    (ROOT / rx.baseline.MARKER).write_text(rx.baseline.TOKEN)


def seed(size):
    if size not in (SIZE, SMOKE):
        raise ValueError("unexpected payload size")
    start = rx.resource_sample()
    rx.require_capacity(start, size)
    source = slot("source")
    source.mkdir()
    rng = random.Random(20261124)
    stamp = time.monotonic()
    reasons = []
    with (source / "f000000.bin").open("xb") as stream:
        for block in range(size // 2**20):
            stream.write(rng.randbytes(2**20))
            if block % 64 == 63:
                reasons = rx.violations(
                    start, rx.resource_sample(),
                    full_psi_tolerance_us=(SECURITY_FULL_PSI_TOLERANCE_US if IS_SECURITY_5G else 0))
                if reasons:
                    break
    generation = time.monotonic() - stamp
    result = rx.sha(source / "f000000.bin")
    result.update(generation_seconds=generation, before=start, after=rx.resource_sample())
    result["violations"] = reasons or rx.violations(
        start, result["after"],
        full_psi_tolerance_us=(SECURITY_FULL_PSI_TOLERANCE_US if IS_SECURITY_5G else 0))
    return result


def clean_slot(label):
    path = slot(label)
    if path.exists():
        rx.baseline.remove_owned(ROOT, path)
    if label == "source":
        remove_upload_manifest()
    return not path.exists()


def remove_upload_manifest():
    rx.baseline.check_root(ROOT)
    path = ROOT / "source.cpnetflux.tree.upload.manifest"
    try:
        identity = path.lstat()
    except FileNotFoundError:
        return True
    if not stat.S_ISREG(identity.st_mode) or identity.st_uid != os.getuid() or identity.st_nlink != 1:
        raise RuntimeError("unexpected upload manifest ownership or type")
    path.unlink()
    return True


def request_cert():
    CRED.mkdir(mode=0o700)
    cmd(["openssl", "req", "-new", "-newkey", "rsa:2048", "-nodes",
         "-subj", subject(HOST), "-keyout", str(CRED/"host.key"), "-out", str(CRED/"host.csr")])
    return (CRED/"host.csr").read_text()


def subject(host):
    return f"/O=CPNetFlux Annual Test/CN=r770-{host}.annual-test.cpnetflux.invalid"


def install_cert():
    public = json.load(sys.stdin)
    for name in ("ca.pem", "host.pem", "gridmap"):
        path = CRED/name
        if name == "ca.pem" and path.exists():
            if path.read_text() != public[name]:
                raise ValueError("local CA mismatch")
        else:
            with path.open("x") as stream:
                stream.write(public[name])
    with (CRED/"cp.token").open("x") as stream:
        stream.write(public["token"] + "\n")
    trust = CRED/"trust"
    trust.mkdir()
    ca = CRED/"ca.pem"
    for flag in ("-subject_hash", "-subject_hash_old"):
        digest = cmd(["openssl", "x509", "-in", str(ca), "-noout", flag])
        cert = trust/(digest + ".0")
        if not cert.exists():
            cert.write_bytes(ca.read_bytes())
            (trust/(digest + ".signing_policy")).write_text(public["policy"])
    cert = CRED/"host.pem"
    cmd(["openssl", "verify", "-CAfile", str(ca), str(cert)])
    cmd(["openssl", "x509", "-in", str(cert), "-checkend", "3600", "-noout"])
    return cmd(["openssl", "x509", "-in", str(cert), "-noout", "-subject", "-enddate"])


def cp_download_mode(mode):
    return mode == "mmap" or ((IS_COMFORT or IS_SECURITY_5G) and mode in ("clear", "tls"))


def env(mode, role=None):
    value = rx.environment("mmap" if cp_download_mode(mode) else "pwrite", False)
    if IS_SECURITY_5G and mode in ("clear", "tls"):
        if role == "receiver":
            value.update(CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION="inline",
                         CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE="off",
                         CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES="1048576",
                         CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND="off")
        elif role == "sender":
            value.update(CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES="off",
                         CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND="1500000000")
    elif IS_COMFORT and mode in CP_MODES:
        value.update(CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION="inline",
                     CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE="off",
                     CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES="1048576",
                     CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND="1500000000")
    elif mode != "mmap":
        value.update(CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES="off",
                     CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND="off")
    return value


def comfort_profile(mode):
    return {
        "data_transport": "tcp",
        "connections": 4,
        "receiver_payload_execution": "inline",
        "receiver_write_backend": "mmap_experimental",
        "direct_receive_opt_in": "off",
        "data_frame_bytes": 262144,
        "tcp_receive_buffer_requested_bytes": 1048576,
        "application_pacing_bytes_per_second_per_lane": 1500000000,
        "checksum": "crc32c",
        "chunk_bytes": 1048576,
        "checkpoint_chunks": 16,
        "compression": "off",
        "authentication": "token" if mode in ("clear", "tls") else "anonymous",
        "control_tls": mode == "tls",
        "data_tls": mode == "tls",
    }


def security_5g_profile(mode):
    return {
        "data_transport": "tcp",
        "connections": 4,
        "receiver_payload_execution": "inline",
        "receiver_write_backend": "mmap_experimental",
        "direct_receive_opt_in": "off",
        "data_frame_bytes": 262144,
        "tcp_receive_buffer_requested_bytes": 1048576,
        "application_pacing_bytes_per_second_per_lane": 1500000000,
        "checksum": "crc32c",
        "chunk_bytes": 1048576,
        "checkpoint_chunks": 16,
        "compression": "off",
        "authentication": "token",
        "control_tls": mode == "tls",
        "data_tls": mode == "tls",
    }



def grid_env():
    value = os.environ.copy()
    for key in list(value):
        if key.startswith(("GLOBUS_", "X509_")) or key == "GRIDMAP":
            value.pop(key)
    value.update(X509_CERT_DIR=str(CRED/"trust"))
    return value


def grid_listener(mode):
    return (45591 if mode == "anon" else 45581) + HOST


def service_args(mode, label):
    if mode in ("grid", "anon"):
        prefix = mode + "-grid"
        config = OUT/(prefix + ".conf")
        config.touch(exist_ok=True)
        first = (45800 if mode == "anon" else 45700) + 30 * HOST
        config_env = grid_env()
        if mode == "grid":
            config_env.update(X509_USER_CERT=str(CRED/"host.pem"),
                              X509_USER_KEY=str(CRED/"host.key"), GRIDMAP=str(CRED/"gridmap"))
        argv = ["/usr/sbin/globus-gridftp-server", "-c", str(config), "-p",
                str(grid_listener(mode)), "-control-interface", IP, "-data-interface", IP,
                "-port-range", f"{first},{first+24}", "-allow-from", "192.168.100.",
                "-rp", str(ROOT), "-no-chdir", "-log-level", "ERROR,WARN,TRANSFER",
                "-l", str(OUT/(prefix + ".log")), "-Z", str(OUT/(prefix + "-transfer.log"))]
        argv += ["-aa"] if mode == "anon" else ["-auth-level", "6"]
        return argv, config_env, grid_listener(mode)

    tls = "required" if mode == "tls" else "off"
    download = cp_download_mode(mode)
    directory = ROOT if download else slot(label)
    auth = "anonymous" if mode == "mmap" else "token"
    argv = [str(BUILD/"cpnetflux-gridftp-server"), "--root", str(directory), "--host", IP,
            "--port", "45596", "--data-port-base", "61350", "--connections", "4",
            "--checksum", "crc32c", "--chunk-size", "1048576", "--buffer-size", "65536",
            "--manifest-flush-policy", "every_n_chunks", "--manifest-flush-interval-chunks", "16",
            "--data-transport", "tcp", "--final-verify-policy", "verified_chunks",
            "--commit-sync-policy", "none", "--file-io-backend", "posix",
            "--posix-write-strategy", "direct",
            "--access-policy", "read-only" if download else "write-only",
            "--auth-mode", auth,
            "--tls-mode", tls, "--data-tls-mode", tls,
            "--event-log", str(OUT/(label + ".events.jsonl"))]
    if auth == "token":
        argv += ["--auth-token-file", str(CRED/"cp.token")]
    if tls == "required":
        argv += ["--tls-cert-file", str(CRED/"host.pem"), "--tls-key-file", str(CRED/"host.key")]
    return argv, env(mode, "sender" if download else "receiver"), 45596


def service(mode, label):
    argv, variables, port = service_args(mode, label)
    if cmd(["ss", "-Hln", "sport", "=", str(port)]):
        raise RuntimeError("experiment port already bound")
    log = OUT/(label + ".service.log")
    with log.open("xb") as stream:
        process = subprocess.Popen(["timeout", "--kill-after=4", "30m", *argv], env=variables,
                                   stdin=subprocess.DEVNULL, stdout=stream, stderr=stream,
                                   start_new_session=True)
    ident = rx.baseline.proc_info(process.pid)
    runtime_profile = {key: variables.get(key) for key in (
        "CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION", "CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES",
        "CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND")}
    receipt = OUT/(label + ".service.json")
    with receipt.open("x") as stream:
        json.dump(dict(pid=process.pid, start=ident["start_ticks"], command=argv, port=port,
                       runtime_profile=runtime_profile), stream)
    try:
        for _ in range(60):
            if process.poll() is not None:
                break
            if IP + ":" + str(port) in cmd(["ss", "-Hln", "sport", "=", str(port)]):
                return {"pid": process.pid, "port": port, "runtime_profile": runtime_profile}
            time.sleep(.05)
        raise RuntimeError("listener did not start; see " + str(log))
    except BaseException:
        stop(label)
        raise


def stop(label):
    receipt = OUT/(label + ".service.json")
    if not receipt.exists():
        return True
    row = json.loads(receipt.read_text())
    pid = row["pid"]
    try:
        proc = rx.baseline.proc_info(pid)
        if proc["start_ticks"] == row["start"]:
            os.killpg(pid, signal.SIGTERM)
    except (FileNotFoundError, ProcessLookupError):
        pass
    for _ in range(65):
        if IP + ":" + str(row["port"]) not in cmd(["ss", "-Hln", "sport", "=", str(row["port"])]):
            return True
        time.sleep(.05)
    raise RuntimeError("experiment listener still present: " + label)


def clean():
    for path in sorted(OUT.glob("*.service.json")):
        stop(path.name.removesuffix(".service.json"))
    if ROOT.exists():
        remove_upload_manifest()
        for path in ROOT.iterdir():
            if path.name != rx.baseline.MARKER:
                clean_slot(path.name)
        rx.baseline.check_root(ROOT)
        (ROOT/rx.baseline.MARKER).unlink()
        ROOT.rmdir()
    for marker in OUT.glob("*.watch.stop"):
        identity = marker.lstat()
        if not stat.S_ISREG(identity.st_mode) or identity.st_uid != os.getuid() or identity.st_nlink != 1:
            raise RuntimeError("foreign watcher stop marker")
        marker.unlink()
    if CRED.exists():
        if CRED.parent != OUT or CRED.is_symlink() or CRED.stat().st_uid != os.getuid():
            raise RuntimeError("foreign credential directory")
        shutil.rmtree(CRED)
    ports = {45582,45583,45592,45593,45596,61350,61351,61352,61353,
             *range(45730,45755), *range(45760,45785),
             *range(45830,45855), *range(45860,45885)}
    bound = {int(line.split()[3].rsplit(":", 1)[-1]) for line in cmd(["ss", "-Hltn"]).splitlines()}
    return {"payload_removed": not ROOT.exists(), "credential_removed": not CRED.exists(),
            "listeners_empty": not ports.intersection(bound),
            "tmpfs_free": shutil.disk_usage(ROOT.parent).free}


def endpoint(host, label, mode):
    prefix = "ftp://anonymous@" if mode == "anon" else "gsiftp://"
    base = 45591 if mode == "anon" else 45581
    return f"{prefix}192.168.100.{host}:{base+host}{ROOT}/{label}/f000000.bin"


def run_client(argv, vars):
    stamp = time.monotonic()
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    proc = subprocess.run(argv, env=vars, capture_output=True, timeout=60, text=True,
                          errors="replace")
    elapsed = time.monotonic() - stamp
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    return proc, {"seconds": elapsed, "returncode": proc.returncode, "command": argv,
                  "controller_cpu": rx.cpu_usage(before, after)}


def grid_client(mode, label, sender, receiver):
    argv = ["globus-url-copy", "-dbg", "-fast", "-p", "4", "-tcp-bs", "1048576",
            "-bs", "262144"]
    variables = grid_env()
    lifetime = None
    if mode != "anon":
        proxy = CRED/"proxy.pem"
        lifetime = int(cmd(["grid-proxy-info", "-f", str(proxy), "-timeleft"]))
        if lifetime < 600:
            raise RuntimeError("GSI proxy expires too soon")
        variables["X509_USER_PROXY"] = str(proxy)
        argv += ["-dcpriv" if mode == "private" else "-nodcau",
                 "-ss", subject(sender), "-ds", subject(receiver)]
    argv += [endpoint(sender, "source", mode), endpoint(receiver, label, mode)]
    proc, row = run_client(argv, variables)
    trace = proc.stdout + "\n" + proc.stderr
    patterns = (r"\b230 User [A-Za-z0-9_-]+ logged in\b|"
                r"\b200 (?:Mode set to E|Protection level set to [CP]|DCAU [NAS])\b|"
                r"\b(?:PROT [CP]|226 Transfer Complete)\b")
    row.update(markers=re.findall(patterns, trace), proxy_timeleft_seconds=lifetime,
               diagnostic_errors=re.findall(r"(?im)^[^\n]{0,80}\b(?:425|426|500|530|534|550)\b[^\n]{0,120}", trace))
    return row


def grid_receipt(mode, label, direction):
    path = OUT/(("anon" if mode == "anon" else "grid") + "-grid-transfer.log")
    value = []
    if path.exists():
        for line in path.read_text().splitlines():
            fields = dict(word.split("=", 1) for word in line.split() if "=" in word)
            if fields.get("FILE") == str(slot("source" if direction == "RETR" else label)/"f000000.bin") and fields.get("TYPE") == direction:
                value.append(fields)
    return value[-1:] if direction == "RETR" else value


def cp_client_args(mode, label, server_host):
    tls = "required" if mode == "tls" else "off"
    download = cp_download_mode(mode)
    auth = "anonymous" if mode == "mmap" else "token"
    peer = f"192.168.100.{server_host}"
    summary = OUT/(label + ".summary.json")
    phase_timing = "on" if IS_DIAG_VALIDATION and download else "off"
    data_frame_bytes = 262144 if download or (IS_SECURITY_5G and mode in ("clear", "tls")) else 65532
    argv = [str(BUILD/f"cpnetflux-tree-{'download' if download else 'upload'}-client"),
            "--host", peer, "--port", "45596", "--local-bind-address", IP,
            "--source-dir", "source" if download else str(slot("source")),
            "--dest-dir", str(slot(label)/"dataset") if download else "dataset",
            "--connections", "4", "--file-parallelism", "4", "--control-reuse", "worker",
            "--data-session-reuse", "tree", "--file-scheduling", "dynamic", "--data-pending-window", "2",
            "--checksum", "crc32c", "--chunk-size", "1048576", "--buffer-size", "65536",
            "--data-frame-bytes", str(data_frame_bytes), "--data-transport", "tcp",
            "--compression", "off", "--auth-mode", auth,
            "--tls-mode", tls, "--data-tls-mode", tls, "--reconnect-attempts", "0",
            "--phase-timing", phase_timing, "--json-summary", str(summary)]
    if download:
        argv += ["--final-verify-policy", "verified_chunks", "--commit-sync-policy", "none",
                 "--manifest-flush-policy", "every_n_chunks", "--manifest-flush-interval-chunks", "16",
                 "--range-checkpoint-batch-chunks", "16", "--posix-write-strategy", "direct",
                 "--file-io-buffer-size", "0"]
    if auth == "token":
        argv += ["--auth-token-file", str(CRED/"cp.token")]
    if tls == "required":
        argv += ["--tls-ca-file", str(CRED/"ca.pem"), "--tls-server-name", peer]
    return argv, env(mode, "receiver" if download else "sender")


def cp_client(mode, label, server_host):
    argv, variables = cp_client_args(mode, label, server_host)
    proc, row = run_client(argv, variables)
    (OUT/(label + ".client.log")).write_text(proc.stdout + "\n" + proc.stderr)
    summary = OUT/(label + ".summary.json")
    row["summary"] = json.loads(summary.read_text()) if summary.exists() else {}
    if IS_SECURITY_5G and mode in ("clear", "tls"):
        row["client_runtime_profile"] = {key: variables.get(key) for key in (
            "CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE",
            "CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION",
            "CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE",
            "CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES",
            "CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND")}
    return row


def cp_receipt(label):
    events = OUT/(label + ".events.jsonl")
    log = OUT/(label + ".service.log")
    lines = log.read_text(errors="replace").splitlines()
    names = [json.loads(l)["event"] for l in events.read_text().splitlines()]

    pacing = [json.loads(l.split(" ", 1)[1]) for l in lines
              if l.startswith("CPNETFLUX_SENDER_PACING ")]
    return {"events": names, "batch_error": any("CPNETFLUX_V3_BATCH_TERMINAL " in l for l in lines),
            "channels": [json.loads(l.split(" ",1)[1])["channel_index"] for l in lines
                         if l.startswith("CPNETFLUX_PERSISTENT_TIMING ")],
            "pacing": pacing}

def cp_client_receipt(label):
    path = OUT/(label + ".client.log")
    pacing = [json.loads(line.split(" ", 1)[1]) for line in path.read_text(errors="replace").splitlines()
              if line.startswith("CPNETFLUX_SENDER_PACING ")]
    return {"pacing": pacing}



def target_sha(mode, label, size):
    folder = slot(label)
    path = folder if mode in ("grid","private","anon") else folder/"dataset"
    result = rx.sha(path/"f000000.bin")
    if mode == "mmap":
        manifests = rx.baseline.check_manifests(folder, 1, size)
        result["verified_chunks"] = rx.verify_chunks(manifests["member_example"], size)
    return result


def watch_stop_path(label):
    return OUT / f"{label}.r{HOST}.watch.stop"


def request_watch_stop(label):
    path = watch_stop_path(label)
    with path.open("x") as stream:
        stream.write("stop\n")
    return {"stop_requested": True, "path": str(path)}


def run_socket_watcher(label):
    stop_path = watch_stop_path(label)
    observer = None
    observer_id = "R{}:{}:{}".format(HOST, os.getpid(), time.time_ns())
    snapshot_count = 0
    destroy_event_count = 0
    selected_socket_count = 0
    stop_seen_at = None

    def emit_sample():
        nonlocal snapshot_count, selected_socket_count
        rows = [row for row in observer.snapshot() if diag.is_experiment_socket(row)]
        observed_at_ns = time.time_ns()
        emit({"kind": "sample", "host": HOST, "run_id": RUN, "label": label,
              "observer_id": observer_id, "observed_at_ns": observed_at_ns,
              "resource": rx.resource_sample(), "sockets": rows})

        snapshot_count += 1
        selected_socket_count += len(rows)

    def emit_destroy_events():
        nonlocal destroy_event_count, selected_socket_count
        for row in observer.drain_destroy_events():
            if diag.is_experiment_socket(row):
                emit({"kind": "destroy", "host": HOST, "run_id": RUN, "label": label,
                      "observer_id": observer_id, "observed_at_ns": row["observed_at_ns"],
                      "socket": row})
                destroy_event_count += 1
                selected_socket_count += 1

    def validate_stop_marker():
        identity = stop_path.lstat()
        if (not stat.S_ISREG(identity.st_mode) or identity.st_uid != os.getuid()
                or identity.st_nlink != 1 or stop_path.read_text() != "stop\n"):
            raise RuntimeError("watch stop marker has foreign ownership or content")

    try:
        observer = diag.SocketDiagObserver()
        baseline = [row for row in observer.snapshot() if diag.is_experiment_socket(row)]
        emit({"kind": "ready", "handshake": "start", "host": HOST, "run_id": RUN,
              "label": label, "observer": "NETLINK_SOCK_DIAG",
              "observer_id": observer_id,
              "destroy_group": diag.SKNLGRP_INET_TCP_DESTROY,
              "subscribed_at_ns": observer.subscribed_at_ns,
              "event_socket_rcvbuf_bytes": observer.event_socket_rcvbuf_bytes,
              "dump_socket_rcvbuf_bytes": observer.dump_socket_rcvbuf_bytes,
              "baseline_experiment_sockets": len(baseline)})
        emit({"kind": "sample", "host": HOST, "run_id": RUN, "label": label,
              "observer_id": observer_id, "observed_at_ns": time.time_ns(),
              "resource": rx.resource_sample(), "sockets": baseline})
        snapshot_count += 1
        selected_socket_count += len(baseline)

        while True:
            emit_destroy_events()
            emit_sample()
            if stop_path.exists():
                validate_stop_marker()
                if stop_seen_at is None:
                    stop_seen_at = time.monotonic()
                if time.monotonic() - stop_seen_at >= 0.5:
                    emit_sample()
                    emit_destroy_events()
                    validate_stop_marker()
                    stop_path.unlink()
                    emit({"kind": "stopped", "handshake": "stop", "host": HOST,
                          "run_id": RUN, "label": label, "observer": "NETLINK_SOCK_DIAG",
                          "observer_id": observer_id, "snapshot_count": snapshot_count,
                          "destroy_event_count": destroy_event_count,
                          "selected_socket_observations": selected_socket_count,
                          "event_loss_count": 0, "drain_grace_seconds": 0.5,
                          "stopped_at_ns": time.time_ns()})
                    return 0
            time.sleep(0.1)
    except Exception as error:
        emit({"kind": "observer_error", "host": HOST, "run_id": RUN, "label": label,
              "observer_id": observer_id, "error": "{}: {}".format(type(error).__name__, error),
              "event_loss_count": 1, "observed_at_ns": time.time_ns()})
        return 2
    finally:
        if observer is not None:
            observer.close()


def emit(value):
    print(json.dumps(value), flush=True)


def main():
    action, *args = sys.argv[1:]
    if action == "watch":
        if len(args) != 1 or not re.fullmatch("[a-z0-9-]+", args[0]):
            raise ValueError("invalid watcher label")
        return run_socket_watcher(args[0])
    if action == "diag-selftest":
        emit(diag.loopback_destroy_selftest())
        return 0
    if action in ("mkdir","remove","stop","cp-receipt","watch-stop") and not re.fullmatch("[a-z0-9-]+", args[0]):
        raise ValueError("invalid slot")
    if action in ("service","grid-client","cp-client","grid-receipt","sha") and (
            args[0] not in MODES or not re.fullmatch("[a-z0-9-]+", args[1])):
        raise ValueError("invalid mode or slot")
    actions = {
        "prepare": prepare, "snapshot": rx.snapshot, "identity": rx.identity,
        "run-available": lambda: {"run": RUN, "available": not OUT.exists() and not ROOT.exists() and not CRED.exists()},
        "watch-stop": request_watch_stop,
        "receipt": rx.build_receipt, "seed": lambda s: seed(int(s)),
        "remove": clean_slot, "remove-upload-manifest": remove_upload_manifest,
        "mkdir": lambda label: slot(label).mkdir(),
        "request-cert": request_cert, "install-cert": install_cert, "clean": clean,
        "service": service, "stop": stop, "grid-client": lambda m,l,s,r: grid_client(m,l,int(s),int(r)),
        "grid-receipt": grid_receipt, "cp-client": cp_client, "cp-receipt": cp_receipt,
        "cp-client-receipt": cp_client_receipt,
        "sha": lambda m,l,s: target_sha(m,l,int(s)),
    }
    emit(actions[action](*args))


if __name__ == "__main__":
    os.umask(0o077)
    sys.exit(main())
