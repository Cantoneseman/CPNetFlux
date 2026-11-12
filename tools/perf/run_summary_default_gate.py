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


def cgroup_snapshot(membership, cgroup_root=Path('/sys/fs/cgroup')):
    entry=next((line[3:] for line in membership.splitlines() if line.startswith('0::')),None)
    if entry is None:raise RuntimeError('unified cgroup membership is unavailable')
    root=cgroup_root.resolve();leaf=(root/entry.lstrip('/')).resolve()
    if leaf!=root and root not in leaf.parents:raise RuntimeError('cgroup path escapes its mount')
    groups=[];current=leaf
    while True:
        fields={}
        for name in ('memory.current','memory.max','memory.high','memory.swap.current',
                     'memory.swap.max','memory.events','memory.stat','memory.pressure'):
            path=current/name
            if not path.is_file():continue
            text=path.read_text().strip()
            if name in ('memory.events','memory.stat'):
                values={}
                for line in text.splitlines():
                    parts=line.split()
                    if len(parts)==2:
                        try:values[parts[0]]=int(parts[1])
                        except ValueError:values[parts[0]]=parts[1]
                fields[name]=values
            else:fields[name]=text
        groups.append({'path':str(current),'fields':fields})
        if current==root:break
        current=current.parent
    return {'membership':membership,'leaf':entry,'leaf_present':leaf.is_dir(),'ancestors':groups}


def cgroup_owner():
    pid=os.getpid()
    membership=(Path('/proc')/str(pid)/'cgroup').read_text()
    return {'process':base.proc_info(pid),**cgroup_snapshot(membership)}

def seed(args):
    base.receipt(args.commit);assert base.node()==2
    assert shutil.disk_usage('/dev/shm').free>9*base.GIB
    source=base.owned(args.payload_root,'source');path=source/'f000000.bin'
    state=base.resource_state();state['cgroup_snapshots']=[]
    state['phase']='source_setup'
    result=None
    def capture_owner(phase):
        owner=cgroup_owner()
        state['cgroup_snapshots'].append({'phase':phase,'owner':owner})
        state['owned_swap_peak_bytes']=max(state['owned_swap_peak_bytes'],owner['process']['swap_bytes'])
        return owner
    def guard():
        state['samples']+=1
        state['mem_available_min_bytes']=min(state['mem_available_min_bytes'],base.mem())
        state['owned_swap_peak_bytes']=max(state['owned_swap_peak_bytes'],base.proc_info(os.getpid())['swap_bytes'])
        assert state['mem_available_min_bytes']>=12*base.GIB,'seed memory threshold'
        assert base.pswpout()==state['pswpout_before'],'seed host swap-out changed'
        assert state['owned_swap_peak_bytes']==0,'seed process swap'
    try:
        capture_owner('before_write')
        source.mkdir()
        block=os.urandom(8*base.MiB)
        state['phase']='source_write'
        with path.open('xb') as f:
            for _ in range(1024):
                f.write(block)
                guard()
        capture_owner('after_write')
        state['phase']='source_sha';guard()
        result=base.tree(source)
        guard()
        capture_owner('after_sha')
        state['phase']='source_residency'
        result['residency']=residency(path)
        state['source_residency']=result['residency']
        capture_owner('after_mincore')
        guard()
        result['data_shape']='one random 8MiB block repeated 1024 times; fully materialized; compression off'
        assert result['bytes']==SIZE and result['files']==1 and result['residency']['all_resident']
    except BaseException as failure:
        state['error']=repr(failure)
        raise
    finally:
        try:
            state['pswpout_after']=base.pswpout()
            state['pswpout_delta']=state['pswpout_after']-state['pswpout_before']
            state['final_mem_available_bytes']=base.mem()
            state['final_owned_swap_bytes']=base.proc_info(os.getpid())['swap_bytes']
            state['owned_swap_peak_bytes']=max(state['owned_swap_peak_bytes'],state['final_owned_swap_bytes'])
            state['final_gate_pass']=state['pswpout_delta']==0 and state['final_owned_swap_bytes']==0
            if not state['final_gate_pass']:
                state.setdefault('error',"AssertionError('seed final resource gate')")
        except BaseException as error:
            state['final_snapshot_error']=repr(error)
        state['materialized_bytes']=path.stat().st_size if path.exists() else 0
        state['guard_scope']='per-write and SHA/residency boundaries; memory/process swap peaks are sampled'
        try:state['owner_final']=cgroup_owner()
        except Exception as error:state['owner_snapshot_error']=repr(error)
        base.save(args.evidence/'peer/seed-resource.json',state)
    assert state.get('pswpout_delta')==0,'seed host swap-out changed or unavailable'
    assert state.get('final_owned_swap_bytes')==0,'seed process swap or final sample unavailable'
    base.save(args.evidence/'peer/source.json',result)
    return result


def source_residency(args):
    state=json.loads((args.evidence/'peer/seed-resource.json').read_text())
    result=residency(base.owned(args.payload_root,'source')/'f000000.bin')
    result['host_pswpout']=base.pswpout()
    result['host_pswpout_delta_since_seed']=result['host_pswpout']-state['pswpout_before']
    result['mem_available_bytes']=base.mem()
    result['owned_probe_swap_bytes']=base.proc_info(os.getpid())['swap_bytes']
    result['probe_owner']=cgroup_owner()
    seed_owner=state.get('owner_final')
    snapshots=state.get('cgroup_snapshots',[])
    identities={(item['owner']['process']['pid'],item['owner']['process']['start_ticks'],
                 item['owner']['membership']) for item in snapshots}
    result['preparation_phase_snapshot_count']=len(snapshots)
    result['preparation_phase_owner_consistent']=(len(snapshots)==4 and len(identities)==1)
    if seed_owner:
        result['source_cgroup_after_probe']=cgroup_snapshot(seed_owner['membership'])
        result['same_preparation_process']=(seed_owner['process']['pid']==result['probe_owner']['process']['pid'] and
            seed_owner['process']['start_ticks']==result['probe_owner']['process']['start_ticks'])
        result['same_preparation_cgroup']=seed_owner['membership']==result['probe_owner']['membership']
    else:
        result['same_preparation_process']=False
        result['same_preparation_cgroup']=False
    return result


def resource_diagnostic(args):
    base.receipt(args.commit);assert base.node()==2
    root=args.payload_root
    assert root.is_absolute() and root.parent==Path('/dev/shm')
    assert root.name.startswith('cpnetflux-s0-') and not root.is_symlink()
    assert not root.exists()
    pre={'mem_available_bytes':base.mem(),'host_pswpout':base.pswpout(),
         'dev_shm_free_bytes':shutil.disk_usage('/dev/shm').free,'owner':cgroup_owner()}
    pre['owned_swap_bytes']=pre['owner']['process']['swap_bytes']
    report={'source_preparations':0,'commit':args.commit,'source_bytes':SIZE,'preflight':pre,
            'payload_server_started':False,'data_transfer_started':False,
            'outcome':'preflight_blocked'}
    args.evidence.mkdir(parents=True,exist_ok=True)
    assert not (args.evidence/'resource-diagnostic.json').exists()
    assert not (args.evidence/'peer/seed-resource.json').exists()
    blocked=[]
    if pre['mem_available_bytes']<12*base.GIB:blocked.append('MemAvailable below 12 GiB')
    if pre['dev_shm_free_bytes']<=9*base.GIB:blocked.append('/dev/shm free space is not above 9 GiB')
    if pre['owned_swap_bytes']!=0:blocked.append('diagnostic process already has swap')
    if blocked:
        report['preflight_block_reasons']=blocked
        report['payload_root_removed']=not root.exists()
        base.save(args.evidence/'resource-diagnostic.json',report)
        return report
    root.mkdir()
    marker=root/base.MARKER
    try:marker.write_text(base.TOKEN)
    except BaseException:
        marker.unlink(missing_ok=True)
        root.rmdir()
        raise
    report['source_preparations']=1
    error=None;cleanup_errors=[]
    try:
        report['source']=seed(args)
        report['seed_resource']=json.loads((args.evidence/'peer/seed-resource.json').read_text())
        report['followup_residency']=source_residency(args)
        probe=report['followup_residency']
        report['residency_gate_pass']=(probe['all_resident'] and
            probe['preparation_phase_owner_consistent'] and
            probe['same_preparation_process'] and
            probe['same_preparation_cgroup'] and
            report.get('seed_resource',{}).get('final_gate_pass',False) and
            probe['host_pswpout_delta_since_seed']==0 and
            probe['mem_available_bytes']>=12*base.GIB and
            probe['owned_probe_swap_bytes']==0)
        report['outcome']='prepared_resident' if report['residency_gate_pass'] else 'prepared_residency_blocked'
    except BaseException as failure:
        error=failure
        report['error']=repr(failure)
        report['outcome']='preparation_failed'
    finally:
        seed_resource=args.evidence/'peer/seed-resource.json'
        source_record=args.evidence/'peer/source.json'
        if seed_resource.is_file():report['seed_resource']=json.loads(seed_resource.read_text())
        if source_record.is_file():report['source_record']=json.loads(source_record.read_text())
        try:
            base.check_root(root)
            source=base.owned(root,'source')
            if source.exists():base.remove_owned(root,source)
            (root/base.MARKER).unlink();root.rmdir()
        except BaseException as failure:
            cleanup_errors.append(repr(failure))
        report['cleanup_errors']=cleanup_errors
        report['payload_root_removed']=not root.exists()
        report['dev_shm_free_after_bytes']=shutil.disk_usage('/dev/shm').free
        report['dev_shm_free_delta_bytes']=report['dev_shm_free_after_bytes']-pre['dev_shm_free_bytes']
        report['cleanup_pass']=not cleanup_errors and report['payload_root_removed']
        if not report['cleanup_pass']:
            report['outcome']='cleanup_failed'
            report['residency_gate_pass']=False
        snapshots=report.get('seed_resource',{}).get('cgroup_snapshots',[])
        identities={(item['owner']['process']['pid'],item['owner']['process']['start_ticks'],
                     item['owner']['membership']) for item in snapshots}
        report['preparation_phase_snapshot_count']=len(snapshots)
        report['preparation_phase_owner_consistent']=(len(snapshots)==4 and len(identities)==1)
        seed_process=report.get('seed_resource',{}).get('owner_final',{}).get('process',{})
        probe_process=report.get('followup_residency',{}).get('probe_owner',{}).get('process',{})
        report['same_preparation_process']=bool(seed_process and probe_process and
            seed_process.get('pid')==probe_process.get('pid') and
            seed_process.get('start_ticks')==probe_process.get('start_ticks'))
        seed_membership=report.get('seed_resource',{}).get('owner_final',{}).get('membership')
        probe_membership=report.get('followup_residency',{}).get('probe_owner',{}).get('membership')
        report['same_preparation_cgroup']=bool(seed_membership and seed_membership==probe_membership)
        base.save(args.evidence/'resource-diagnostic.json',report)
    if error is not None:raise error
    if cleanup_errors:raise RuntimeError('diagnostic cleanup failed; see resource-diagnostic.json')
    return report


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
    started=False;completed=False;phase='seed';name=None
    try:
        expected=own_peer(args,'seed')
        phase='server_start'
        base.peer(args,'start',name='large-reuse',port=port,data_base=61000);started=True;ready(port)
        for rep in range(1,6):
            for mode in (('original','cached') if rep%2 else ('cached','original')):
                name=f'8g-{mode}-r{rep}'
                phase='source_residency'
                resident=own_peer(args,'residency')
                base.save(args.evidence/'samples'/(name+'.source-residency.json'),resident)
                assert resident['all_resident'],'source residency gate'
                assert resident['host_pswpout_delta_since_seed']==0,'source host swap-out changed'
                assert resident['mem_available_bytes']>=12*base.GIB,'source memory threshold'
                assert resident['owned_probe_swap_bytes']==0,'source probe swap'
                phase='client_sample'
                row=base.client_sample(args,name,expected,'2','on',port,summary_path=mode,mapped_download=True)
                row['rep']=rep;row['source_residency_before']=resident
                row['verified_chunk_count']=verify_chunks(row['manifest_checks']['member_example'])
                with gzip.open(args.evidence/'samples'/(name+'.summary.json.gz'),'rt') as f:
                    row['mapped_checks']=verify_ranges(json.load(f)['file_timings'])
                rows.append(row);base.save(args.evidence/'samples'/(name+'.receipt.json'),row)
                base.save(args.evidence/'results.json',rows)
                print(json.dumps({'sample':name,'t_ready':row['t_ready_seconds'],'gbps':row['whole_task_gbps']}),flush=True)
        phase='source_final_check'
        checked=base.peer(args,'source-check')
        assert checked=={k:expected[k] for k in ('sha256','files','bytes')}
        base.save(args.evidence/'source-final-check.json',checked)
        completed=True
    except BaseException as error:
        base.save(args.evidence/'case-failure.json',{'phase':phase,'sample':name,
                  'completed_samples':len(rows),'error':repr(error)})
        raise
    finally:
        try:
            if started:
                server=base.peer(args,'stop',name='large-reuse');base.save(args.evidence/'large-reuse.server.json',server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
                if completed:assert server['sender_phases']['count']==80
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
    p.add_argument('--action',required=True,choices=['seed','residency','matrix','resource-diagnostic'])
    p.add_argument('--evidence',required=True,type=Path);p.add_argument('--payload-root',required=True,type=Path)
    p.add_argument('--commit',required=True)
    args=p.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
    if args.action=='matrix':matrix(args)
    elif args.action=='resource-diagnostic':
        print(json.dumps(resource_diagnostic(args)),flush=True)
    else:
        base.receipt(args.commit)
        result=seed(args) if args.action=='seed' else source_residency(args)
        print(json.dumps(result))


if __name__=='__main__':main()
