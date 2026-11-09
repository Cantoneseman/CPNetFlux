#!/usr/bin/env python3
"""S1B: five matched 8GiB reuse pairs before summary-default promotion."""
import argparse
import ctypes
import gzip
import json
import mmap
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess

import run_small_file_baseline as base
from run_summary_path_ab import ready

SIZE=8*base.GIB


def residency(path):
    size=path.stat().st_size;pages=(size+mmap.PAGESIZE-1)//mmap.PAGESIZE
    assert size>0
    vector=(ctypes.c_ubyte*pages)()
    libc=ctypes.CDLL(None,use_errno=True)
    libc.mincore.argtypes=(ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p)
    libc.mincore.restype=ctypes.c_int
    with path.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_COPY) as mapping:
        address=ctypes.addressof(ctypes.c_char.from_buffer(mapping))
        if libc.mincore(address,size,vector):raise OSError(ctypes.get_errno(),'mincore')
    resident=sum(bool(value&1) for value in vector)
    return {'bytes':size,'page_bytes':mmap.PAGESIZE,'pages':pages,
            'resident_pages':resident,'all_resident':resident==pages}


def seed(args):
    base.receipt(args.commit);assert base.node()==2
    assert shutil.disk_usage('/dev/shm').free>9*base.GIB
    source=base.owned(args.payload_root,'source');source.mkdir()
    path=source/'f000000.bin';block=os.urandom(8*base.MiB)
    state=base.resource_state()
    try:
        with path.open('xb') as f:
            for _ in range(1024):
                f.write(block)
                state['mem_available_min_bytes']=min(state['mem_available_min_bytes'],base.mem())
                state['owned_swap_peak_bytes']=max(state['owned_swap_peak_bytes'],base.proc_info(os.getpid())['swap_bytes'])
                assert state['mem_available_min_bytes']>=12*base.GIB,'seed memory threshold'
                assert base.pswpout()==state['pswpout_before'],'seed host swap-out changed'
                assert state['owned_swap_peak_bytes']==0,'seed process swap'
    finally:
        state['pswpout_delta']=base.pswpout()-state['pswpout_before']
        state['materialized_bytes']=path.stat().st_size
        base.save(args.evidence/'peer/seed-resource.json',state)
    result=base.tree(source);result['residency']=residency(path)
    result['data_shape']='one random 8MiB block repeated 1024 times; fully materialized; compression off'
    assert result['bytes']==SIZE and result['files']==1 and result['residency']['all_resident']
    base.save(args.evidence/'peer/source.json',result)
    return result


def own_peer(args,action):
    argv=['python3',str(Path(__file__).resolve()),'--action',action,'--evidence',str(args.evidence),
          '--payload-root',str(args.payload_root),'--commit',args.commit]
    r=subprocess.run(['ssh','-n','-o','BatchMode=yes','r770-2',shlex.join(argv)],
                     text=True,capture_output=True,timeout=180)
    assert r.returncode==0,r.stderr+r.stdout
    return json.loads(r.stdout)


def verify_chunks(text,size=SIZE):
    fields=dict(line.split('=',1) for line in text.splitlines() if '=' in line)
    assert fields['state']=='committed' and int(fields['chunk_size'])==base.MiB
    assert fields['completed_ranges']==f'0-{size}'
    chunks=[value.split(':') for value in fields['verified_chunks'].split(',')]
    assert len(chunks)==size//base.MiB
    for index,c in enumerate(chunks):
        assert len(c)==5 and int(c[0])>=0
        assert int(c[1])==index*base.MiB and int(c[2])==base.MiB
        assert c[3]=='crc32c' and re.fullmatch('[0-9a-fA-F]{8}',c[4])
    return len(chunks)


def verify_ranges(rows):
    assert len(rows)==8,'requires eight 1GiB ranges'
    for index,r in enumerate(sorted(rows,key=lambda r:r['offset'])):
        assert r['offset']==index*base.GIB and r['length']==base.GIB
        assert r['receiver_payload_execution']=='inline' and r['sender_payload_execution']=='not_sender'
        assert r['receiver_write_backend']=='mmap_experimental'
        assert not r['range_observation_enabled'] and r['lane_thread_cpu_seconds']==0
        assert r['sender_cpu_observation'] is None and r['receiver_cpu_observation'] is None
        m=r['mapped_write']
        assert m['window_bound_bytes']==64*base.MiB and m['concurrent_window_bound_bytes']==256*base.MiB
        assert m['map_calls']==m['unmap_calls']==16 and m['map_failures']==m['active_mapped_bytes']==0
        assert m['written_bytes']==base.GIB and m['write_calls']==17408
        assert 0<m['peak_mapped_bytes']<=64*base.MiB and m['profile'] is None
    return {'ranges':8,'bytes':SIZE,'window_per_lane_bytes':64*base.MiB,'concurrent_bound_bytes':256*base.MiB}


def evaluate(rows):
    assert len(rows)==10,'requires five complete pairs'
    groups={mode:sorted([r for r in rows if r['summary_path_request']==mode],key=lambda r:r['rep'])
            for mode in ('original','cached')}
    for mode,rs in groups.items():
        assert len(rs)==5 and {r['rep'] for r in rs}==set(range(1,6)),'requires five complete pairs'
        for r in rs:
            assert r['t_ready_seconds']>0 and r['result']=='pass' and r['output_slot_removed']
            s=r['summary']
            assert s['summary_tree_hash_source']=='verified_chunks'
            assert s['summary_path_requested']==mode and s['summary_path_effective']=='not_used'
    assert len({r['summary']['tree_hash'] for r in rows})==1,'tree hash changed'
    result={'scope':'matched 8GiB full spawn-to-exit; p95 descriptive, five pairs','arms':{}}
    for mode,rs in groups.items():
        times=[r['t_ready_seconds'] for r in rs]
        median=statistics.median(times)
        result['arms'][mode]={'values':times,'median_seconds':median,'median_gbps':SIZE*8/median/1e9,
            'p95_descriptive_seconds':base.percentile(times,.95),
            'cv_population':statistics.pstdev(times)/statistics.mean(times)}
    a,b=result['arms']['original'],result['arms']['cached']
    result['median_wall_regression']=b['median_seconds']/a['median_seconds']-1
    result['gate_pass']=result['median_wall_regression']<=.05 and max(a['cv_population'],b['cv_population'])<=.05
    result['gate']='cached median wall regression<=5%; each arm CV<=5%; no sample removal or retry'
    return result


def run_case(args,rows,port=49323):
    started=False
    try:
        expected=own_peer(args,'seed')
        base.peer(args,'start',name='large-reuse',port=port,data_base=61000);started=True;ready(port)
        for rep in range(1,6):
            for mode in (('original','cached') if rep%2 else ('cached','original')):
                resident=own_peer(args,'residency');assert resident['all_resident']
                name=f'8g-{mode}-r{rep}'
                row=base.client_sample(args,name,expected,'2','on',port,summary_path=mode,mapped_download=True)
                row['rep']=rep;row['source_residency_before']=resident
                row['verified_chunk_count']=verify_chunks(row['manifest_checks']['member_example'])
                with gzip.open(args.evidence/'samples'/(name+'.summary.json.gz'),'rt') as f:
                    row['mapped_checks']=verify_ranges(json.load(f)['file_timings'])
                rows.append(row);base.save(args.evidence/'samples'/(name+'.receipt.json'),row)
                base.save(args.evidence/'results.json',rows)
                print(json.dumps({'sample':name,'t_ready':row['t_ready_seconds'],'gbps':row['whole_task_gbps']}),flush=True)
        checked=base.peer(args,'source-check')
        assert checked=={k:expected[k] for k in ('sha256','files','bytes')}
        base.save(args.evidence/'source-final-check.json',checked)
    finally:
        try:
            if started:
                server=base.peer(args,'stop',name='large-reuse');base.save(args.evidence/'large-reuse.server.json',server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
                assert server['sender_phases']['count']==80
                assert all(r['sender_payload_execution']=='queued' for r in server['sender_phases']['examples'])
        finally:base.peer(args,'source-cleanup')


def matrix(args):
    assert base.node()==1;args.size,args.count=SIZE,1
    local=base.receipt(args.commit);remote=base.peer(args,'receipt');assert local==remote
    base.save(args.evidence/'matching-builds.json',{'R770-1':local,'R770-2':remote})
    assert not args.payload_root.exists()
    args.payload_root.mkdir();(args.payload_root/base.MARKER).write_text(base.TOKEN)
    base.peer(args,'init');rows=[]
    try:run_case(args,rows)
    finally:
        base.peer(args,'finish')
        base.check_root(args.payload_root);(args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
    report=evaluate(rows);base.save(args.evidence/'analysis.json',report)
    print(json.dumps({'execution':'pass','samples':len(rows),'analysis':report,
                      'payload_root_removed':not args.payload_root.exists()}),flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--action',required=True,choices=['seed','residency','matrix'])
    p.add_argument('--evidence',required=True,type=Path);p.add_argument('--payload-root',required=True,type=Path)
    p.add_argument('--commit',required=True)
    args=p.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
    if args.action=='matrix':matrix(args)
    else:
        base.receipt(args.commit)
        result=seed(args) if args.action=='seed' else residency(base.owned(args.payload_root,'source')/'f000000.bin')
        print(json.dumps(result))


if __name__=='__main__':main()
