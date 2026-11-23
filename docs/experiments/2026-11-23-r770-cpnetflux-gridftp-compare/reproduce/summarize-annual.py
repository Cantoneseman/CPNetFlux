from pathlib import Path
from collections import defaultdict
from statistics import median,mean,pstdev
import json,hashlib,re
root=Path("/home/Su/experiments/CPNetFlux/2026-11-23-r770-cpnetflux-gridftp-compare-01")
cases=[]
for window in ("gsi-smoke-b","gsi-resume-c"):
    for p in sorted((root/window/"matrix").glob("*.json")):
        if "-target-" not in p.name: continue
        row=json.loads(p.read_text())
        if "tool" not in row: continue
        row["evidence"]=str(p.relative_to(root))
        row["run"]=p.name.split("-target-",1)[0]
        row["position"]=int(re.search(r"-target-(\d+)",p.name)[1])
        cases.append(row)

def group_metrics(rows):
    g=[r["task_gbps"] for r in rows]
    if not g: return {"n":0}
    return {
        "n":len(g),"median_seconds":median(r["seconds"] for r in rows),
        "median_gbps":median(g),"min_gbps":min(g),"max_gbps":max(g),"cv_pct":100*pstdev(g)/mean(g),
        "median_rx_interface_gbps":median(r["rcv_interface_gbps"] for r in rows),
        "median_host_cpu_s_per_gib":median(r["host_cpu_seconds_per_gib"] for r in rows),
        "lowest_memavailable_gib":{i:min(r["metrics"][i]["min_memavailable_kib"] for r in rows)/2**20 for i in ("1","2")},
        "peak_user_cgroup_gib":{i:max(r["metrics"][i]["cgroup_peak_total_bytes"] for r in rows)/2**30 for i in ("1","2")},
        "host_tcp_retrans_sum":{i:sum(r["metrics"][i]["host_tcp_retrans"] for r in rows) for i in ("1","2")},
        "swapout_pages":{i:sum(r["metrics"][i]["pswpout_delta"] for r in rows) for i in ("1","2")},
        "all_sha_ok":all(r["external_sha_ok"] for r in rows)
    }

def summarize():
    groups=defaultdict(list); all_attempts=defaultdict(list)
    for row in cases:
        all_attempts[row["run"]].append(row)
        if row["decision"]=="accept":
            groups[(row["run"],row["tool"])].append(row)
    bad=[{"run":r["run"],"tool":r["tool"],"seconds":r["seconds"],"sha_ok":r["external_sha_ok"],
          "terminals_ok":r["terminals_ok"],"metrics":r["metrics"],"network":r["net_reasons"],
          "memory":r["memory_reasons"],"evidence":r["evidence"]} for r in cases if r["decision"]!="accept"]
    group_list={run:{tool:group_metrics(group) for (r,tool),group in groups.items() if r==run}
                for run in sorted(all_attempts)}
    pairs={}
    for run,rows in all_attempts.items():
        if not run.startswith("main-formal"): continue
        by_index={r["position"]:r for r in rows if r["decision"]=="accept"}
        ratios=[]
        for i in range(1,11,2):
            if i not in by_index or i+1 not in by_index: continue
            pair={r["tool"]:r["task_gbps"] for r in [by_index[i],by_index[i+1]]}
            if {"cp","grid"}==set(pair):
                ratios.append({"pair":(i+1)//2,"cp_over_grid":pair["cp"]/pair["grid"],"cp_minus_grid_gbps":pair["cp"]-pair["grid"]})
        pairs[run]=ratios
    return {"groups":group_list,"pairs":pairs,"rejected":bad}

if __name__=="__main__":
    print(json.dumps(summarize(),ensure_ascii=False,indent=2))
