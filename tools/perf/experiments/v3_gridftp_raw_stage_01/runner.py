#!/usr/bin/env python3
import hashlib
import json
import os
import resource
import re
import shlex
import shutil
import socket
import ssl
import statistics
import subprocess
import sys
import time
from pathlib import Path

BASE = Path(os.environ.get("CPNETFLUX_RAW_BASE", "/tmp/cpnetflux-runs/V3-GRIDFTP-RAW-STAGE-01/20261022T041500Z-raw-v3-55f7"))
PEER = "Su@192.168.100.2"
PEER_IP = "192.168.100.2"
LOCAL_IP = "192.168.100.1"
LOCAL_IF = "enp130s0np0"
PEER_IF = "enp177s0np0"
V3_PORT = 34261
GRIDFTP_PORT = 32811
V3_DATA_BASE = 35000
GRIDFTP_DATA_BASE = 34000
CHANNELS = int(os.environ.get("CHANNELS", "8"))
RTT_BUDGET_MS = 160
REPS = int(os.environ.get("REPS", "3"))
PENDING = int(os.environ.get("PENDING", "8"))
FAST_POLICY = os.environ.get("FAST_POLICY", "1") == "1"
SUMMARY_MODE = os.environ.get("SUMMARY_MODE", "on")
resource.setrlimit(resource.RLIMIT_NOFILE, (8192, resource.getrlimit(resource.RLIMIT_NOFILE)[1]))
TAG = os.environ.get("RUN_TAG", "formal")
SYSTEMS = os.environ.get("SYSTEMS", "v3,gridftp").split(",")
DATASETS = os.environ.get("DATASETS", "dense,large-random,large-repeated,mixed").split(",")
SERVER = None
SERVER_LOG = None
BIN_NAMES = ("cpnetflux-gridftp-server", "cpnetflux-tree-upload-client", "cpnetflux-tree-download-client")


def run(cmd, *, cwd=None, env=None, timeout=3600, check=True, stdout=None, stderr=None):
    print("$", shlex.join([str(x) for x in cmd]) if len(str(cmd)) < 1000 else str(cmd[0]) + " [structured diagnostic command]", flush=True)
    return subprocess.run([str(x) for x in cmd], cwd=cwd, env=env, timeout=timeout,
                          check=check, text=True, stdout=stdout, stderr=stderr)


def ssh(command, *, timeout=120, check=True, capture=False):
    args = ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=8", PEER, command]
    return run(args, timeout=timeout, check=check,
               stdout=subprocess.PIPE if capture else None,
               stderr=subprocess.PIPE if capture else None)


def remote_python(code, args=(), *, timeout=120, check=True):
    command = "python3 -c %s" % shlex.quote(code)
    command += " " + " ".join(shlex.quote(str(a)) for a in args)
    result = ssh(command, timeout=timeout, check=check, capture=True)
    return json.loads(result.stdout)


SNAPSHOT_CODE = r'''
import json, os, pathlib, shutil, sys
root, interface, ip, pid_path = sys.argv[1:]
iface = pathlib.Path('/sys/class/net') / interface
def counter(name):
    try: return int((iface / 'statistics' / name).read_text())
    except Exception: return None
result = {'root': root, 'interface': interface, 'ip': ip,
          'route': os.popen('ip -j route get ' + ip).read().strip(),
          'counters': {n: counter(n) for n in ('rx_bytes','tx_bytes','rx_errors','tx_errors','rx_dropped','tx_dropped')},
          'load': os.getloadavg(), 'free_bytes': shutil.disk_usage(root).free}
try: result['link_speed_mbps'] = int((iface / 'speed').read_text())
except Exception: result['link_speed_mbps'] = None
for name in ('cpu','io','memory'):
    path = pathlib.Path('/proc/pressure') / name
    if path.exists(): result.setdefault('pressure', {})[name] = path.read_text()
if pid_path and pathlib.Path(pid_path).exists():
    try:
        pid = int(pathlib.Path(pid_path).read_text())
        stat = pathlib.Path('/proc') / str(pid) / 'stat'
        fields = stat.read_text().split(')', 1)[1].split()
        result['pid'] = pid
        result['cpu_seconds'] = (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')
        result['rss_bytes'] = int(fields[21]) * os.sysconf('SC_PAGE_SIZE')
    except Exception as exc: result['pid_error'] = str(exc)
print(json.dumps(result))
'''


def snap_local(root, pid_path=""):
    return remote_python(SNAPSHOT_CODE, (root, LOCAL_IF, PEER_IP, pid_path), timeout=30) if False else json.loads(
        subprocess.run(["python3", "-c", SNAPSHOT_CODE, str(root), LOCAL_IF, PEER_IP, pid_path],
                       check=True, text=True, capture_output=True).stdout)


def snap_peer(root, pid_path=""):
    return remote_python(SNAPSHOT_CODE, (root, PEER_IF, LOCAL_IP, pid_path), timeout=30)


def tree_hash(root):
    digest = hashlib.sha256(); count = 0; total = 0
    for path in sorted(p for p in Path(root).rglob("*") if p.is_file()):
        rel = path.relative_to(root).as_posix()
        if ".cpnetflux." in rel or ".part." in rel: continue
        fh = hashlib.sha256()
        with path.open("rb") as handle:
            for block in iter(lambda: handle.read(1024 * 1024), b""): fh.update(block)
        size = path.stat().st_size
        digest.update(rel.encode() + b"\0" + str(size).encode() + b"\0" + fh.hexdigest().encode() + b"\0")
        count += 1; total += size
    return {"tree_hash": digest.hexdigest(), "file_count": count, "bytes": total}


REMOTE_HASH_CODE = r'''
import hashlib, json, pathlib, sys
root=pathlib.Path(sys.argv[1]); d=hashlib.sha256(); count=total=0
for p in sorted(x for x in root.rglob('*') if x.is_file()):
    rel=p.relative_to(root).as_posix()
    if '.cpnetflux.' in rel or '.part.' in rel: continue
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1024*1024), b''): h.update(b)
    s=p.stat().st_size; count+=1; total+=s
    d.update(rel.encode()+b'\0'+str(s).encode()+b'\0'+h.hexdigest().encode()+b'\0')
print(json.dumps({'tree_hash':d.hexdigest(),'file_count':count,'bytes':total}))
'''


def peer_hash(path):
    return remote_python(REMOTE_HASH_CODE, (path,), timeout=900)


def make_file(path, length, random_fraction):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    pattern = (b"CPNetFlux raw V3 stage dataset\n" * 4096)[:65536]
    with path.open("wb") as handle:
        index = 0
        while length:
            size = min(length, 65536)
            random_size = int(size * random_fraction)
            random_part = hashlib.shake_256(str(index).encode()).digest(random_size)
            handle.write(random_part + pattern[:size-random_size])
            length -= size; index += 1


def make_datasets(root):
    root = Path(root); root.mkdir(parents=True, exist_ok=True)
    make_file(root / "large-random" / "large.bin", 4096 * 1024**2, 1.0)
    make_file(root / "large-repeated" / "large.bin", 4096 * 1024**2, 0.0)
    for index in range(2048): make_file(root / "dense" / str(index % 32) / f"{index}.bin", 65536, 0.5)
    make_file(root / "mixed" / "large.bin", 2048 * 1024**2, 0.5)
    for index in range(512): make_file(root / "mixed" / str(index % 16) / f"{index}.bin", 8192, index % 2)
    (root / "mixed" / "empty").touch()
    make_file(root / "smoke" / "smoke.bin", 8 * 1024**2, 0.5)
    return [root / n for n in ("dense", "large-random", "large-repeated", "mixed")]


def wait_tcp_tls(cert, host, port, timeout=30):
    context = ssl.create_default_context(cafile=str(cert))
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with socket.create_connection((host, port), timeout=2) as sock:
                with context.wrap_socket(sock, server_hostname="nodeb.grid") as tls:
                    if tls.recv(512).startswith(b"220"): return
        except OSError:
            time.sleep(.25)
    raise RuntimeError("TLS service did not become ready")


def start_v3():
    global SERVER, SERVER_LOG
    server = BASE / "bin" / "cpnetflux-gridftp-server"
    args = [str(server), "--host", PEER_IP, "--port", V3_PORT, "--data-port-base", V3_DATA_BASE,
            "--root", BASE / "v3-server-root", "--checksum", "crc32c", "--auth-mode", "token",
            "--auth-token-file", BASE / "credentials" / "token", "--tls-mode", "required",
            "--tls-cert-file", BASE / "tls" / "server.crt", "--tls-key-file", BASE / "tls" / "server.key",
            "--data-tls-mode", "required", "--event-log", BASE / "logs" / "v3-server-events.jsonl"]
    if FAST_POLICY:
        args += ["--manifest-flush-policy", "final_only", "--final-verify-policy", "verified_chunks",
                 "--commit-sync-policy", "none", "--preallocate", "off", "--file-io-backend", "posix"]
    root = shlex.quote(str(BASE / "v3-server-root"))
    log = shlex.quote(str(BASE / "logs" / "v3-server.log"))
    pid = shlex.quote(str(BASE / "v3-server.pid"))
    ssh(f"install -d -m 700 {root}")
    supervisor = '''import pathlib,subprocess,sys,selectors,json,resource
resource.setrlimit(resource.RLIMIT_NOFILE,(8192,resource.getrlimit(resource.RLIMIT_NOFILE)[1]))
p=subprocess.Popen(sys.argv[2:],stdin=subprocess.DEVNULL)
pathlib.Path(sys.argv[1]).write_text(str(p.pid))
print(json.dumps({'pid':p.pid,'nofile':resource.getrlimit(resource.RLIMIT_NOFILE)}),flush=True)
s=selectors.DefaultSelector();s.register(sys.stdin,selectors.EVENT_READ)
try:
 while p.poll() is None:
  if s.select(.2): break
finally:
 if p.poll() is None: p.terminate()
 try: p.wait(timeout=10)
 except subprocess.TimeoutExpired: p.kill();p.wait()
 print(json.dumps({'server_exit':p.returncode}),flush=True)
'''
    SERVER_LOG = (BASE / "logs" / f"{TAG}-v3-server.log").open("w")
    command = "python3 -c " + shlex.quote(supervisor) + " " + pid + " " + shlex.join([str(x) for x in args])
    SERVER = subprocess.Popen(["ssh", "-o", "BatchMode=yes", PEER, command], stdin=subprocess.PIPE, stdout=SERVER_LOG, stderr=subprocess.STDOUT)
    wait_tcp_tls(BASE / "tls" / "server.crt", PEER_IP, V3_PORT)


def stop_v3():
    global SERVER, SERVER_LOG
    if SERVER is not None:
        SERVER.stdin.close()
        SERVER.wait(timeout=25)
        SERVER = None
        SERVER_LOG.close()
        ssh("rm -f " + shlex.quote(str(BASE / "v3-server.pid")))
        return
    ssh("if test -s %s; then kill -TERM $(cat %s) 2>/dev/null || true; fi; sleep 2" % (shlex.quote(str(BASE / "v3-server.pid")), shlex.quote(str(BASE / "v3-server.pid"))), check=False)


def start_gridftp():
    root = BASE / "gridftp" / "server-root"
    paths = [root, BASE / "gridftp"]
    ssh("install -d -m 777 %s %s; rm -rf %s/v3-raw" % tuple(shlex.quote(str(x)) for x in (root, BASE / "gridftp", root)))
    args = ["sudo", "-n", "globus-gridftp-server", "-S", "-p", GRIDFTP_PORT,
            "-control-interface", PEER_IP, "-data-interface", PEER_IP,
            "-port-range", f"{GRIDFTP_DATA_BASE},{GRIDFTP_DATA_BASE+100}",
            "-home-dir", root, "-auth-level", 2, "-encrypt-data", "-allow-from", LOCAL_IP,
            "-pidfile", BASE / "gridftp" / "server.pid", "-l", BASE / "gridftp" / "server.log",
            "-Z", BASE / "gridftp" / "transfer.log", "-d", "INFO"]
    ssh("ulimit -n 8192; %s" % shlex.join([str(x) for x in args]), timeout=30)
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        status = ssh("ss -ltn | grep -q ':%s '" % GRIDFTP_PORT, check=False)
        if status.returncode == 0: return
        time.sleep(.25)
    raise RuntimeError("GridFTP service did not become ready")


def stop_gridftp():
    ssh("if test -s %s; then p=$(cat %s); sudo -n kill -TERM $p 2>/dev/null || true; fi; sleep 2" % tuple(shlex.quote(str(BASE / "gridftp" / "server.pid")) for _ in range(2)), check=False)


def run_v3_case(dataset, expected, rep, logs):
    name = f"{TAG}-{dataset.name}-r{rep}"
    remote_dest = BASE / "v3-server-root" / name
    ssh("rm -rf %s" % shlex.quote(str(remote_dest)))
    summary = BASE / "cases" / f"v3-{name}.summary.json"
    event = BASE / "cases" / f"v3-{name}.events.jsonl"
    log = BASE / "cases" / f"v3-{name}.client.log"
    cpu = BASE / "cases" / f"v3-{name}.cpu.json"
    metrics = BASE / "metrics" / f"v3-{name}"
    before = snap_local(BASE, str(BASE / "v3-server.pid")); peer_before = snap_peer(BASE, str(BASE / "v3-server.pid"))
    command = ["/usr/bin/time", "-f", '{"user_seconds":%U,"system_seconds":%S,"max_rss_kib":%M}', "-o", cpu,
               BASE / "bin" / "cpnetflux-tree-upload-client", "--host", PEER_IP, "--port", V3_PORT,
               "--source-dir", dataset, "--dest-dir", name, "--connections", 1, "--file-parallelism", CHANNELS,
               "--checksum", "crc32c", "--checksum-backend", "auto",
               "--chunk-size", 4*1024*1024,
               "--local-bind-address", LOCAL_IP, "--peer-addresses", PEER_IP, "--control-reuse", "worker",
               "--data-session-reuse", "tree", "--file-scheduling", "dynamic", "--data-pending-window", PENDING,
               "--auth-mode", "token", "--auth-token-file", BASE / "credentials" / "token", "--tls-mode", "required",
               "--tls-server-name", "nodeb.grid", "--tls-ca-file", BASE / "tls" / "server.crt", "--data-tls-mode", "required",
               "--scheduler", "global", "--scheduler-policy", "fixed", "--scheduler-metrics-dir", metrics,
               "--scheduler-link-id", "link0", "--scheduler-capacity-gbps", 100, "--scheduler-workitem-min-bytes", 512*1024*1024,
               "--scheduler-workitem-max-bytes", 512*1024*1024, "--scheduler-default-rtt-ms", RTT_BUDGET_MS,
               "--compression", "off", "--transfer-timeout-ms", 180000,
               "--json-summary", summary, "--event-log", event, "--phase-timing", "on"]
    if SUMMARY_MODE == "off":
        position = command.index("--json-summary"); del command[position:position+2]
    if os.environ.get("TRACE_SYSCALLS") == "1":
        command = ["strace", "-f", "-c", "-o", str(BASE / "cases" / f"{name}.strace.txt"), *command]
    monitor = subprocess.Popen(["python3", str(BASE / "sample.py"), str(BASE / "metrics" / f"v3-{name}.local.jsonl"), LOCAL_IF])
    remote_monitor = subprocess.Popen(["ssh", PEER, f"python3 {BASE}/sample.py {BASE}/metrics/v3-{name}.peer.jsonl {PEER_IF} stdin"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL)
    started = time.monotonic()
    with log.open("w", encoding="utf-8") as handle: result = run(command, timeout=2400, check=False, stdout=handle, stderr=subprocess.STDOUT)
    elapsed = time.monotonic() - started
    monitor.terminate(); monitor.wait()
    remote_monitor.stdin.close(); remote_monitor.wait(timeout=10)
    after = snap_local(BASE, str(BASE / "v3-server.pid")); peer_after = snap_peer(BASE, str(BASE / "v3-server.pid"))
    observed = json.loads(summary.read_text()) if SUMMARY_MODE == "on" and summary.exists() else {}
    if SUMMARY_MODE == "off":
        tokens = dict(re.findall(r"([a-z_]+)=([^\s]+)",log.read_text()))
        observed = {"result":tokens.get("result"),"elapsed_seconds":float(tokens.get("elapsed_seconds",0))}
    verify_started = time.monotonic(); actual = peer_hash(remote_dest)
    verification_wall = time.monotonic() - verify_started
    cpu_data = json.loads(cpu.read_text().splitlines()[-1]) if cpu.exists() else {}
    row = {"system":"cpnetflux-v3","dataset":dataset.name,"repetition":rep,"exit_code":result.returncode,
           "wall_seconds":elapsed,"expected":expected,"observed":actual,"hash_match":actual==expected,
           "client_cpu":cpu_data,"local_before":before,"local_after":after,"peer_before":peer_before,"peer_after":peer_after,
           "summary":observed,"log":str(log),"stage":"v3_client", "verification_wall_seconds":verification_wall,
           "profile":{"channels":CHANNELS,"pending":PENDING,"fast_policy":FAST_POLICY,"chunk_bytes":4194304,"nofile":8192,"compression":"off","summary_mode":SUMMARY_MODE}}
    logs.append(row)
    with (BASE / f"{TAG}-results.jsonl").open("a") as f: f.write(json.dumps(row)+"\n")
    if result.returncode != 0 or actual != expected or observed.get("result") != "pass" or (SUMMARY_MODE == "on" and (observed.get("compression_attempts") != 0 or observed.get("actual_transport") != "v3")):
        raise RuntimeError(json.dumps({"v3_case_failed": row}, ensure_ascii=False))
    ssh("rm -rf %s" % shlex.quote(str(remote_dest)))
    print(json.dumps({"system":"v3","dataset":dataset.name,"rep":rep,"seconds":elapsed,"pass":True}), flush=True)


def run_gridftp_case(dataset, expected, rep, logs):
    name = f"{TAG}-{dataset.name}-r{rep}"
    remote_dest = BASE / "gridftp" / "server-root" / "v3-raw" / name
    ssh("rm -rf %s" % shlex.quote(str(remote_dest)))
    log = BASE / "cases" / f"gridftp-{name}.client.log"; cpu = BASE / "cases" / f"gridftp-{name}.cpu.json"
    before = snap_local(BASE, str(BASE / "gridftp" / "server.pid")); peer_before = snap_peer(BASE, str(BASE / "gridftp" / "server.pid"))
    env = os.environ.copy(); env["X509_USER_PROXY"] = str(BASE / "credentials" / "x509up_u1002"); env["X509_CERT_DIR"] = "/etc/grid-security/certificates"
    command = ["/usr/bin/time", "-f", '{"user_seconds":%U,"system_seconds":%S,"max_rss_kib":%M}', "-o", cpu,
               "globus-url-copy", "-dcpriv", "-fast", "-cd", "-r", "-rp", "-cc", CHANNELS, "-p", CHANNELS, "-rst", "-vb", "-dbg",
               f"file://{dataset}/", f"gsiftp://nodeb.grid:{GRIDFTP_PORT}/v3-raw/{name}/"]
    monitor = subprocess.Popen(["python3", str(BASE / "sample.py"), str(BASE / "metrics" / f"gridftp-{name}.local.jsonl"), LOCAL_IF])
    remote_monitor = subprocess.Popen(["ssh", PEER, f"sudo -n python3 {BASE}/sample.py {BASE}/metrics/gridftp-{name}.peer.jsonl {PEER_IF} stdin"], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL)
    started = time.monotonic()
    with log.open("w", encoding="utf-8") as handle:
        process = subprocess.Popen([str(x) for x in command], env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        for line in process.stdout:
            # Record protocol boundaries without GSI tokens or protected payloads.
            matched = re.search(r"(?:^|[\s\]>:])([1-5][0-9]{2})(?:[- ]|$)",line)
            if matched:
                clean = "response " + matched.group(1)
            elif re.search(r"\b(AUTH|ADAT|FEAT|USER|PASS|PROT|PBSZ|TYPE|MODE|STOR|RETR|MLSD|MLST|PASV|EPSV|SPAS|SPOR|PORT|EPRT|CWD|MKD|QUIT|OPTS)\b",line):
                clean = "command " + re.search(r"\b(AUTH|ADAT|FEAT|USER|PASS|PROT|PBSZ|TYPE|MODE|STOR|RETR|MLSD|MLST|PASV|EPSV|SPAS|SPOR|PORT|EPRT|CWD|MKD|QUIT|OPTS)\b",line).group(1)
            elif any(x in line.upper() for x in ("ADAT", "MIC ", "ENC ", "PRIVATE KEY", "CERTIFICATE")):
                clean = "GSI exchange [content omitted]"
            elif line.lstrip().startswith(("Source:","Dest:","[error", "error:", "Total")):
                clean = line.strip()[:250]
            else:
                clean = "diagnostic [content omitted]"
            handle.write(json.dumps({"elapsed":time.monotonic()-started,"event":clean})+"\n")
        result = subprocess.CompletedProcess(command, process.wait(timeout=2400))
    elapsed = time.monotonic() - started
    monitor.terminate(); monitor.wait()
    remote_monitor.stdin.close(); remote_monitor.wait(timeout=10)
    after = snap_local(BASE, str(BASE / "gridftp" / "server.pid")); peer_after = snap_peer(BASE, str(BASE / "gridftp" / "server.pid"))
    verify_started = time.monotonic(); actual = peer_hash(remote_dest)
    verification_wall = time.monotonic() - verify_started
    cpu_data = json.loads(cpu.read_text().splitlines()[-1]) if cpu.exists() else {}
    row = {"system":"gridftp","dataset":dataset.name,"repetition":rep,"exit_code":result.returncode,
           "wall_seconds":elapsed,"expected":expected,"observed":actual,"hash_match":actual==expected,
           "client_cpu":cpu_data,"local_before":before,"local_after":after,"peer_before":peer_before,"peer_after":peer_after,
           "log":str(log),"stage":"gridftp_client","auth_mode":"gsi","data_channel_protection":"private","parallelism":CHANNELS,
           "verification_wall_seconds":verification_wall,"profile":{"channels":CHANNELS,"fast":True,"nofile":8192}}
    logs.append(row)
    with (BASE / f"{TAG}-results.jsonl").open("a") as f: f.write(json.dumps(row)+"\n")
    if result.returncode != 0 or actual != expected:
        raise RuntimeError(json.dumps({"gridftp_case_failed": row}, ensure_ascii=False))
    ssh("rm -rf %s" % shlex.quote(str(remote_dest)))
    print(json.dumps({"system":"gridftp","dataset":dataset.name,"rep":rep,"seconds":elapsed,"pass":True}), flush=True)


def main():
    for path in (BASE, BASE/"bin", BASE/"credentials", BASE/"tls", BASE/"payload", BASE/"cases", BASE/"metrics", BASE/"logs"):
        Path(path).mkdir(parents=True, exist_ok=True)
    if not (BASE / "credentials" / "x509up_u1002").exists(): raise RuntimeError("missing GSI proxy")
    payload = BASE / "payload"
    if not (payload / "large-random").exists():
        print("generating payload", flush=True); make_datasets(payload)
    expected = {p.name: tree_hash(p) for p in (payload/"dense", payload/"large-random", payload/"large-repeated", payload/"mixed")}
    (BASE / "payload-manifest.json").write_text(json.dumps(expected, indent=2), encoding="utf-8")
    rows = []
    try:
        if "v3" in SYSTEMS:
            start_v3()
            for dataset in (payload/name for name in DATASETS):
                for rep in range(1, REPS+1): run_v3_case(dataset, expected[dataset.name], rep, rows)
            stop_v3()
        if "gridftp" in SYSTEMS:
            start_gridftp()
            run_gridftp_case(payload/"smoke", tree_hash(payload/"smoke"), 0, rows)
            for dataset in (payload/name for name in DATASETS):
                for rep in range(1, REPS+1): run_gridftp_case(dataset, expected[dataset.name], rep, rows)
    finally:
        stop_v3(); stop_gridftp()
        (BASE / f"{TAG}-cleanup.json").write_text(json.dumps({"payload_retained":True,"credentials_retained_for_audit":True}, indent=2), encoding="utf-8")
    print(json.dumps({"result":"pass","formal_rows":len([r for r in rows if r['repetition']>0]),"evidence":str(BASE)}, ensure_ascii=False))


if __name__ == "__main__": main()
