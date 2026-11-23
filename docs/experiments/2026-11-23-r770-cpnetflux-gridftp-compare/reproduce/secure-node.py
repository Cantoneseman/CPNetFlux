import hashlib
import json
import os
import pathlib
import re
import resource
import shutil
import signal
import subprocess
import sys
import time

P = pathlib.Path
root = P("/dev/shm/cpnetflux-annual-20261123-gsi-c")
evidence = P("/home/Su/experiments/CPNetFlux/2026-11-23-r770-cpnetflux-gridftp-compare-01/gsi-resume-c")
build = P("/home/Su/projects/CPNetFlux/build")
host, action, label, *args = sys.argv[1:]
assert host in ("1","2") and re.fullmatch(r"[a-z0-9-]+", label)
owned = root / label
logs = evidence / "samples" / label
ip = "192.168.100." + host
port = 45471
os.umask(0o077)

def output(value):
    print(json.dumps(value), flush=True)

def command(argv, **kw):
    return subprocess.check_output(argv, text=True, timeout=10, **kw)

def entries(path):
    return {line.split()[0].strip(":"): int(line.split()[1]) for line in P(path).read_text().splitlines() if len(line.split()) >= 2 and line.split()[1].isdigit()}

def snapshot():
    mem = entries("/proc/meminfo")
    netdir = P("/sys/class/net/eno17095np0/statistics")
    netnames = ("rx_bytes","tx_bytes","rx_missed_errors","rx_dropped","tx_dropped","rx_errors","tx_errors")
    nic = {name:int((netdir/name).read_text()) for name in netnames}
    extra = ("rx_out_of_buffer","rx_crc_errors_phy","rx_discards_phy","tx_discards_phy")
    for line in command(["ethtool","-S","eno17095np0"]).splitlines():
        key, colon, value = line.strip().partition(":")
        if key in extra: nic[key] = int(value)
    if not set(extra).issubset(nic):
        raise RuntimeError("missing mlx5 counters")
    hca_names=("port_rcv_errors","port_xmit_discards","port_rcv_remote_physical_errors",
               "local_link_integrity_errors","excessive_buffer_overrun_errors","link_downed","symbol_error")
    hca = P("/sys/class/infiniband/rocep11s0/ports/1/counters")
    for name in hca_names:
        nic[name] = int((hca/name).read_text())
    path = P("/sys/fs/cgroup") / P("/proc/self/cgroup").read_text().strip().split("::",1)[1].lstrip("/")
    cgroup = {}
    for cg in [path,*path.parents]:
        if str(cg) == "/sys/fs": break
        if (cg/"memory.current").exists():
            fields = {s:(cg/s).read_text().strip() for s in ("memory.current","memory.max","memory.high","memory.swap.current") if (cg/s).exists()}
            fields["events"] = entries(cg/"memory.events")
            cgroup[str(cg)] = fields
    tcp = [line.split()[1:] for line in P("/proc/net/snmp").read_text().splitlines() if line.startswith("Tcp:")]
    stat=P("/proc/stat").read_text().splitlines()[0].split()
    return {"mem_kib":{name:mem[name] for name in ("MemAvailable","MemFree","SwapFree","Shmem")},
        "pswpout":entries("/proc/vmstat")["pswpout"],"tmpfs_free":shutil.disk_usage(root).free,
        "nic":nic,"cgroup":cgroup,"tcp_retrans":int(tcp[1][tcp[0].index("RetransSegs")]),
        "pressure":P("/proc/pressure/memory").read_text(),"cpu_busy_ticks":sum(int(stat[i]) for i in (1,2,3,6,7,8)),
        "hz":os.sysconf("SC_CLK_TCK")}

def sha(path):
    t=time.monotonic()
    with path.open("rb") as f:
        digest=hashlib.file_digest(f,"sha256").hexdigest()
    return {"sha256":digest,"bytes":path.stat().st_size,"seconds":time.monotonic()-t}

def run_client(argv, env):
    start=time.monotonic()
    z=subprocess.run(argv,env=env,capture_output=True,text=True,timeout=100)
    wall=time.monotonic()-start
    usage=resource.getrusage(resource.RUSAGE_CHILDREN)
    return z, {"seconds":wall,"returncode":z.returncode,"command":argv,
               "controller_cpu_seconds":usage.ru_utime+usage.ru_stime,"controller_peak_rss_kib":usage.ru_maxrss}

if action=="init":
    assert not owned.exists()
    owned.mkdir(mode=0o700)
    logs.mkdir(parents=True)
elif action=="snapshot":
    output(snapshot())
elif action=="watch":
    start=time.monotonic()
    for _ in range(1200):
        output(snapshot())
        time.sleep(max(0.02, 0.08-((time.monotonic()-start)%0.08)))
elif action=="generate":
    import random
    n=int(args[0])
    src=owned/"source"
    src.mkdir()
    gen=random.Random(20261123)
    t=time.monotonic()
    with (src/"sample.bin").open("xb") as f:
        while n:
            count=min(n,1<<20)
            sample=gen.randbytes(count)
            f.write(sample)
            n-=count
    generation=time.monotonic()-t
    output({"generation_seconds":generation,**sha(src/"sample.bin")})
elif action=="sha":
    rel=args[0]
    assert re.fullmatch(r"(?:source|target-[a-z0-9-]+)(?:/dataset)?/sample.bin",rel)
    output(sha(owned/rel))
elif action=="grid":
    sender,receiver,slot,streams,mode=args
    endpoint="ftp://anonymous@192.168.100." if mode=="nongsi" else "gsiftp://192.168.100."
    port_head="4537" if mode=="nongsi" else "4527"
    src=f"{endpoint}{sender}:{port_head}{sender}{owned}/source/sample.bin"
    dst=f"{endpoint}{receiver}:{port_head}{receiver}{owned}/{slot}/sample.bin"
    creds=evidence/"credentials"
    env=os.environ.copy()
    env.update(X509_CERT_DIR=str(creds/"trust"),X509_USER_PROXY=str(creds/"proxy.pem"))
    argv=["globus-url-copy","-dbg","-fast","-p",streams,"-tcp-bs","1048576","-bs","262144",
          "-dcpriv" if mode=="private" else "-nodcau","-ss",f"/O=CPNetFlux Annual Test/CN=r770-{sender}.annual-test.cpnetflux.invalid",
          "-ds",f"/O=CPNetFlux Annual Test/CN=r770-{receiver}.annual-test.cpnetflux.invalid",src,dst]
    if mode=="nongsi":
        del argv[-6:-2]
        env.pop("X509_USER_PROXY",None)
        env.pop("X509_CERT_DIR",None)
    else:
        assert int(command(["grid-proxy-info","-f",str(creds/"proxy.pem"),"-timeleft"]))>600
    z,row=run_client(argv,env)
    marks = r"\b(?:PROT [CP]|DCAU [NAS])\b|\b(?:200 Protection level set to [CP]|230 User [A-Za-z0-9_-]+ logged in|226 Transfer Complete)\b"
    row["markers"]=re.findall(marks,z.stdout+"\n"+z.stderr)
    row["error_codes"]=re.findall(r"(?:^|:\s+)(?:4[0-9]{2}|5[0-9]{2})(?=[ -])",z.stdout+"\n"+z.stderr,re.M)
    output(row)
elif action=="receipt":
    slot,role,mode=args
    path=evidence/("anon-transfer.log" if mode=="nongsi" else "transfer.log")
    out=[]
    if path.exists():
        for line in path.read_text().splitlines():
            fields=dict(part.split("=",1) for part in line.split() if "=" in part)
            file=fields.get("FILE","")
            expected=owned/("source" if role=="RETR" else slot)/"sample.bin"
            if file==str(expected) and fields.get("TYPE")==role:
                out.append(fields)
    output(out[-1:] if role=="RETR" else out)
elif action=="mkdir":
    assert re.fullmatch(r"target-[a-z0-9-]+",args[0])
    (owned/args[0]).mkdir()
elif action=="cpstart":
    slot,lanes,mode=args
    target=owned/slot
    argv=[str(build/"cpnetflux-gridftp-server"),"--host",ip,"--port",str(port),
          "--data-port-base","46100","--root",str(target),"--connections",lanes,
          "--chunk-size","1048576","--buffer-size","65536","--checksum","crc32c",
          "--manifest-flush-policy","every_n_chunks","--manifest-flush-interval-chunks","16",
          "--final-verify-policy","verified_chunks","--commit-sync-policy","none",
          "--file-io-backend","posix","--posix-write-strategy","direct","--auth-mode","anonymous",
          "--tls-mode","off","--data-tls-mode","off","--data-transport","tcp","--access-policy","write-only",
          "--event-log",str(logs/(slot+"-events.jsonl"))]
    creds=evidence/"credentials"
    argv[argv.index("--auth-mode")+1]="token"
    argv += ["--auth-token-file",str(creds/"cp.token")]
    if mode=="tls":
        argv[argv.index("--tls-mode")+1]="required"
        argv[argv.index("--data-tls-mode")+1]="required"
        argv += ["--tls-cert-file",str(creds/"host.pem"),"--tls-key-file",str(creds/"host.key")]
    out=logs/(slot+"-server.log")
    with out.open("xb") as log:
        proc=subprocess.Popen(["timeout","--kill-after=5","120",*argv],stdout=log,stderr=log,
            stdin=subprocess.DEVNULL,start_new_session=True)
    ticks=P(f"/proc/{proc.pid}/stat").read_text().split(") ",1)[1].split()[19]
    (logs/(slot+"-pid.json")).write_text(json.dumps({"pid":proc.pid,"ticks":ticks,"command":argv}))
    for i in range(40):
        if proc.poll() is not None: break
        if f"{ip}:{port}" in command(["ss","-Hln","sport","=",str(port)]):
            output({"pid":proc.pid,"command":argv})
            break
        time.sleep(.05)
    else:
        raise RuntimeError("CP server listen timeout")
    if proc.poll() is not None:
        raise RuntimeError(out.read_text()[-1000:])
elif action=="cp":
    receiver,slot,lanes,pacing,mode=args
    env=os.environ.copy()
    env.pop("CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND",None)
    env["CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND"]="off"
    summary=logs/(slot+"-client.json")
    argv=[str(build/"cpnetflux-tree-upload-client"),"--host","192.168.100."+receiver,"--port",str(port),
          "--local-bind-address",ip,"--source-dir",str(owned/"source"),"--dest-dir","dataset",
          "--connections",lanes,"--file-parallelism",lanes,"--chunk-size","1048576","--buffer-size","65536","--data-frame-bytes","65532",
          "--checksum","crc32c","--compression","off","--control-reuse","worker","--data-session-reuse","tree",
          "--file-scheduling","dynamic","--data-pending-window","2","--tls-mode","off","--data-tls-mode","off",
          "--data-transport","tcp","--auth-mode","anonymous","--phase-timing","off","--transfer-timeout-ms","60000",
          "--json-summary",str(summary)]
    creds=evidence/"credentials"
    argv[argv.index("--auth-mode")+1]="token"
    argv += ["--auth-token-file",str(creds/"cp.token")]
    if mode=="tls":
        argv[argv.index("--tls-mode")+1]="required"
        argv[argv.index("--data-tls-mode")+1]="required"
        argv += ["--tls-ca-file",str(creds/"ca.pem"),"--tls-server-name","192.168.100."+receiver]
    z,row=run_client(argv,env)
    (logs/(slot+"-client.log")).write_text(z.stdout+"\n"+z.stderr)
    m=json.loads(summary.read_text()) if summary.exists() else {}
    keys=("result","error","error_code","completed_files","failed_files","completed_directories","failed_directories",
          "bytes_transferred","bytes_total","actual_transport","actual_mode","data_transport","data_connect_count",
          "lane_endpoints","control_tls_verified","data_tls_verified","compression_policy")
    row["summary"]={k:m[k] for k in keys if k in m}
    output(row)
elif action=="cpstop":
    slot=args[0]
    data=json.loads((logs/(slot+"-pid.json")).read_text())
    pid=data["pid"]
    stat=P(f"/proc/{pid}/stat")
    if stat.exists() and stat.read_text().split(") ",1)[1].split()[19]==data["ticks"]:
        os.killpg(pid,signal.SIGTERM)
    output({"stopped":True})
elif action=="clean":
    slot=args[0] if args else None
    if slot:
        assert re.fullmatch(r"target-[a-z0-9-]+",slot)
        target=owned/slot
    else:
        target=owned
    assert target.resolve()==target and target.is_relative_to(root) and target!=root
    if target.exists():
        shutil.rmtree(target)
    output(not target.exists())
elif action=="record":
    name,value=args
    assert re.fullmatch(r"[a-z0-9-]+\.json",name)
    out=evidence/"matrix"
    out.mkdir(exist_ok=True)
    with (out/name).open("x") as target:
        json.dump(json.loads(value),target,indent=2)
else:
    raise RuntimeError(action)
