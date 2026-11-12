#!/usr/bin/env python3
"""Bounded S0 baseline; fixed-commit peers, exact owned cleanup, no runtime tuning."""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import random
import re
import shlex
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import threading
import time

import receive_network

PROJECT = Path(__file__).resolve().parents[2]
SCRIPT = Path(__file__).resolve()
MARKER = '.cpnetflux-s0-owned'
TOKEN = 'cpnetflux-s0-v1\n'
MiB = 1024**2
GIB = 1024**3
RECEIVER_PAYLOAD_PROFILE_FIELDS = (
    'data_frame_count','frame_payload_allocations','frame_payload_reuses',
    'frame_payload_allocated_capacity_bytes','frame_payload_peak_capacity_bytes',
    'frame_payload_bytes','frame_payload_capacity_bytes','logical_buffer_allocations',
    'logical_copy_bytes','logical_copy_seconds','prefix_shift_calls','prefix_shift_bytes',
    'prefix_shift_seconds','decompression_calls','decompression_seconds','frame_crc_bytes',
    'frame_crc_seconds','whole_file_write_at_all_calls','whole_file_write_at_all_bytes',
    'whole_file_write_at_all_failures','whole_file_write_at_all_seconds',
)
RECEIVER_CPU_PROFILE_FIELDS = (
    'socket_read_calls','crc_calls','crc_bytes','state_calls','lease_start_calls',
    'lease_finish_calls','write_calls','write_logical_bytes','range_calls',
    'physical_write_calls','physical_write_bytes','push_calls','pop_calls',
    'socket_read_cpu_seconds','crc_cpu_seconds','state_cpu_seconds','lease_cpu_seconds',
    'write_cpu_seconds','range_cpu_seconds','physical_write_cpu_seconds',
    'push_cpu_seconds','pop_cpu_seconds',
)
RECEIVER_PROFILE_PEAK_FIELDS = {'frame_payload_peak_capacity_bytes'}
BINARIES = ['cpnetflux-gridftp-server','cpnetflux-tree-download-client','cpnetflux-tree-upload-client']

def save(path, value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2)+'\n')

def output(*args):
    return subprocess.check_output(args,text=True).strip()

def cases():
    return [(k*1024,1000) for k in (4,16,64,256)]+[(k*1024,10000) for k in (4,16,64)]

def check_root(root):
    assert root.is_absolute() and root.parent == Path('/dev/shm')
    assert root.name.startswith('cpnetflux-s0-') and not root.is_symlink()
    marker=root/MARKER
    assert marker.is_file() and not marker.is_symlink() and marker.read_text()==TOKEN

def owned(root, name):
    check_root(root)
    assert re.fullmatch('[a-zA-Z0-9][a-zA-Z0-9-]*',name), 'invalid owned child name'
    child=root/name
    assert not child.is_symlink() and child.resolve().parent==root.resolve()
    return child

def remove_owned(root, path):
    assert path.parent==root and owned(root,path.name)==path
    assert path.is_dir() and not path.is_symlink()
    shutil.rmtree(path)  # Only a marked root's exact child; internal links aren't followed.

def percentile(values, fraction):
    values=sorted(values)
    at=(len(values)-1)*fraction
    low=int(at);high=min(low+1,len(values)-1)
    return values[low]+(values[high]-values[low])*(at-low)

def summarize_profile(rows,key,fields):
    observations=[row.get(key) for row in rows if isinstance(row.get(key),dict)]
    if not observations:return None
    schemas={item.get('schema') for item in observations}
    scopes={item.get('scope') for item in observations}
    assert len(schemas)==len(scopes)==1, f'inconsistent {key} schema or scope'
    totals={};peaks={}
    for field in fields:
        values=[item[field] for item in observations
                if isinstance(item.get(field),(int,float)) and not isinstance(item.get(field),bool)]
        if not values:continue
        target=peaks if field in RECEIVER_PROFILE_PEAK_FIELDS else totals
        target[field]=max(values) if field in RECEIVER_PROFILE_PEAK_FIELDS else sum(values)
    result={'ranges':len(observations),'schema':next(iter(schemas)),'scope':next(iter(scopes)),
            'totals':totals}
    if peaks:result['peak']=peaks
    if key=='receiver_cpu_observation':
        result['write_cpu_owners']=sorted({item['write_cpu_owner'] for item in observations
                                           if isinstance(item.get('write_cpu_owner'),str)})
    return result


def phases(rows):
    result={'count':len(rows),'scope':'per-file intervals; sums overlap across workers and scopes, not task wall'}
    for key in ('wall_seconds','queue_wait_seconds','first_payload_seconds','read_seconds','write_seconds',
                'payload_io_seconds','file_result_seconds','manifest_seconds','checksum_seconds','finalize_seconds'):
        values=[r[key] for r in rows if isinstance(r.get(key),(int,float))]
        if values:
            result[key]={'sum':sum(values),'median':statistics.median(values),'p95':percentile(values,.95)}
    payload=summarize_profile(rows,'receiver_payload_observation',RECEIVER_PAYLOAD_PROFILE_FIELDS)
    cpu=summarize_profile(rows,'receiver_cpu_observation',RECEIVER_CPU_PROFILE_FIELDS)
    if payload is not None or cpu is not None:
        result['receiver_profile']={'payload':payload,'cpu':cpu}
    result['examples']=rows[:2]
    return result

def sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda:f.read(MiB),b''):digest.update(block)
    return digest.hexdigest()

def tree(root):
    digest=hashlib.sha256();count=total=0
    for path in sorted(root.rglob('*')):
        assert not path.is_symlink(), 'symlink in experimental tree'
        if not path.is_file():continue
        relative=path.relative_to(root).as_posix()
        if '.cpnetflux.' in relative:continue
        assert re.fullmatch(r'f[0-9]{6}\.bin',relative), ('unexpected payload',relative)
        size=path.stat().st_size;count+=1;total+=size
        digest.update(relative.encode()+b'\0'+str(size).encode()+b'\0'+sha(path).encode()+b'\0')
    return {'sha256':digest.hexdigest(),'files':count,'bytes':total}

def mem():
    return int(next(l.split()[1] for l in Path('/proc/meminfo').read_text().splitlines() if l.startswith('MemAvailable:')))*1024

def pswpout():
    return int(next(l.split()[1] for l in Path('/proc/vmstat').read_text().splitlines() if l.startswith('pswpout ')))

def proc_info(pid):
    base=Path('/proc')/str(pid)
    fields=(base/'stat').read_text().rsplit(')',1)[1].split()
    values={line.split(':')[0]:int(line.split()[1])*1024 for line in (base/'status').read_text().splitlines() if line.endswith('kB')}
    return {'pid':pid,'start_ticks':int(fields[19]),'cpu_seconds':(int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK'),
            'rss_bytes':values.get('VmRSS',0),'swap_bytes':values.get('VmSwap',0)}

def node():
    text=output('ip','-o','-4','addr','show','dev','eno17095np0')
    return 2 if '192.168.100.2/' in text else 1

def snapshot():
    which=node();local=f'192.168.100.{which}';peer=f'192.168.100.{3-which}'
    counters={p.name:int(p.read_text()) for p in Path('/sys/class/net/eno17095np0/statistics').iterdir()}
    production=proc_info(126766 if which==1 else 164834)
    assert production['start_ticks']==(1191040 if which==1 else 1679976)
    return {'mem_available_bytes':mem(),'pswpout':pswpout(),'net':counters,'production':production,
            'route':output('ip','route','get',peer,'from',local)}

def configuration():
    lines=(PROJECT/'build/CMakeCache.txt').read_text().splitlines()
    return [l for l in lines if re.match(r'CPNETFLUX_.*:BOOL=|CMAKE_BUILD_TYPE:STRING=|CMAKE_CXX_COMPILER:FILEPATH=|CMAKE_PROJECT_VERSION:STATIC=',l)]

def receipt(commit):
    assert output('git','-C',str(PROJECT),'rev-parse','HEAD')==commit
    assert not output('git','-C',str(PROJECT),'diff','--name-only','HEAD'), 'tracked dirty source'
    return {'commit':commit,'configuration':configuration(),
            'binaries':{n:sha(PROJECT/'build'/n) for n in BINARIES},'project':str(PROJECT)}

def environment(receiver_profile=False, range_timing=None):
    env=os.environ.copy()
    # Freeze task-local knobs; never inherit another experiment's runtime policy.
    for key in list(env):
        if key.startswith('CPNETFLUX_'):env.pop(key)
    if range_timing is None:range_timing=receiver_profile
    env.update(CPNETFLUX_STAGE_READ_CONCURRENCY='0',CPNETFLUX_STAGE_WRITE_CONCURRENCY='0',
               CPNETFLUX_STAGE_BUFFER_BYTES=str(64*MiB),CPNETFLUX_PAYLOAD_PIPELINE_DEPTH='2',
               CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION='queued',CPNETFLUX_SENDER_PAYLOAD_EXECUTION='queued',
               CPNETFLUX_RANGE_TIMING='on' if range_timing else 'off',
               CPNETFLUX_RECEIVER_PROFILE='1' if receiver_profile else '0',
               CPNETFLUX_SENDER_PROFILE='0',
               CPNETFLUX_RECEIVER_CPU_PROFILE='1' if receiver_profile else '0',
               CPNETFLUX_MAPPED_WRITE_PROFILE='0',
               CPNETFLUX_RECEIVER_WRITE_BATCH='1',CPNETFLUX_RANGE_DIRECT_WRITE='parallel')
    return env

def resource_state():
    return {'mem_available_min_bytes':mem(),'owned_swap_peak_bytes':0,'owned_swap_current_bytes':None,'rss_peak_bytes':0,
            'cpu_seconds_sampled_lower_bound':0,'pswpout_before':pswpout(),'samples':0,'tcp_snapshots':[]}

def observe(proc, state):
    state['mem_available_min_bytes']=min(state['mem_available_min_bytes'],mem())
    try:
        info=proc_info(proc.pid)
        state['owned_swap_current_bytes']=info['swap_bytes']
        state['owned_swap_peak_bytes']=max(state['owned_swap_peak_bytes'],info['swap_bytes'])
        state['rss_peak_bytes']=max(state['rss_peak_bytes'],info['rss_bytes'])
        state['cpu_seconds_sampled_lower_bound']=max(state['cpu_seconds_sampled_lower_bound'],info['cpu_seconds'])
    except FileNotFoundError:pass
    state['samples']+=1
    assert state['mem_available_min_bytes']>=12*GIB, 'memory threshold'
    assert state['owned_swap_peak_bytes']==0 and pswpout()==state['pswpout_before'], 'swap gate'
    if state['samples']%25==0 and len(state['tcp_snapshots'])<4 and proc.poll() is None:
        text=output('ss','-tinp')
        lines=text.splitlines();filtered=[]
        for i,line in enumerate(lines):
            if f'pid={proc.pid},' in line:
                filtered+=lines[max(0,i-1):min(len(lines),i+2)]
        if filtered:state['tcp_snapshots'].append(filtered)

def run_guarded(argv, log, timeout=120, summary_path=None, mapped_download=False, tree_manifest_serializer=None,
                tree_manifest_profile=None, member_work_profile=None, tcp_receive_buffer_bytes=None,
                process_observer=None, receiver_profile=False, range_timing=None, tcp_exact_read=None):
    state=resource_state();started=time.monotonic()
    env=environment(receiver_profile=receiver_profile,range_timing=range_timing)
    if tcp_receive_buffer_bytes is not None:
        assert tcp_receive_buffer_bytes in ('off','1048576')
        env['CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES']=tcp_receive_buffer_bytes
    if tcp_exact_read is not None:
        assert tcp_exact_read in ('off','on')
        env['CPNETFLUX_TCP_EXACT_READ']=tcp_exact_read
    if mapped_download:
        env['CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION']='inline'
        env['CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE']='mmap'
    if summary_path is not None:
        assert summary_path in ('original','cached')
        env['CPNETFLUX_SUMMARY_PATH']=summary_path
    if tree_manifest_serializer is not None:
        assert tree_manifest_serializer in ('original','buffered')
        env['CPNETFLUX_TREE_MANIFEST_SERIALIZER']=tree_manifest_serializer
    if tree_manifest_profile is not None:
        assert tree_manifest_profile in ('0','1')
        env['CPNETFLUX_TREE_MANIFEST_PROFILE']=tree_manifest_profile
    if member_work_profile is not None:
        assert member_work_profile in ('0','1')
        env['CPNETFLUX_MEMBER_WORK_PROFILE']=member_work_profile
    with log.open('x') as f:
        proc=subprocess.Popen(argv,stdout=f,stderr=subprocess.STDOUT,env=env,start_new_session=True)
        exited=threading.Event();exit_info={}
        def wait_for_exit():
            exit_info['code']=proc.wait()
            exit_info['at']=time.monotonic()
            exited.set()
        waiter=threading.Thread(target=wait_for_exit)
        waiter.start()
        failure=None
        try:
            while not exited.is_set():
                if process_observer is not None:process_observer(proc,state)
                observe(proc,state)
                assert time.monotonic()-started<timeout, 'owned process timeout'
                exited.wait(.01)
        except BaseException as error:
            failure=error
        finally:
            if not exited.is_set():
                proc.terminate()
                if not exited.wait(5):proc.kill()
            waiter.join()
    state['exit_code']=exit_info.get('code')
    state['wall_seconds']=exit_info['at']-started
    state['observer_wall_seconds']=time.monotonic()-started
    state['observer_seconds_after_exit']=state['observer_wall_seconds']-state['wall_seconds']
    state['pswpout_after']=pswpout()
    state['pswpout_delta']=state['pswpout_after']-state['pswpout_before']
    if failure is None and state['pswpout_delta'] != 0:
        failure=AssertionError('host pswpout gate')
    if failure is not None:
        state['failure']=repr(failure)
        failure_path=log.with_name(log.stem+'.resource-failure.json')
        save(failure_path,state)
        try:
            failure.resource_state=state
            failure.resource_failure_path=str(failure_path)
        except (AttributeError,TypeError):
            pass
        raise failure
    return state

def compact_log(path):
    rows=[];sender_profiles=[];pacing=[]
    with path.open() as f:
        for line in f:
            prefix='CPNETFLUX_PERSISTENT_TIMING '
            if line.startswith(prefix):rows.extend(json.loads(line[len(prefix):])['files'])
            sender_prefix='CPNETFLUX_SENDER_WORK '
            if line.startswith(sender_prefix):sender_profiles.append(json.loads(line[len(sender_prefix):]))
            pacing_prefix='CPNETFLUX_SENDER_PACING '
            if line.startswith(pacing_prefix):pacing.append(json.loads(line[len(pacing_prefix):]))
    result=phases(rows)
    if sender_profiles:result['sender_work_profiles']=sender_profiles
    if pacing:result['sender_pacing']=pacing
    with path.open('rb') as src,gzip.open(str(path)+'.gz','xb') as dst:shutil.copyfileobj(src,dst)
    path.unlink()
    return result

def peer(args, action, **values):
    argv=['python3',str(SCRIPT),'--action',action,'--evidence',str(args.evidence),
          '--payload-root',str(args.payload_root),'--commit',args.commit]
    for key,value in values.items():argv+=['--'+key.replace('_','-'),str(value)]
    result=subprocess.run(['ssh','-n','-o','BatchMode=yes','r770-2',shlex.join(argv)],text=True,capture_output=True,timeout=180)
    assert result.returncode==0,result.stderr+result.stdout
    return json.loads(result.stdout)

def generate(args):
    assert args.size*args.count<=GIB and args.size in (4096,16384,65536,262144) and 0<args.count<=10000
    source=owned(args.payload_root,'source');source.mkdir()
    started=time.monotonic()
    for index in range(args.count):
        (source/f'f{index:06}.bin').write_bytes(random.Random(210000+index).randbytes(args.size))
    result=tree(source);result['generation_seconds']=time.monotonic()-started
    save(args.evidence/'peer/source.json',result)
    return result

def server_run(args):
    receipt(args.commit);check_root(args.payload_root)
    folder=args.evidence/'peer';folder.mkdir(parents=True,exist_ok=True)
    log=folder/(args.name+'.server.log');stop=folder/(args.name+'.stop')
    assert not stop.exists()
    command=[str(PROJECT/'build/cpnetflux-gridftp-server'),'--root',str(args.payload_root),
             '--host','192.168.100.2','--port',str(args.port),'--data-port-base',str(args.data_base),
             '--connections','4','--checksum','crc32c','--chunk-size',str(MiB),'--buffer-size','65536',
             '--data-transport','tcp','--final-verify-policy','verified_chunks','--commit-sync-policy','none']
    state=resource_state();before=snapshot();started=time.monotonic()
    server_env=environment()
    server_env['CPNETFLUX_SENDER_WORK_PROFILE']=args.sender_work_profile
    server_env['CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND']=args.sender_pacing
    with log.open('x') as f:
        process=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT,env=server_env)
        save(folder/(args.name+'.server.pid.json'),proc_info(process.pid))
        try:
            while not stop.exists():
                assert process.poll() is None, 'isolated server exited'
                observe(process,state)
                assert time.monotonic()-started<600, 'isolated server lifetime exceeded'
                time.sleep(.01)
        except BaseException as error:
            state['error']=repr(error)
        finally:
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
    state['pswpout_delta']=pswpout()-state['pswpout_before']
    result={'resource':state,'before':before,'after':snapshot(),'sender_phases':compact_log(log),'command':command}
    result['sender_work_profile_mode']=args.sender_work_profile
    save(folder/(args.name+'.server.done.json'),result)

def start_server(args):
    folder=args.evidence/'peer';folder.mkdir(parents=True,exist_ok=True)
    argv=[sys.executable,str(SCRIPT),'--action','server-run','--evidence',str(args.evidence),
          '--payload-root',str(args.payload_root),'--commit',args.commit,'--name',args.name,
          '--port',str(args.port),'--data-base',str(args.data_base),
          '--sender-work-profile',args.sender_work_profile,'--sender-pacing',args.sender_pacing]
    with (folder/(args.name+'.supervisor.log')).open('x') as f:
        proc=subprocess.Popen(argv,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        pidfile=folder/(args.name+'.server.pid.json')
        if pidfile.exists():return json.loads(pidfile.read_text())
        time.sleep(.02)
    raise TimeoutError('server supervisor startup')

def stop_server(args):
    folder=args.evidence/'peer';(folder/(args.name+'.stop')).touch(exist_ok=False)
    done=folder/(args.name+'.server.done.json');deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        if done.exists():return json.loads(done.read_text())
        time.sleep(.02)
    raise TimeoutError('owned server stop')

def check_manifests(slot, count, size):
    member=[];trees=[]
    for path in slot.rglob('*.manifest'):
        assert not path.is_symlink()
        fields=dict(line.split('=',1) for line in path.read_text().splitlines() if '=' in line)
        if 'total_size' in fields:
            assert fields.get('state')=='committed' and fields.get('checksum_algorithm')=='crc32c',path
            assert int(fields['total_size'])==size,path
            assert fields.get('completed_ranges') and fields.get('verified_chunks'),path
            member.append(path)
        else:
            assert fields.get('mode')=='download' and fields.get('checksum_algorithm')=='crc32c',path
            records=[item.split(':') for item in fields['files'].split(',')]
            assert len(records)==count and all(len(r)==6 and r[4]=='completed' and int(r[1])==size for r in records),path
            assert len({r[0] for r in records})==count,path
            trees.append(path)
    assert len(member)==count, ('member checkpoint count',len(member),count)
    assert len(trees)==1, ('tree checkpoint count',len(trees))
    return {'committed_member_manifests':len(member),'completed_tree_manifests':len(trees),
            'member_example':member[0].read_text()}

def client_sample(args, name, expected, window, timing, port, summary_path=None, mapped_download=False, runner=None,
                  tree_manifest_serializer=None, data_frame_bytes=None, tcp_receive_buffer_bytes=None,
                  receiver_profile=False, range_timing=None, network_profile=False, connections=4,
                  network_settle_seconds=0, tcp_exact_read=None):
    assert connections in (1,4), 'bounded client supports one or four lanes'
    assert network_settle_seconds in (0,.5) and (network_profile or not network_settle_seconds)
    assert not mapped_download or connections==4, 'mapped download requires four lanes'
    if data_frame_bytes is not None:
        assert data_frame_bytes in (65532,262144)
    slot=owned(args.payload_root,name);slot.mkdir();target=slot/'output'
    folder=args.evidence/'samples';folder.mkdir(exist_ok=True)
    summary=folder/(name+'.summary.json');log=folder/(name+'.client.log')
    command=[str(PROJECT/'build/cpnetflux-tree-download-client'),'--host','192.168.100.2','--port',str(port),
             '--local-bind-address','192.168.100.1','--source-dir','source','--dest-dir',str(target),
             '--connections',str(connections),'--file-parallelism',str(connections),'--control-reuse','worker','--data-session-reuse','tree',
             '--file-scheduling','dynamic','--data-pending-window',window,'--checksum','crc32c',
             '--chunk-size',str(MiB),'--buffer-size','65536','--data-transport','tcp',
             '--final-verify-policy','verified_chunks','--commit-sync-policy','none',
             '--manifest-flush-policy','every_n_chunks','--manifest-flush-interval-chunks','16',
             '--range-checkpoint-batch-chunks','16','--posix-write-strategy','direct',
             '--file-io-buffer-size','0','--compression','off','--phase-timing',timing,'--json-summary',str(summary)]
    if data_frame_bytes is not None:
        command+=['--data-frame-bytes',str(data_frame_bytes)]
    row={'name':name,'window_request':window,'phase_timing':timing,'expected':expected,'command':command,
         'before':snapshot(),'result':'fail'}
    if receiver_profile:row['receiver_profile_requested']=True
    if range_timing is not None:row['range_timing_requested']=range_timing
    if network_profile:row['receiver_network_window']={}
    try:
        network_window=row.get('receiver_network_window')
        if network_window is not None:
            network_window['before']=receive_network.snapshot(full=True)
        run_options=dict(summary_path=summary_path,mapped_download=mapped_download)
        if tcp_receive_buffer_bytes is not None:
            run_options['tcp_receive_buffer_bytes']=tcp_receive_buffer_bytes
        if tcp_exact_read is not None:
            run_options['tcp_exact_read']=tcp_exact_read
        if tree_manifest_serializer is not None:
            run_options['tree_manifest_serializer']=tree_manifest_serializer
        if receiver_profile:
            run_options['receiver_profile']=True
        if range_timing is not None:
            run_options['range_timing']=range_timing
        try:
            resources=(runner or run_guarded)(command,log,**run_options)
        finally:
            if network_window is not None:
                after_transfer=receive_network.snapshot(full=True)
                network_window['after_transfer']=after_transfer
                network_window['analysis']=receive_network.analyze(network_window['before'],after_transfer)
                if network_settle_seconds:
                    # Still before local SHA. The refresh tail is separate from the process window.
                    time.sleep(network_settle_seconds)
                    after_settle=receive_network.snapshot(full=True)
                    network_window['after_settle_before_sha']=after_settle
                    network_window['refresh_tail_analysis']=receive_network.analyze(after_transfer,after_settle)
                    network_window['settled_analysis']=receive_network.analyze(network_window['before'],after_settle)
        row['resource']=resources;assert resources['exit_code']==0,log.read_text()[-4000:]
        value=json.loads(summary.read_text())
        if tree_manifest_serializer is not None:
            row['tree_manifest_serializer_request']=tree_manifest_serializer
            assert value['tree_manifest_serializer_requested']==tree_manifest_serializer
            assert value['tree_manifest_serializer_effective']==tree_manifest_serializer
            assert value['tree_manifest_serializer_scope']=='all_client_tree_manifest_save_attempts'
        if summary_path is not None:
            row['summary_path_request']=summary_path
            assert value['summary_path_requested']==summary_path
            effective='not_used' if value['summary_tree_hash_source']=='verified_chunks' else summary_path
            assert value['summary_path_effective']==effective
        assert value['actual_transport']=='v3' and value['completed_files']==args.count
        assert value['failed_files']==value['changed_files']==0
        # A fresh download selects before it has the remote manifest: auto is 2.
        selected=2 if window=='auto' else int(window)
        assert value['data_pending_window']==selected and value['data_pending_window_policy']==('auto' if window=='auto' else 'fixed')
        assert value['data_pending_high_watermark']==0, 'download receiver is not sender pending depth'
        actual=tree(target)
        assert actual=={k:expected[k] for k in ('sha256','files','bytes')},(actual,expected)
        row['manifest_checks']=check_manifests(slot,args.count,args.size)
        row['receiver_phases']=phases(value.pop('file_timings',[]))
        if receiver_profile:
            assert row['receiver_phases'].get('receiver_profile',{}).get('payload') is not None
            assert row['receiver_phases'].get('receiver_profile',{}).get('cpu') is not None
        row['summary']=value;row['actual_tree']=actual;row['after']=snapshot()
        row['t_ready_seconds']=resources['wall_seconds']
        row['whole_task_gbps']=args.size*args.count*8/resources['wall_seconds']/1e9
        row['result']='pass'
        with summary.open('rb') as src,gzip.open(str(summary)+'.gz','xb') as dst:shutil.copyfileobj(src,dst)
        summary.unlink()
    except BaseException as error:
        row['error']=repr(error)
        resource_state=getattr(error,'resource_state',None)
        resource_failure_path=getattr(error,'resource_failure_path',None)
        if resource_failure_path is None:
            candidate=log.with_name(log.stem+'.resource-failure.json')
            if candidate.exists():resource_failure_path=str(candidate)
        if resource_state is None and resource_failure_path is not None:
            resource_state=json.loads(Path(resource_failure_path).read_text())
        if resource_state is not None:row['resource_failure']=resource_state
        if resource_failure_path is not None:row['resource_failure_path']=str(resource_failure_path)
        try:row['after_failure']=snapshot()
        except BaseException as snapshot_error:row['after_failure_error']=repr(snapshot_error)
        raise
    finally:
        if log.exists():row['client_log_phases']=compact_log(log)
        remove_owned(args.payload_root,slot)
        row['output_slot_removed']=not slot.exists()
        save(folder/(name+'.receipt.json'),row)
    return row

def matrix(args):
    assert node()==1
    local=receipt(args.commit);remote=peer(args,'receipt');assert local==remote,(local,remote)
    save(args.evidence/'matching-builds.json',{'R770-1':local,'R770-2':remote})
    assert not args.payload_root.exists()
    args.payload_root.mkdir();(args.payload_root/MARKER).write_text(TOKEN)
    peer(args,'init');results=[]
    for index,(size,count) in enumerate(cases()):
        args.size,args.count=size,count
        case=f'{size//1024}k-{count}';port=49210+index;data_base=59000
        expected=peer(args,'generate',size=size,count=count)
        case_rows=[];server_started=False
        try:
            peer(args,'start',name=case,port=port,data_base=data_base);server_started=True
            deadline=time.monotonic()+10
            while True:
                try:
                    with socket.create_connection(('192.168.100.2',port),timeout=.2,source_address=('192.168.100.1',0)) as control:
                        assert control.recv(1024).startswith(b'220')
                    break
                except OSError:
                    assert time.monotonic()<deadline;time.sleep(.02)
            for rep in range(1,4):
                choices=[('auto','off')]
                if size==4096 and count==1000:choices.append(('auto','on'))
                if count==10000:choices.append(('8','off'))
                if rep%2==0:choices.reverse()
                for window,timing in choices:
                    name=f'{case}-w{window}-timing{timing}-r{rep}'
                    row=client_sample(args,name,expected,window,timing,port)
                    case_rows.append(row);results.append(row)
                    print(json.dumps({'sample':name,'t_ready_seconds':row['t_ready_seconds'],'gbps':row['whole_task_gbps']}),flush=True)
        finally:
            if server_started:
                server=peer(args,'stop',name=case)
                save(args.evidence/(case+'.server.json'),server)
                assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0,server['resource']
            checked=peer(args,'source-check')
            assert checked=={k:expected[k] for k in ('sha256','files','bytes')}
            peer(args,'source-cleanup')
        save(args.evidence/(case+'.rows.json'),case_rows)
    peer(args,'finish');(args.payload_root/MARKER).unlink();args.payload_root.rmdir()
    save(args.evidence/'results.json',results)
    grouped={}
    for row in results:
        key=f"{row['expected']['bytes']//row['expected']['files']//1024}k-{row['expected']['files']}-w{row['window_request']}-timing{row['phase_timing']}"
        grouped.setdefault(key,[]).append(row['t_ready_seconds'])
    summary={key:{'count':len(v),'values':v,'median':statistics.median(v),'p95_descriptive':percentile(v,.95),
                  'cv_population':statistics.pstdev(v)/statistics.mean(v)} for key,v in grouped.items()}
    save(args.evidence/'summary.json',summary)
    print(json.dumps({'result':'pass','samples':len(results),'payload_root_removed':not args.payload_root.exists(),'summary':summary}))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--action',required=True,choices=['receipt','init','generate','source-check','source-cleanup','finish','start','stop','server-run','matrix'])
    p.add_argument('--evidence',required=True,type=Path);p.add_argument('--payload-root',required=True,type=Path)
    p.add_argument('--commit',required=True);p.add_argument('--name',default='')
    p.add_argument('--size',type=int,default=0);p.add_argument('--count',type=int,default=0)
    p.add_argument('--port',type=int,default=0);p.add_argument('--data-base',type=int,default=59000)
    p.add_argument('--sender-pacing',choices=('off','1500000000'),default='off')
    p.add_argument('--sender-work-profile',choices=('0','1'),default='0')
    args=p.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
    if args.action in ('start','stop','server-run'):
        assert re.fullmatch('[a-zA-Z0-9][a-zA-Z0-9-]*',args.name), 'invalid server evidence name'
    if args.action=='receipt':result=receipt(args.commit)
    elif args.action=='init':
        assert args.payload_root.parent==Path('/dev/shm') and args.payload_root.name.startswith('cpnetflux-s0-')
        args.payload_root.mkdir();(args.payload_root/MARKER).write_text(TOKEN);result=snapshot()
    elif args.action=='generate':result=generate(args)
    elif args.action=='source-check':result=tree(owned(args.payload_root,'source'))
    elif args.action=='source-cleanup':
        remove_owned(args.payload_root,owned(args.payload_root,'source'));result={'removed':True}
    elif args.action=='finish':
        check_root(args.payload_root);(args.payload_root/MARKER).unlink();args.payload_root.rmdir();result={'removed':True}
    elif args.action=='start':result=start_server(args)
    elif args.action=='stop':result=stop_server(args)
    elif args.action=='server-run':server_run(args);return
    else:matrix(args);return
    print(json.dumps(result))

if __name__=='__main__':main()
