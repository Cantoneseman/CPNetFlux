#!/usr/bin/env python3
"""One bounded 1/4-lane TCP pwrite diagnostic; stop at the first receive/resource event."""
import argparse
import json
from pathlib import Path
import time

import receive_network as net
import run_large_data_ab as large
import run_small_file_baseline as base
from run_summary_default_gate import verify_chunks
from run_summary_path_ab import ready


def receive_gate(analysis):
    assert analysis['rx_out_of_buffer_delta']==0 and analysis['rx_missed_errors_delta']==0, \
        'receive counter stop gate'


def run(args):
    assert base.node()==1
    args.size,args.count=large.SIZE,1
    assert args.size==base.GIB
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
        idle_analysis=net.analyze(*idle)
        base.save(args.evidence/'idle-analysis.json',idle_analysis)
        receive_gate(idle_analysis)
        for lanes in (1,4):
            name=f'network-lanes-{lanes}'
            source=large.own_peer(args,'residency')
            base.save(args.evidence/(name+'.source.json'),source)
            assert source['all_resident'] and source['host_pswpout_delta_since_seed']==0
            assert source['mem_available_bytes']>=12*base.GIB and source['process']['swap_bytes']==0
            row=base.client_sample(args,name,expected,'2','off',49336,mapped_download=False,
                data_frame_bytes=262144,tcp_receive_buffer_bytes='1048576',connections=lanes,
                receiver_profile=False,range_timing=False,network_profile=True,network_settle_seconds=.5)
            row['lanes_requested']=lanes
            row['source_residency']=source
            row['measurement_scope']='single diagnostic per lane count; no throughput acceptance or causal attribution'
            rows.append(row);base.save(args.evidence/'results.json',rows)
            summary=row['summary']
            assert summary['connections']==lanes and summary['actual_transport']=='v3'
            assert summary['data_transport']=='tcp' and summary['data_frame_logical_bytes']==262144
            endpoints=summary['lane_endpoints']
            assert len(endpoints)==lanes and sum(e['data_connections'] for e in endpoints)==lanes
            assert all(e['data_local_address']=='192.168.100.1' and
                       e['data_peer_address']=='192.168.100.2' and
                       1048576<=e['tcp_receive_buffer_bytes']<=2097152 for e in endpoints)
            row['verified_chunks']=verify_chunks(row['manifest_checks']['member_example'],large.SIZE)
            assert row['verified_chunks']==1024
            window=row['receiver_network_window']
            row['network']=window['settled_analysis']
            # A broad window guards local validation/cleanup, but establishes no transfer causality.
            row['whole_sample_missed_delta']=large.network_counter_deltas(
                row['before']['net'],row['after']['net'])['rx_missed_errors']
            base.save(args.evidence/'results.json',rows)
            print(json.dumps(dict(sample=name,diagnostic_gbps=row['whole_task_gbps'],
                rx_out_of_buffer=row['network']['rx_out_of_buffer_delta'],
                rx_missed_errors=row['network']['rx_missed_errors_delta'],
                queues=row['network']['active_rx_queues'])),flush=True)
            receive_gate(window['analysis'])
            receive_gate(row['network'])
            assert row['whole_sample_missed_delta']==0, 'receive counter whole-sample stop gate'
        assert base.peer(args,'source-check')=={k:expected[k] for k in ('sha256','files','bytes')}
    except BaseException as error:
        base.save(args.evidence/'failure.json',dict(error=repr(error),completed_samples=len(rows)))
        raise
    finally:
        try:
            if started:
                server=base.peer(args,'stop',name='receive-network')
                base.save(args.evidence/'server.json',server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
                assert large.network_counter_deltas(server['before']['net'],server['after']['net'])[
                    'rx_missed_errors']==0, 'receive counter sender stop gate'
        finally:
            if initialized:
                large.own_peer(args,'cleanup');base.peer(args,'finish')
            base.check_root(args.payload_root)
            (args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
            base.save(args.evidence/'cleanup.json',dict(payload_root_removed=not args.payload_root.exists()))
    report=dict(scope='one task per lane count; bounded diagnostic, not performance acceptance',
        samples=len(rows),lanes=[row['lanes_requested'] for row in rows],
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
    args=parser.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
    run(args)
