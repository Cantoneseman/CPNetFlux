#!/usr/bin/env python3
"""Milestone35: bounded 1GiB real-file DATA A/B; never the formal 8GiB M1 gate."""
import argparse
import gzip
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import subprocess

import run_small_file_baseline as base
from run_summary_default_gate import residency, verify_chunks
from run_summary_path_ab import ready

SIZE = base.GIB


def cgroup_owner():
    """Called in the actual writer/SHA process, not a separate SSH probe."""
    membership = Path('/proc/self/cgroup').read_text()
    entry = next(line[3:] for line in membership.splitlines() if line.startswith('0::'))
    current = Path('/sys/fs/cgroup') / entry.lstrip('/')
    groups = []
    while True:
        fields = {}
        for name in ('memory.current','memory.max','memory.high','memory.swap.current',
                     'memory.swap.max','memory.events','memory.stat','memory.pressure'):
            path = current / name
            if path.exists(): fields[name] = path.read_text().strip()
        groups.append(dict(path=str(current), fields=fields))
        if current == Path('/sys/fs/cgroup'): break
        assert Path('/sys/fs/cgroup') in current.parents
        current = current.parent
    return dict(process=base.proc_info(os.getpid()), membership=membership, ancestors=groups)


def source_probe(args):
    state = json.loads((args.evidence/'peer/seed-resource.json').read_text())
    result = residency(base.owned(args.payload_root,'source')/'f000000.bin')
    result.update(host_pswpout_delta_since_seed=base.pswpout()-state['pswpout_before'],
                  mem_available_bytes=base.mem(), process=base.proc_info(os.getpid()))
    return result


def seed(args):
    base.receipt(args.commit)
    assert base.node()==2 and shutil.disk_usage('/dev/shm').free>2*SIZE
    source=base.owned(args.payload_root,'source');source.mkdir()
    path=source/'f000000.bin';state=base.resource_state();state['owner_before']=cgroup_owner()
    def guard(phase):
        state['phase']=phase;state['samples']+=1
        state['mem_available_min_bytes']=min(state['mem_available_min_bytes'],base.mem())
        state['owned_swap_peak_bytes']=max(state['owned_swap_peak_bytes'],base.proc_info(os.getpid())['swap_bytes'])
        assert state['mem_available_min_bytes']>=12*base.GIB,'seed memory threshold'
        assert state['owned_swap_peak_bytes']==0 and base.pswpout()==state['pswpout_before'],'seed swap gate'
    try:
        block=os.urandom(8*base.MiB)
        with path.open('xb') as handle:
            for _ in range(SIZE//len(block)):handle.write(block);guard('write')
        state['owner_after_write']=cgroup_owner()
        guard('sha');expected=base.tree(source);guard('sha_complete')
        state['owner_after_sha']=cgroup_owner()
        expected['residency']=residency(path);guard('residency')
        assert expected['residency']['all_resident'] and expected['bytes']==SIZE
        expected['data_shape']='random 8MiB block repeated 128 times; fully materialized'
        base.save(args.evidence/'peer/source.json',expected)
        return expected
    finally:
        state['owner_final']=cgroup_owner();state['pswpout_delta']=base.pswpout()-state['pswpout_before']
        state['materialized_bytes']=path.stat().st_size
        base.save(args.evidence/'peer/seed-resource.json',state)


def own_peer(args,action):
    argv=['python3',str(Path(__file__).resolve()),'--action',action,'--evidence',str(args.evidence),
          '--payload-root',str(args.payload_root),'--commit',args.commit]
    result=subprocess.run(['ssh','-n','-o','BatchMode=yes','r770-2',shlex.join(argv)],
                          text=True,capture_output=True,timeout=180)
    assert result.returncode==0,result.stderr+result.stdout
    return json.loads(result.stdout)


def mapped_checks(args,name,size,receiver_execution):
    with gzip.open(args.evidence/'samples'/(name+'.summary.json.gz'),'rt') as f:data=json.load(f)
    rows=data['file_timings'];assert len(rows)==8
    frames=0
    for index,row in enumerate(sorted(rows,key=lambda r:r['offset'])):
        assert row['offset']==index*SIZE//8 and row['length']==SIZE//8
        assert row['receiver_payload_execution']==receiver_execution and not row['range_observation_enabled']
        assert row['receiver_write_backend']=='mmap_experimental'
        if receiver_execution == 'queued':
            assert row['payload_pipeline_depth']==2
            assert row['pipeline_budget_bytes']==262148*2
            assert 0 < row['pipeline_high_watermark_bytes'] <= row['pipeline_budget_bytes']
        else:
            assert row['payload_pipeline_depth']==0 and row['pipeline_budget_bytes']==0
        mapped=row['mapped_write']
        expected=128*(17 if size==65532 else 4)
        assert mapped['write_calls']==expected and mapped['written_bytes']==SIZE//8
        assert mapped['map_calls']==mapped['unmap_calls']==2 and mapped['active_mapped_bytes']==0
        assert mapped['window_bound_bytes']==64*base.MiB and mapped['concurrent_window_bound_bytes']==256*base.MiB
        frames+=mapped['write_calls']
    assert data['data_frame_logical_bytes']==size
    return dict(ranges=8,actual_data_frames=frames,mapped_window_bytes=64*base.MiB)


def evaluate(rows):
    result={'scope':'1GiB complete real-file task; not 8GiB M1', 'arms':{}, 'm1_pass':False}
    arms=list(dict.fromkeys(r['arm'] for r in rows))
    assert len(arms)==2
    for label in arms:
        arm=[r for r in rows if r['arm']==label]
        times=[r['t_ready_seconds'] for r in arm];median=statistics.median(times)
        result['arms'][label]=dict(seconds=times,median_seconds=median,
            median_gbps=SIZE*8/median/1e9,cv=statistics.pstdev(times)/statistics.mean(times),
            sampled_receiver_cpu_seconds=[r['resource']['cpu_seconds_sampled_lower_bound'] for r in arm],
            rss_peak_bytes=max(r['resource']['rss_peak_bytes'] for r in arm))
    result['paired_improvements']=[1-next(r for r in rows if r['rep']==rep and r['arm']==arms[1])['t_ready_seconds']/
        next(r for r in rows if r['rep']==rep and r['arm']==arms[0])['t_ready_seconds'] for rep in range(1,6)]
    result['median_task_rate_change']=result['arms'][arms[1]]['median_gbps']/result['arms'][arms[0]]['median_gbps']-1
    result['improved_pairs']=sum(x>0 for x in result['paired_improvements'])
    return result


def matrix(args):
    assert base.node()==1
    args.size,args.count=SIZE,1
    local=base.receipt(args.commit);peer=base.peer(args,'receipt');assert local==peer
    base.save(args.evidence/'matching-builds.json',dict(local=local,peer=peer))
    assert not args.payload_root.exists()
    args.payload_root.mkdir();(args.payload_root/base.MARKER).write_text(base.TOKEN)
    peer_initialized=False;started=False;rows=[];expected=None
    try:
        base.peer(args,'init');peer_initialized=True
        expected=own_peer(args,'seed')
        base.peer(args,'start',name='large-data',port=49335,data_base=61000);started=True;ready(49335)
        for rep in range(1,6):
            if args.mechanism=='frame':
                labels=('65532','262144')
            elif args.mechanism=='receive-buffer':
                labels=('off','1048576')
            else:
                labels=('inline','queued')
            for label in (labels if rep%2 else labels[::-1]):
                size=int(label) if args.mechanism=='frame' else 262144
                receive_buffer=label if args.mechanism=='receive-buffer' else 'off'
                receiver_execution=label if args.mechanism=='receiver-pipeline' else 'inline'
                name=f'1g-{args.mechanism}-{label}-r{rep}'
                resident=own_peer(args,'residency');base.save(args.evidence/'samples'/(name+'.source.json'),resident)
                assert resident['all_resident'] and resident['host_pswpout_delta_since_seed']==0,'source residency/swap gate'
                assert resident['mem_available_bytes']>=12*base.GIB and resident['process']['swap_bytes']==0
                row=base.client_sample(args,name,expected,'2','on',49335,mapped_download=True,data_frame_bytes=size,
                    tcp_receive_buffer_bytes=receive_buffer,receiver_payload_execution=receiver_execution)
                row.update(rep=rep,frame_bytes=size,arm=label,receive_buffer=receive_buffer,
                    receiver_payload_execution=receiver_execution,source_residency=resident)
                row['verified_chunks']=verify_chunks(row['manifest_checks']['member_example'],SIZE)
                row['mechanism']=mapped_checks(args,name,size,receiver_execution)
                if receive_buffer!='off':
                    assert all(1048576<=lane['tcp_receive_buffer_bytes']<=2097152
                               for lane in row['summary']['lane_endpoints'])
                rows.append(row);base.save(args.evidence/'results.json',rows)
                print(json.dumps(dict(sample=name,gbps=row['whole_task_gbps'],frames=row['mechanism']['actual_data_frames'])),flush=True)
        assert base.peer(args,'source-check')=={k:expected[k] for k in ('sha256','files','bytes')}
    except BaseException as error:
        base.save(args.evidence/'failure.json',dict(error=repr(error),completed_samples=len(rows)))
        raise
    finally:
        try:
            if started:
                server=base.peer(args,'stop',name='large-data');base.save(args.evidence/'server.json',server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
        finally:
            if peer_initialized:
                own_peer(args,'cleanup')
                base.peer(args,'finish')
            base.check_root(args.payload_root)
            (args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
    report=evaluate(rows);base.save(args.evidence/'analysis.json',report)
    print(json.dumps(report),flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--action',required=True,choices=('matrix','seed','residency','cleanup'))
    parser.add_argument('--evidence',required=True,type=Path)
    parser.add_argument('--payload-root',required=True,type=Path)
    parser.add_argument('--commit',required=True)
    parser.add_argument('--mechanism',choices=('frame','receive-buffer','receiver-pipeline'),default='frame')
    args=parser.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
    if args.action=='matrix':matrix(args)
    elif args.action=='cleanup':
        source=base.owned(args.payload_root,'source')
        if source.exists():base.remove_owned(args.payload_root,source)
        print(json.dumps(dict(source_removed=not source.exists())))
    else:print(json.dumps(seed(args) if args.action=='seed' else source_probe(args)))
