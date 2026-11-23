import concurrent.futures as cf
import json
import pathlib
import re
import shlex
import subprocess
import sys
import tempfile
import time

RUN = "/home/Su/experiments/CPNetFlux/2026-11-23-r770-cpnetflux-gridftp-compare-01/gsi-resume-c"
HOSTS = ("1", "2")
MIN_MEM_KIB = 12 * 1024**2
BASELINE_OVERHEAD = 256 * 1024**2

def argv(host, action, label, *args):
    remote = shlex.join(["python3", RUN + "/annual-node.py", host, action, label, *map(str,args)])
    return ["ssh","-o","BatchMode=yes","-o","ConnectTimeout=6","r770-"+host,remote]

def call(host, action, label, *args):
    p = subprocess.run(argv(host,action,label,*args), capture_output=True, encoding="utf-8", timeout=120)
    if p.returncode:
        raise RuntimeError(f"{host} {action} rc={p.returncode}: {p.stderr[-800:]}")
    return json.loads(p.stdout) if p.stdout.strip() else None

def both(action, label, *args):
    with cf.ThreadPoolExecutor(max_workers=2) as pool:
        fs={h:pool.submit(call,h,action,label,*args) for h in HOSTS}
        return {h:f.result() for h,f in fs.items()}

def record(label, name, obj):
    # Stream potentially large diagnostic records through SSH, outside the timed window.
    remote = "umask 077 && mkdir -p " + RUN + "/matrix && set -C && cat > " + RUN + "/matrix/" + label + "-" + name + ".json"
    p=subprocess.run(["ssh","-o","BatchMode=yes","r770-1",remote],input=json.dumps(obj,indent=2)+"\n",text=True,timeout=15)
    p.check_returncode()

def memory_limits(snap):
    for cg in snap["cgroup"].values():
        for key in ("memory.max","memory.high"):
            if cg[key] != "max":
                yield int(cg[key])-int(cg["memory.current"])

def capacity(snapshots, size):
    return [h for h,snap in snapshots.items()
        if snap["mem_kib"]["MemAvailable"]*1024 < MIN_MEM_KIB*1024+size+BASELINE_OVERHEAD
        or snap["tmpfs_free"] < size+BASELINE_OVERHEAD
        or any(limit < size+BASELINE_OVERHEAD for limit in memory_limits(snap))]

def deltas(before, after):
    return {k:after[k]-v for k,v in before.items()}

def observe(before, after, readings, *, require_samples=True):
    network_all, memory_all, counters, metrics = [], [], {}, {}
    for host in HOSTS:
        a,b=before[host],after[host]
        samples=[a,*readings[host],b]
        n=deltas(a["nic"],b["nic"])
        counters[host]=n
        for k,d in n.items():
            if k not in ("rx_bytes","tx_bytes") and d:
                network_all.append(f"R{host} {k} {d:+d}")
        if require_samples and not readings[host]:
            network_all.append(f"R{host} missing in-flight observation")
        peak=max(int(z["cgroup"]["/sys/fs/cgroup/user.slice/user-1002.slice"]["memory.current"]) for z in samples)
        mem=min(z["mem_kib"]["MemAvailable"] for z in samples)
        psi=max(int(re.search(r"full .*?total=(\d+)",z["pressure"])[1]) for z in samples)-int(re.search(r"full .*?total=(\d+)",a["pressure"])[1])
        metrics[host]={"min_memavailable_kib":mem,"min_memfree_kib":min(z["mem_kib"]["MemFree"] for z in samples),
            "min_tmpfs_free_bytes":min(z["tmpfs_free"] for z in samples),"min_swap_free_kib":min(z["mem_kib"]["SwapFree"] for z in samples),
            "cgroup_peak_total_bytes":peak, "host_cpu_busy_seconds":(b["cpu_busy_ticks"]-a["cpu_busy_ticks"])/a["hz"],
            "host_tcp_retrans":b["tcp_retrans"]-a["tcp_retrans"],"memory_full_stall_us":psi,
            "pswpout_delta":b["pswpout"]-a["pswpout"],"samples":len(readings[host])}
        if mem < MIN_MEM_KIB or metrics[host]["min_tmpfs_free_bytes"]<=0 or psi>0:
            memory_all.append(f"R{host} memory floor/tmpfs/PSI")
        if any(z["pswpout"]!=a["pswpout"] or z["mem_kib"]["SwapFree"]<a["mem_kib"]["SwapFree"] for z in samples):
            memory_all.append(f"R{host} swap changed")
        for key in a["cgroup"].keys() & b["cgroup"].keys():
            for event,d in deltas(a["cgroup"][key]["events"],b["cgroup"][key]["events"]).items():
                if d:
                    memory_all.append(f"R{host} cgroup {key} {event} {d:+d}")
            if int(b["cgroup"][key].get("memory.swap.current",0)) > int(a["cgroup"][key].get("memory.swap.current",0)):
                memory_all.append(f"R{host} cgroup swap")
    return network_all,memory_all,counters,metrics

def sample(label,slot,tool,sender,expected,streams,pacing="1500000000"):
    receiver="1" if sender=="2" else "2"
    slot="target-"+slot
    started=False
    reads={}
    before=both("snapshot",label)
    if failing:=capacity({receiver:before[receiver]},expected["bytes"]):
        record(label,slot,{"decision":"capacity","before":before,"hosts":failing})
        return "capacity"
    call(receiver,"mkdir",label,slot)
    try:
        if tool=="cp":
            call(receiver,"cpstart",label,slot,streams)
            started=True
        before=both("snapshot",label)
        with tempfile.TemporaryDirectory(prefix="cpnetflux-annual-watch-") as watchdir:
            paths={h:pathlib.Path(watchdir)/h for h in HOSTS}
            watches={}
            for h in HOSTS:
                with paths[h].open("w") as sink:
                    watches[h]=subprocess.Popen(argv(h,"watch",label),stdout=sink,stderr=subprocess.DEVNULL)
            try:
                started_watch=time.monotonic()
                while not all(p.stat().st_size for p in paths.values()):
                    if time.monotonic()-started_watch>8:
                        raise RuntimeError("watcher did not start")
                    time.sleep(.05)
                if tool=="cp":
                    row=call(sender,"cp",label,receiver,slot,streams,pacing)
                else:
                    row=call("1","grid",label,sender,receiver,slot,streams,"private" if tool=="grid-private" else "clear")
            finally:
                for watcher in watches.values(): watcher.terminate()
                for watcher in watches.values(): watcher.wait(timeout=8)
                for h,p in paths.items():
                    reads[h]=[]
                    for line in p.read_text(encoding="utf-8").splitlines():
                        try: reads[h].append(json.loads(line))
                        except json.JSONDecodeError: pass
        after=both("snapshot",label)
        net,mem,nic,metrics=observe(before,after,reads)
        target=slot+("/dataset" if tool=="cp" else "")+"/sample.bin"
        actual=None
        if row["returncode"]==0:
            try: actual=call(receiver,"sha",label,target)
            except RuntimeError: pass
        integrity=actual is not None and actual["bytes"]==expected["bytes"] and actual["sha256"]==expected["sha256"]
        receipts={}
        if tool=="cp":
            summary=row["summary"]
            terminal=(summary.get("result")=="pass" and summary.get("bytes_transferred")==expected["bytes"]
                and summary.get("completed_files")==1 and summary.get("failed_files")==0
                and summary.get("actual_transport")=="v3" and summary.get("data_transport")=="tcp"
                and summary.get("compression_policy")=="off" and summary.get("data_connect_count")==int(streams))
        else:
            receipts={sender:call(sender,"receipt",label,slot,"RETR"),receiver:call(receiver,"receipt",label,slot,"STOR")}
            terminal=all(len(recs)==1 and recs[0].get("CODE")=="226"
                and int(recs[0].get("NBYTES",0))==expected["bytes"]
                and int(recs[0].get("STREAMS",0))==int(streams)
                for recs in receipts.values())
            markers=row.get("markers",[])
            terminal = terminal and markers.count("230 User Su logged in")==2
            terminal = terminal and (
                markers.count("200 Protection level set to P")==2 if tool=="grid-private"
                else markers.count("DCAU N")>=2 and "PROT P" not in markers)
        decision="stop" if net or not terminal or not integrity else ("capacity" if mem else "accept")
        row.update(decision=decision,tool=tool,sender=sender,receiver=receiver,net_reasons=net,
            memory_reasons=mem,nic_deltas=nic,metrics=metrics,source=expected,destination=actual,
            external_sha_ok=integrity,terminals_ok=terminal,receipts=receipts,
            rcv_interface_gbps=nic[receiver]["rx_bytes"]*8e-9/row["seconds"],
            host_cpu_seconds_per_gib=sum(z["host_cpu_busy_seconds"] for z in metrics.values())/(expected["bytes"]/2**30),
            before=before,after=after,observations=reads)
        if decision=="accept":
            row["task_gbps"]=expected["bytes"]*8e-9/row["seconds"]
        record(label,slot,row)
        print(json.dumps({k:row[k] for k in ("decision","tool","seconds","net_reasons","memory_reasons","terminals_ok","external_sha_ok","returncode")},ensure_ascii=False),flush=True)
        return decision
    finally:
        if started: call(receiver,"cpstop",label,slot)
        call(receiver,"clean",label,slot)

def main():
    label=sys.argv[1]
    size=int(sys.argv[2])
    sender=sys.argv[3]
    profiles=[token.split(":") for token in sys.argv[4:]]
    assert re.fullmatch(r"[a-z0-9-]+",label)
    safety=both("snapshot",label)
    if rejected:=capacity(safety,size):
        record(label,"preallocation-rejected",{"decision":"capacity","size":size,"hosts":rejected,"before":safety})
        return 3
    both("init",label)
    try:
        expected=call(sender,"generate",label,size)
        after_generation=both("snapshot",label)
        net,mem,_,_=observe(safety,after_generation,{h:[] for h in HOSTS},require_samples=False)
        record(label,"source",{"source":sender,"expected":expected,"baseline":safety,
                              "after_generation":after_generation,"network_reasons":net,"memory_reasons":mem})
        if net or mem:
            print(json.dumps({"decision":"stop" if net else "capacity","phase":"generation",
                              "network_reasons":net,"memory_reasons":mem}),flush=True)
            return 2 if net else 3
        result="accept"
        for index,(tool,streams) in enumerate(profiles,1):
            result=sample(label,f"{index}-{tool}-{streams}",tool,sender,expected,streams)
            if result!="accept": break
    finally:
        cleaned=both("clean",label)
        record(label,"cleanup",{"cleaned":cleaned,"after":both("snapshot",label)})
    return {"accept":0,"stop":2,"capacity":3}[result]

if __name__=="__main__":
    sys.exit(main())
