#!/usr/bin/env python3
"""One bounded receive-queue investigation, at most three same-configuration 1GiB full tasks."""
import argparse
import json
from pathlib import Path
import statistics
import time

import receive_network as net
import run_large_data_ab as large
import run_small_file_baseline as base
from run_summary_default_gate import verify_chunks
from run_summary_path_ab import ready


def run(args):
    assert base.node()==1 and 1<=args.samples<=3
    args.size,args.count=large.SIZE,1
    local=base.receipt(args.commit);remote=base.peer(args,'receipt');assert local==remote
    base.save(args.evidence/'matching-builds.json',dict(local=local,remote=remote))
    base.save(args.evidence/'inventory-r1.json',net.inventory())
    assert not args.payload_root.exists()
    args.payload_root.mkdir();(args.payload_root/base.MARKER).write_text(base.TOKEN)
    initialized=False;started=False;rows=[]
    try:
        base.peer(args,'init');initialized=True
        expected=large.own_peer(args,'seed')
        base.peer(args,'start',name='receive-network',port=49336,data_base=61000);started=True;ready(49336)
        idle=[net.snapshot()];time.sleep(.5);idle.append(net.snapshot())
        base.save(args.evidence/'idle.json',idle)
        base.save(args.evidence/'idle-analysis.json',net.analyze(*idle))
        for index in range(1,args.samples+1):
            name=f'network-r{index}'
            source=large.own_peer(args,'residency')
            base.save(args.evidence/(name+'.source.json'),source)
            assert source['all_resident'] and source['host_pswpout_delta_since_seed']==0
            assert source['mem_available_bytes']>=12*base.GIB and source['process']['swap_bytes']==0
            evidence=dict(before=net.snapshot(),samples=[])
            def observer(process,state):
                previous=evidence['samples'][-1]['ended_monotonic'] if evidence['samples'] else 0
                if time.monotonic()-previous>=.05 and len(evidence['samples'])<64:
                    evidence['samples'].append(net.snapshot(process.pid,full=False))
            def runner(command,log,**options):
                return base.run_guarded(command,log,process_observer=observer,**options)
            try:
                row=base.client_sample(args,name,expected,'2','on',49336,mapped_download=True,
                    data_frame_bytes=262144,tcp_receive_buffer_bytes='1048576',runner=runner)
                evidence['after_client_and_sha']=net.snapshot()
                # Device counters are refreshed asynchronously. Keep this window distinct.
                time.sleep(.5);evidence['after_settle']=net.snapshot()
                row['verified_chunks']=verify_chunks(row['manifest_checks']['member_example'],large.SIZE)
                row['mechanism']=large.mapped_checks(args,name,262144)
                row['network']=net.analyze(evidence['before'],evidence['after_settle'])
                row['measurement_scope']='instrumented diagnostic only; not comparable to unobserved throughput'
                row['observation_calls']=len(evidence['samples'])
                rows.append(row);base.save(args.evidence/'results.json',rows)
                print(json.dumps(dict(sample=name,diagnostic_gbps=row['whole_task_gbps'],
                    rx_out_of_buffer=row['network']['rx_out_of_buffer_delta'],
                    rx_missed_errors=row['network']['rx_missed_errors_delta'],
                    queues=row['network']['active_rx_queues'])),flush=True)
            finally:base.save(args.evidence/(name+'.network.json'),evidence)
        assert base.peer(args,'source-check')=={k:expected[k] for k in ('sha256','files','bytes')}
    except BaseException as error:
        base.save(args.evidence/'failure.json',dict(error=repr(error),completed_samples=len(rows)));raise
    finally:
        try:
            if started:
                server=base.peer(args,'stop',name='receive-network');base.save(args.evidence/'server.json',server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
        finally:
            if initialized:
                large.own_peer(args,'cleanup');base.peer(args,'finish')
            base.check_root(args.payload_root);(args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
    report=dict(scope='bounded network diagnostic, not performance acceptance',samples=len(rows),
        reproduced=sum(r['network']['rx_out_of_buffer_delta']>0 for r in rows),
        missed_delta=sum(r['network']['rx_missed_errors_delta'] for r in rows),
        out_of_buffer_delta=sum(r['network']['rx_out_of_buffer_delta'] for r in rows),
        flow_cause_proven=False,m1_pass=False,payload_root_removed=not args.payload_root.exists())
    base.save(args.evidence/'analysis.json',report);print(json.dumps(report),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence',required=True,type=Path)
    parser.add_argument('--payload-root',required=True,type=Path)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--samples',type=int,choices=(1,2,3),default=3)
    args=parser.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
    run(args)
