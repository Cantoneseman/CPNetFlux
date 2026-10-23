#!/usr/bin/env python3
"""Exercise V3 striped upload/download and inspect both endpoint observations."""
import argparse
import json
import os
import subprocess
import tempfile
from pathlib import Path
from gridftp_port_window import clamp_passive_data_port_base
from run_cpnetflux_tree_manifest_coalesce_smoke import RenameObserver
from tree_smoke_common import free_port, stop_server, tree_hash, wait_for_control

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--observation", choices=("on","off"), default="on")
    args = parser.parse_args()
    build = args.build_dir.absolute()
    with tempfile.TemporaryDirectory(prefix="cpnetflux-range-observation-") as tmp:
        root = Path(tmp)
        source, peer = root/"source", root/"peer"
        source.mkdir(); peer.mkdir()
        block = bytes(i % 251 for i in range(65536))
        with (source/"large.bin").open("wb") as out:
            for _ in range(2048): out.write(block)
        expected = tree_hash(source)
        port, data = free_port(), clamp_passive_data_port_base(free_port())
        env = dict(os.environ, CPNETFLUX_PAYLOAD_PIPELINE_DEPTH="8", CPNETFLUX_RANGE_TIMING=args.observation)
        log = root/"server.log"
        with log.open("w") as output:
            server = subprocess.Popen([str(build/"cpnetflux-gridftp-server"),
                "--host","127.0.0.1","--port",str(port),"--root",str(peer),
                "--data-port-base",str(data),"--connections","4","--link-set-max-links","4",
                "--checksum","crc32c","--chunk-size","4194304"], stdout=output, stderr=subprocess.STDOUT, env=env)
        try:
            wait_for_control(port)
            rows = {}
            for direction in ("upload","download"):
                dest = root/"received"
                if direction=="download": dest.mkdir()
                observer = RenameObserver(dest, "large.bin.cpnetflux.download.manifest") if direction=="download" else None
                summary = root/(direction+".json")
                cmd=[str(build/("cpnetflux-tree-"+direction+"-client")),
                    "--host","127.0.0.1","--port",str(port),
                    "--source-dir",str(source) if direction=="upload" else "received",
                    "--dest-dir","received" if direction=="upload" else str(dest),
                    "--checksum","crc32c","--checksum-backend","auto",
                    "--file-parallelism","4","--connections","4","--link-set-links","4",
                    "--chunk-size","4194304","--phase-timing","on",
                    "--json-summary",str(summary)]
                done=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=60)
                assert done.returncode==0,done.stdout+done.stderr
                if observer:
                    observer.drain()
                    observed_renames=observer.count
                    observer.close()
                assert tree_hash(peer/"received" if direction=="upload" else dest)==expected
                rows[direction]=json.loads(summary.read_text())
            # Old code silently ignores diagnostic depth and lacks these observations.
            senders=rows["upload"]["file_timings"]
            assert senders and all(f.get("payload_pipeline_depth")==8 for f in senders), \
                "missing per-range pipeline depth observation (expected 8)"
            receivers=rows["download"]["file_timings"]
            assert all(f["range_id"] for f in receivers), "fixture must exercise striped ranges"
            if args.observation=="on":
                assert sum(f["range_observation"]["offset_write_calls"] for f in receivers)>0
                assert sum(f["range_observation"]["chunk_flush_calls"] for f in receivers)==32
                assert sum(f["range_observation"]["manifest_save"]["rename_calls"] for f in receivers)==observed_renames
            else:
                assert observed_renames>=35, observed_renames
            for f in senders+receivers:
                assert f["range_observation_enabled"]==(args.observation=="on")
                if args.observation=="off":
                    assert f["lane_thread_cpu_seconds"]==0
                    assert f["range_observation"]["entry_lock_calls"]==0
                    assert f["range_observation"]["manifest_save"]["attempts"]==0
                    assert f["pipeline_producer_wait_seconds"]==0
                    continue
                assert f["lane_thread_cpu_seconds"]>0
                if f["measurement_scope"]=="local_receiver":
                    m=f["range_observation"]
                    assert m["entry_lock_hold_seconds"]>=m["offset_write_seconds"]
                    assert m["entry_lock_hold_seconds"]>=m["checkpoint_seconds"]
                    assert m["manifest_save"]["failures"]==0
                else:
                    assert f["pipeline_high_watermark_bytes"]<=f["pipeline_budget_bytes"]
                    assert f["producer_thread_cpu_seconds"]>0
                    assert f["socket_write_seconds"]>0
            remote=[]
            for line in log.read_text().splitlines():
                if line.startswith("CPNETFLUX_PERSISTENT_TIMING "):
                    remote.extend(json.loads(line.split(" ",1)[1])["files"])
            assert remote
            if args.observation=="on":
                assert any(f["range_observation"]["offset_write_calls"] for f in remote)
            else:
                assert not any(f["range_observation_enabled"] for f in remote)
            print(json.dumps(dict(result="pass",bytes=expected[2] if len(expected)>2 else 134217728,
                sender_ranges=len(senders),receiver_ranges=len(receivers),server_ranges=len(remote))))
        finally:
            stop_server(server, log)

if __name__=="__main__":
    main()
