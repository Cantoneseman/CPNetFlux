#!/usr/bin/env python3
"""Local TLS V3 single-variable paired range diagnostics; never changes system networking."""
import argparse
import atexit
import hashlib
import json
import os
import resource
import shutil
import socket
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"test"))
from tree_smoke_common import free_port, file_sha256, stop_server
from gridftp_port_window import clamp_passive_data_port_base
from run_gridftp_data_tls_smoke import generate_cert
from range_observation import export_range_observations, write_evidence_bundle

def summarize(files):
    totals={}
    lanes={}
    for f in files:
        lane=lanes.setdefault(str(f["channel_index"]),{})
        values={k:f[k] for k in ("read_seconds","checksum_seconds","payload_io_seconds",
            "socket_write_seconds","frame_prepare_seconds","pipeline_producer_wait_seconds",
            "pipeline_consumer_wait_seconds","producer_thread_cpu_seconds","lane_thread_cpu_seconds")
            if isinstance(f.get(k),(int,float))}
        r=f["range_observation"]
        values.update({k:v for k,v in r.items() if isinstance(v,(int,float))})
        values.update({"manifest_"+k:v for k,v in r["manifest_save"].items()})
        for k,v in values.items():
            totals[k]=totals.get(k,0)+v
            lane[k]=lane.get(k,0)+v
        lane["pipeline_high_watermark_bytes"]=max(lane.get("pipeline_high_watermark_bytes",0),f["pipeline_high_watermark_bytes"])
        lane["pipeline_budget_bytes"]=max(lane.get("pipeline_budget_bytes",0),f["pipeline_budget_bytes"])
    return dict(metric_sums_not_additive=totals,lanes=lanes)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir",type=Path,required=True)
    p.add_argument("--evidence-dir",type=Path,required=True)
    p.add_argument("--bytes",type=int,default=536870912)
    p.add_argument("--rounds",type=int,default=5)
    p.add_argument("--quick",action="store_true",help="one calibration and depth pair for harness validation only")
    p.add_argument("--experiment",choices=("pipeline","checkpoint","observation-overhead"),default="observation-overhead")
    a=p.parse_args()
    if a.bytes<134217728 or a.bytes%65536: p.error("--bytes must be a multiple of 65536 and >=128 MiB")
    if a.rounds<5 and not a.quick:p.error("acceptance needs >=5 paired rounds")
    root=a.evidence_dir.absolute()
    root.mkdir(parents=True,exist_ok=False)
    build=a.build_dir.absolute()
    repo=Path(__file__).resolve().parents[2]
    binary_hashes={name:file_sha256(build/name) for name in (
        "cpnetflux-gridftp-server","cpnetflux-tree-upload-client","cpnetflux-tree-download-client")}
    scratch=Path(tempfile.mkdtemp(prefix="cpnetflux-range-observation-"))
    atexit.register(shutil.rmtree,scratch,ignore_errors=True)
    cert,key=scratch/"cert.pem",scratch/"key.pem"
    assert generate_cert(cert,key),"openssl unavailable"
    source=scratch/"source";source.mkdir()
    block=bytes(i%251 for i in range(65536))
    with (source/"large.bin").open("wb") as out:
        for _ in range(a.bytes//len(block)): out.write(block)
    expected=file_sha256(source/"large.bin")
    # Read once to warm the same input before either arm; no cache-drop/system tuning.
    report=dict(schema="cpnetflux_range_observation_experiment_v1",result="running",
        input_bytes=a.bytes,input_sha256=expected,binaries=binary_hashes,
        commit=subprocess.check_output(["git","rev-parse","HEAD"],text=True,cwd=repo).strip(),
        profile=dict(transport="loopback_TCP",tls="control_and_data_required",crc="crc32c_auto",
            compression="off",lanes=4,chunk_bytes=4194304,buffer_bytes=65536,
            manifest_flush="final_only",final_verify="verified_chunks",commit_sync="none",
            cache="source_read_once_no_cache_drop_no_residency_guarantee",process_wall="client_spawn_through_exit_includes_summary",
            hash_validation="independent_SHA256_outside_timed_wall",
        limitations="loopback TCP, host page cache not controlled, no CPU isolation, not a physical-disk or WAN result"),
        cpu_profile=dict(perf_path=shutil.which("perf"),
            perf_event_paranoid=Path("/proc/sys/kernel/perf_event_paranoid").read_text().strip(),
            stack_status="unavailable: perf executable absent" if not shutil.which("perf") else "not_collected",
            available_cpus=os.cpu_count(),meminfo=Path("/proc/meminfo").read_text(),lane_cpu="CLOCK_THREAD_CPUTIME_ID in producer and lane"),
        runs=[])
    plan=[]
    if a.experiment=="pipeline":
        for i in range(1 if a.quick else a.rounds):
            arms=[("off",2,1),("on",2,1)]
            if i%2:arms.reverse()
            plan.append(("calibration",i,arms))
        for depth in ([4] if a.quick else [4,8,16]):
            for i in range(1 if a.quick else a.rounds):
                arms=[("on",2,1),("on",depth,1)]
                if i%2:arms.reverse()
                plan.append(("depth-"+str(depth),i,arms))
    elif a.experiment=="checkpoint":
        for batch in ([4] if a.quick else [4,16]):
            for i in range(1 if a.quick else a.rounds):
                arms=[("on",2,1),("on",2,batch)]
                if i%2:arms.reverse()
                plan.append(("checkpoint-"+str(batch),i,arms))
    else:
        for i in range(1 if a.quick else a.rounds):
            arms=[("off",2,1),("on",2,1)]
            if i%2:arms.reverse()
            plan.append(("observation-overhead",i,arms))
    report["experiment"]=a.experiment
    for group,index,arms in plan:
        for observation,depth,batch in arms:
            label=f"{group}-{index}-{observation}-{depth}"+(f"-batch{batch}" if a.experiment=="checkpoint" else "")
            case=root/label;case.mkdir()
            peer=case/"peer";peer.mkdir()
            env=dict(os.environ,CPNETFLUX_RANGE_TIMING=observation,
                     CPNETFLUX_PAYLOAD_PIPELINE_DEPTH=str(depth),CPNETFLUX_RANGE_CHECKPOINT_BATCH=str(batch))
            port,data=free_port(),clamp_passive_data_port_base(free_port())
            log=case/"server.log"
            server_cmd=[str(build/"cpnetflux-gridftp-server"),"--host","127.0.0.1",
                "--port",str(port),"--root",str(peer),"--data-port-base",str(data),
                "--connections","4","--link-set-max-links","4","--checksum","crc32c",
                "--checksum-backend","auto","--chunk-size","4194304","--buffer-size","65536",
                "--manifest-flush-policy","final_only","--final-verify-policy","verified_chunks",
                "--commit-sync-policy","none","--tls-mode","required","--tls-cert-file",str(cert),
                "--tls-key-file",str(key),"--data-tls-mode","required"]
            with log.open("w") as output:
                server=subprocess.Popen(server_cmd,stdout=output,stderr=subprocess.STDOUT,env=env)
            try:
                deadline=time.monotonic()+10
                while True:
                    try:
                        with socket.create_connection(("127.0.0.1",port),timeout=.2):break
                    except OSError:
                        if time.monotonic()>deadline:raise
                        time.sleep(.02)
                summary=case/"summary.json"
                cmd=[str(build/"cpnetflux-tree-upload-client"),"--host","127.0.0.1",
                    "--port",str(port),"--source-dir",str(source),"--dest-dir","received",
                    "--checksum","crc32c","--checksum-backend","auto","--file-parallelism","4",
                    "--connections","4","--link-set-links","4","--chunk-size","4194304",
                    "--buffer-size","65536","--control-reuse","worker","--data-session-reuse","tree",
                    "--control-pipeline-depth","2",
                    "--compression","off","--tls-mode","required","--tls-ca-file",str(cert),
                    "--tls-server-name","localhost","--data-tls-mode","required",
                    "--phase-timing","on","--json-summary",str(summary)]
                before=resource.getrusage(resource.RUSAGE_CHILDREN)
                started=time.monotonic()
                done=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=180)
                wall=time.monotonic()-started
                after=resource.getrusage(resource.RUSAGE_CHILDREN)
                (case/"client.log").write_text(done.stdout+done.stderr)
                assert done.returncode==0,done.stdout+done.stderr
                observed=json.loads(summary.read_text())
                assert observed["result"]=="pass"
                assert observed["bytes_transferred"]==a.bytes
                assert observed.get("compression_attempts",0)==0
                actual=file_sha256(peer/"received"/"large.bin")
                assert actual==expected,(label,actual,expected)
            finally:
                stop_server(server,log)
            remote=[]
            for line in log.read_text().splitlines():
                marker="CPNETFLUX_PERSISTENT_TIMING "
                if marker in line:remote.extend(json.loads(line.split(marker,1)[1])["files"])
            assert remote and all(f["range_id"] for f in remote),"range observation missing"
            row=dict(label=label,group=group,pair_index=index,observation=observation,depth=depth,checkpoint_batch=batch,
                process_wall_seconds=wall,elapsed_seconds=observed["elapsed_seconds"],
                summary_hash_seconds=observed["summary_tree_hash_seconds"],
                client_cpu=dict(user_seconds=after.ru_utime-before.ru_utime,system_seconds=after.ru_stime-before.ru_stime),
                sha256=actual,sender=summarize(observed["file_timings"]),receiver=summarize(remote))
            report["runs"].append(row)
            (root/"result.json").write_text(json.dumps(report,indent=2)+"\n")
            print(json.dumps({k:row[k] for k in ("label","process_wall_seconds","elapsed_seconds")}),flush=True)
            # Remove only this run's generated destination, after independent verification.
            assert peer.parent==case and case.parent==root
            shutil.rmtree(peer)
    aggregates={}
    for group in sorted({r["group"] for r in report["runs"]}):
        rows=[r for r in report["runs"] if r["group"]==group]
        arms={}
        for observation,depth,batch in sorted({(r["observation"],r["depth"],r["checkpoint_batch"]) for r in rows}):
            values=[r["process_wall_seconds"] for r in rows if r["observation"]==observation and r["depth"]==depth and r["checkpoint_batch"]==batch]
            core=[r["elapsed_seconds"] for r in rows if r["observation"]==observation and r["depth"]==depth and r["checkpoint_batch"]==batch]
            key=f"{observation}-{depth}"+(f"-batch{batch}" if a.experiment=="checkpoint" else "")
            arms[key]=dict(n=len(values),median=statistics.median(values),
                minimum=min(values),maximum=max(values),core_median=statistics.median(core))
        aggregates[group]=arms
    assert binary_hashes=={name:file_sha256(build/name) for name in binary_hashes},"binaries changed"
    report.update(result="pass",aggregates=aggregates)
    if a.experiment=="observation-overhead":
        pairs=[]
        for index in sorted({r["pair_index"] for r in report["runs"]}):
            arms={r["observation"]:r for r in report["runs"] if r["pair_index"]==index}
            if set(arms)!={"off","on"}: raise ValueError(f"incomplete overhead pair: {index}")
            off,on=arms["off"],arms["on"]
            pairs.append({
                "pair_index":index,
                "process_wall_delta_seconds":on["process_wall_seconds"]-off["process_wall_seconds"],
                "process_wall_delta_percent":(on["process_wall_seconds"]/off["process_wall_seconds"]-1)*100,
                "elapsed_delta_seconds":on["elapsed_seconds"]-off["elapsed_seconds"],
                "elapsed_delta_percent":(on["elapsed_seconds"]/off["elapsed_seconds"]-1)*100,
                "off_process_wall_seconds":off["process_wall_seconds"],
                "on_process_wall_seconds":on["process_wall_seconds"],
                "off_elapsed_seconds":off["elapsed_seconds"],
                "on_elapsed_seconds":on["elapsed_seconds"],
            })
        report["observation_overhead_pairs"]=pairs
        report["observation_overhead_summary"]={
            key:statistics.median(row[key] for row in pairs)
            for key in ("process_wall_delta_seconds","process_wall_delta_percent",
                        "elapsed_delta_seconds","elapsed_delta_percent")
        }
        report["observation_overhead_interpretation"]=(
            "paired medians are descriptive only; short loopback transfers on a shared 2-vCPU host "
            "do not isolate timer overhead from scheduling and cache variation"
        )
        (root/"observation-overhead.json").write_text(
            json.dumps(report["observation_overhead_summary"],indent=2)+"\n")
    server_logs=sorted(root.glob("*/server.log"))
    range_export=export_range_observations(server_logs,root)
    report["range_observation_export"]={
        "status":range_export["status"],
        "counts":range_export["counts"],
        "source_logs":range_export["source_logs"],
    }
    (root/"result.json").write_text(json.dumps(report,indent=2)+"\n")
    evidence_names=["result.json","range-observations.json","range-observations.csv"]
    if a.experiment=="observation-overhead": evidence_names.append("observation-overhead.json")
    archive=write_evidence_bundle(root,evidence_names)
    print(json.dumps(dict(result="pass",aggregates=aggregates,
        observation_overhead=report.get("observation_overhead_summary"),
        range_observations=range_export["counts"],evidence_bundle=archive.name)),flush=True)

if __name__=="__main__": main()
