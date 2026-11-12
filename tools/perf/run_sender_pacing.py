#!/usr/bin/env python3
"""One 1GiB M1-combined plus paced-sender feasibility task; never a performance baseline."""
import argparse
import json
from pathlib import Path
import shutil
import time

import run_large_data_ab as large
import run_small_file_baseline as base
import receive_network as net
from run_receive_network import receive_gate
from run_summary_default_gate import verify_chunks
from run_summary_path_ab import ready

RATE='1500000000'

def validate_receipts(rows, size=base.GIB):
    assert len(rows)==4 and {r['lane'] for r in rows}==set(range(4)), 'pacing lane receipts'
    assert sum(r['frames'] for r in rows)==size//262144, 'paced frame coverage'
    assert sum(r['wire_bytes'] for r in rows)==size+(size//262144)*68, 'paced byte coverage'
    for row in rows:
        assert row['wait_backend']=='timerfd' and row['timer_fd_creates']==1, 'bounded timer waiter not used'
        assert row['timer_arm_calls']==row['sleep_calls']>0, 'timer arm coverage'
        assert row['timer_read_calls']>=row['timer_arm_calls'], 'timer read coverage'
        assert row['rate_bytes_per_second']==int(RATE) and row['spacing_violations']==0
        assert row['frames']>1 and row['waits']>0 and row['wait_ns']>0, 'pacer did not delay admission'
        assert row['last_admission_ns']-row['first_admission_ns']>=row['required_spacing_ns'], 'admission rate exceeded'
        assert row['wire_bytes']==row['frames']*262212
        assert row['required_spacing_ns']==(row['frames']-1)*174808, 'incomplete spacing accounting'
    return dict(scope='application DATA admission; not wire packet pacing',
                receipts=rows,frames=sum(r['frames'] for r in rows),wire_bytes=sum(r['wire_bytes'] for r in rows),
                wait_ns=sum(r['wait_ns'] for r in rows),passed=True)

def run(args):
    assert base.node()==1
    args.size_gib=1;args.size,args.count=base.GIB,1
    assert base.mem()>=12*base.GIB and shutil.disk_usage('/dev/shm').free>2*base.GIB
    start=base.snapshot();base.save(args.evidence/'preflight-r1.json',start)
    local=base.receipt(args.commit);remote=base.peer(args,'receipt');assert local==remote
    base.save(args.evidence/'matching-builds.json',dict(local=local,remote=remote))
    base.save(args.evidence/'inventory-r1.json',net.inventory())
    assert not args.payload_root.exists()
    args.payload_root.mkdir();(args.payload_root/base.MARKER).write_text(base.TOKEN)
    initialized=False;started=False;row=None;cleanup={};server=None
    try:
        peer=base.peer(args,'init');initialized=True;base.save(args.evidence/'preflight-r2.json',peer)
        expected=large.own_peer(args,'seed')
        base.peer(args,'start',name='sender-pacing',port=49337,data_base=61000,sender_pacing=RATE)
        started=True;ready(49337)
        idle=[net.snapshot()];time.sleep(.5);idle.append(net.snapshot())
        base.save(args.evidence/'idle.json',idle);receive_gate(net.analyze(*idle))
        source=large.own_peer(args,'residency');base.save(args.evidence/'source-residency.json',source)
        assert source['all_resident'] and source['host_pswpout_delta_since_seed']==0
        assert source['mem_available_bytes']>=12*base.GIB and source['process']['swap_bytes']==0
        row=base.client_sample(args,'paced-combined-1g',expected,'2','on',49337,mapped_download=True,
            data_frame_bytes=262144,tcp_receive_buffer_bytes='1048576',receiver_profile=False,
            range_timing=False,network_profile=True,network_settle_seconds=.5)
        base.save(args.evidence/'results.json',[row])
        summary=row['summary']
        assert summary['connections']==4 and summary['actual_transport']=='v3' and summary['data_transport']=='tcp'
        assert summary['data_frame_logical_bytes']==262144 and len(summary['lane_endpoints'])==4
        assert not summary['control_tls_verified'] and not summary['data_tls_verified']
        assert all(e['data_connections']==1 and e['data_local_address']=='192.168.100.1'
                   and e['data_peer_address']=='192.168.100.2'
                   and 1048576<=e['tcp_receive_buffer_bytes']<=2097152 for e in summary['lane_endpoints'])
        row['verified_chunks']=verify_chunks(row['manifest_checks']['member_example'],base.GIB)
        assert row['verified_chunks']==1024
        row['mapped_mechanism']=large.mapped_checks(args,'paced-combined-1g',262144,receiver_profile=False,range_timing=False)
        base.save(args.evidence/'results.json',[row])
        window=row['receiver_network_window']
        receive_gate(window['analysis']);receive_gate(window['settled_analysis'])
        assert large.network_counter_deltas(row['before']['net'],row['after']['net'])['rx_missed_errors']==0
        assert base.peer(args,'source-check')=={k:expected[k] for k in ('sha256','files','bytes')}
    except BaseException as error:
        base.save(args.evidence/'failure.json',dict(error=repr(error),completed_samples=int(row is not None)))
        raise
    finally:
        try:
            if started:
                server=base.peer(args,'stop',name='sender-pacing');base.save(args.evidence/'server.json',server)
                receipts=server['sender_phases'].get('sender_pacing',[])
                base.save(args.evidence/'pacing-receipts.json',receipts)
                # Preserve mechanism evidence even when the network stop gate rejected the task.
                if row is not None:
                    base.save(args.evidence/'pacing-mechanism.json',validate_receipts(receipts))
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
                assert large.network_counter_deltas(server['before']['net'],server['after']['net'])['rx_missed_errors']==0
        finally:
            if initialized:
                cleanup['peer_source']=large.own_peer(args,'cleanup')
                cleanup['peer_root']=base.peer(args,'finish')
            base.check_root(args.payload_root);(args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
            cleanup['local_root_removed']=not args.payload_root.exists()
            base.save(args.evidence/'cleanup.json',cleanup)
    assert row is not None
    report=dict(scope='one candidate feasibility task, no same-commit performance comparison',
                task_gbps=row['whole_task_gbps'],t_ready_seconds=row['t_ready_seconds'],
                mechanism_pass=True,network_pass=True,cleanup=cleanup,m1_pass=False,m2_pass=False,
                formal_admission_rate_met=row['whole_task_gbps']>=40,
                interpretation='single no-event task is not stability or throughput improvement proof')
    base.save(args.evidence/'analysis.json',report);print(json.dumps(report),flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--payload-root',type=Path,required=True)
    p.add_argument('--commit',required=True)
    args=p.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
    run(args)
