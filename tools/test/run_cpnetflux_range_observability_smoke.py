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
    parser.add_argument("--sender-profile", choices=("0","1"), default="0")
    parser.add_argument("--sender-execution", choices=("queued","inline","invalid"), default="queued")
    parser.add_argument("--checkpoint-batch", choices=("1","4","16"), default="1")
    parser.add_argument("--direct-write", choices=("serial","parallel"), default="parallel")
    parser.add_argument("--stage-read", choices=[str(i) for i in range(65)], default="4")
    parser.add_argument("--stage-write", choices=[str(i) for i in range(65)], default="1")
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
        env = dict(os.environ, CPNETFLUX_PAYLOAD_PIPELINE_DEPTH="8", CPNETFLUX_RANGE_TIMING=args.observation,
                   CPNETFLUX_SENDER_PROFILE=args.sender_profile,
                   CPNETFLUX_SENDER_PAYLOAD_EXECUTION=args.sender_execution,
                   CPNETFLUX_RANGE_CHECKPOINT_BATCH=args.checkpoint_batch,
                   CPNETFLUX_RANGE_DIRECT_WRITE=args.direct_write,
                   CPNETFLUX_STAGE_READ_CONCURRENCY=args.stage_read,
                   CPNETFLUX_STAGE_WRITE_CONCURRENCY=args.stage_write)
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
                if args.sender_execution=="invalid":
                    assert direction=="upload" and done.returncode!=0, "invalid sender mode was accepted"
                    assert "CPNETFLUX_SENDER_PAYLOAD_EXECUTION must be queued or inline" in done.stdout+done.stderr
                    assert not (peer/"received/large.bin").exists(), "invalid sender mode published a file"
                    print(json.dumps(dict(result="pass",invalid_sender_execution_rejected=True)))
                    return
                assert done.returncode==0,done.stdout+done.stderr
                if observer:
                    observer.drain()
                    observed_renames=observer.count
                    observer.close()
                assert tree_hash(peer/"received" if direction=="upload" else dest)==expected
                rows[direction]=json.loads(summary.read_text())
            contract=rows["download"].get("range_observation_contract")
            assert contract and contract["schema"]=="cpnetflux_range_observation_v2", \
                "missing versioned range observation contract"
            # Old code silently ignores diagnostic depth and lacks these observations.
            senders=rows["upload"]["file_timings"]
            assert senders and all(f.get("sender_payload_execution")==args.sender_execution for f in senders), \
                "missing sender execution contract"
            assert all(f.get("payload_pipeline_depth")== (0 if args.sender_execution=="inline" else 8) for f in senders)
            receivers=rows["download"]["file_timings"]
            assert all(f["range_id"] for f in receivers), "fixture must exercise striped ranges"
            if args.observation=="on":
                assert sum(f["range_observation"]["data_frame_count"] for f in receivers)>0
                assert sum(f["range_observation"]["effective_data_bytes"] for f in receivers)==134217728
                assert sum(f["range_observation"]["writer_call_count"] for f in receivers)>0
                assert sum(f["range_observation"]["file_write_at_all_calls"] for f in receivers)>0
                assert sum(f["range_observation"]["file_write_at_all_bytes"] for f in receivers)==134217728
                assert sum(f["range_observation"]["offset_write_calls"] for f in receivers)>0
                assert sum(f["range_observation"]["chunk_flush_calls"] for f in receivers)==32
                assert sum(f["range_observation"]["manifest_save"]["rename_calls"] for f in receivers)==observed_renames
            else:
                assert observed_renames>0, observed_renames
            if args.checkpoint_batch!="1":
                assert observed_renames < 32, ("checkpoint still saved per chunk",observed_renames)
            for f in senders+receivers:
                assert f["range_observation_enabled"]==(args.observation=="on")
                if args.observation=="off":
                    assert f["lane_thread_cpu_seconds"]==0
                    assert f["range_observation"]["entry_lock_calls"]==0
                    assert f["range_observation"]["parallel_direct_write_calls"]==0
                    assert f["range_observation"]["reclaim_wait_calls"]==0
                    assert f["range_observation"]["reclaim_wait_seconds"]==0
                    assert f["range_observation"]["manifest_save"]["attempts"]==0
                    assert f["pipeline_producer_wait_seconds"]==0
                    continue
                assert f["lane_thread_cpu_seconds"]>0
                if f["measurement_scope"]=="local_receiver":
                    m=f["range_observation"]
                    if args.direct_write=="parallel":
                        assert m["parallel_direct_write_calls"]==m["data_frame_count"]>0
                        assert m["entry_lock_calls"]>=2*m["writer_call_count"]
                        assert "outside Entry" in contract["metrics"]["parallel_direct_write_calls"]["scope"]
                    else:
                        assert m["parallel_direct_write_calls"]==0
                        assert m["entry_lock_hold_seconds"]>=m["offset_write_seconds"]
                    assert m["entry_lock_hold_seconds"]>=m["checkpoint_seconds"]
                    assert m["manifest_save"]["failures"]==0
                else:
                    assert f["pipeline_high_watermark_bytes"]<=f["pipeline_budget_bytes"]
                    assert (f["producer_thread_cpu_seconds"]==0 if args.sender_execution=="inline" else f["producer_thread_cpu_seconds"]>0)
                    assert f["socket_write_seconds"]>0
            remote=[]
            server_contract=None
            for line in log.read_text().splitlines():
                if line.startswith("CPNETFLUX_PERSISTENT_TIMING "):
                    record=json.loads(line.split(" ",1)[1])
                    server_contract=record.get("range_observation_contract")
                    remote.extend(record["files"])
            assert remote
            for f in senders+receivers+remote:
                is_sender=f['measurement_scope']=='local_sender'
                inline=is_sender and args.sender_execution=='inline'
                assert f['sender_payload_execution']==(args.sender_execution if is_sender else 'not_sender')
                if inline:
                    assert f['producer_thread_cpu_seconds']==0
                    assert f['payload_pipeline_depth']==f['pipeline_budget_bytes']==f['pipeline_high_watermark_bytes']==0
                    assert f['pipeline_producer_waits']==f['pipeline_consumer_waits']==0
                    assert f['pipeline_producer_wait_seconds']==f['pipeline_consumer_wait_seconds']==0
                    assert f['payload_buffer_allocations']==1
                    assert f['payload_buffer_ownership_budget_bytes']==65532
                    assert f['payload_buffer_allocated_capacity_bytes']<=65532
                assert 'sender_cpu_observation' in f, 'missing sender CPU observation contract'
                cpu=f['sender_cpu_observation']
                enabled=(args.sender_profile=='1' and args.observation=='on' and
                         f['measurement_scope']=='local_sender')
                if not enabled:
                    assert cpu is None
                    continue
                assert cpu['schema']=='cpnetflux_sender_cpu_observation_v1'
                assert cpu['clock']=='CLOCK_THREAD_CPUTIME_ID'
                assert cpu['read_checksum_cpu_owner']==('lane' if inline else 'producer')
                assert cpu['read_calls']==cpu['checksum_calls']==cpu['socket_calls']==f['stage_execution']['read_calls']
                assert cpu['read_bytes']==cpu['checksum_bytes']==f['length']
                assert cpu['push_calls']==(0 if inline else cpu['read_calls'])
                assert cpu['push_bytes']==(0 if inline else f['length'])
                assert cpu['pop_calls']==(0 if inline else cpu['socket_calls']+1)
                assert cpu['socket_calls']==f['socket_write_frame_calls']>0
                assert cpu['socket_logical_bytes']==f['length']
                assert cpu['lease_start_calls']==cpu['lease_finish_calls']==cpu['socket_calls']
                producer=sum(cpu[k] for k in ('read_cpu_seconds','checksum_cpu_seconds','push_cpu_seconds'))
                lane=sum(cpu[k] for k in ('pop_cpu_seconds','prepare_cpu_seconds','lease_cpu_seconds','socket_cpu_seconds'))
                if inline:
                    assert cpu['push_cpu_seconds']==cpu['pop_cpu_seconds']==0
                    lane+=producer
                else:
                    assert 0 < producer <= f['producer_thread_cpu_seconds']+0.01
                    assert cpu['push_cpu_seconds']>0 and cpu['pop_cpu_seconds']>0
                assert 0 < lane <= f['lane_thread_cpu_seconds']+0.01
                assert all(cpu[k]>0 for k in ('read_cpu_seconds','checksum_cpu_seconds',
                    'prepare_cpu_seconds','lease_cpu_seconds','socket_cpu_seconds'))
            for f in senders+receivers+remote:
                stage=f["stage_execution"]
                is_receiver = f["measurement_scope"] == "local_receiver"
                assert stage["schema_version"]==1
                assert stage["network_lanes"]==4
                assert stage["read_concurrency"]==int(args.stage_read)
                assert stage["read_admission_required"] == (0 < int(args.stage_read) < 4)
                assert stage["write_admission_required"] == (0 < int(args.stage_write) < 4)
                assert stage["admission_policy"] == ("FIFO_ready_jobs" if is_receiver and 0 < int(args.stage_write) < 4 else "oldest_waiter_with_bounded_ready_bypass")
                assert stage["max_ready_bypasses"] == (0 if is_receiver and 0 < int(args.stage_write) < 4 else 32)
                assert stage["write_executor"] == ("not_receiver" if not is_receiver else "shared_ready_pool" if 0 < int(args.stage_write) < 4 else "per_lane")
                assert stage["write_pool_cpu_seconds"] >= 0
                assert stage["write_concurrency"]==int(args.stage_write)
                assert stage["payload_ownership_bound_bytes"]==4*(8+6)*65536
                assert stage["payload_ownership_bound_bytes"]<=stage["payload_budget_bytes"]
                assert stage["read_wait_seconds"]>=0 and stage["write_wait_seconds"]>=0
                assert stage["read_calls"]>0 if f["measurement_scope"]=="local_sender" else stage["write_calls"]>0
            assert server_contract and server_contract["schema"]=="cpnetflux_range_observation_v2"
            server_receivers=[f for f in remote if f.get("measurement_scope")=="local_receiver"]
            assert server_receivers
            if args.observation=="on":
                assert any(f["range_observation"]["data_frame_count"] for f in server_receivers)
                for f in server_receivers:
                    m=f["range_observation"]
                    assert m["parallel_direct_write_calls"]==(m["data_frame_count"] if args.direct_write=="parallel" else 0)
                    if args.direct_write=="serial":
                        assert m["entry_lock_hold_seconds"]>=m["offset_write_seconds"]
            else:
                assert not any(f["range_observation_enabled"] for f in remote)
                assert all(f["range_observation"]["data_frame_count"]==0 for f in remote)
            print(json.dumps(dict(result="pass",bytes=expected[2] if len(expected)>2 else 134217728,
                sender_ranges=len(senders),receiver_ranges=len(receivers),server_ranges=len(remote))))
        finally:
            stop_server(server, log)

if __name__=="__main__":
    main()
