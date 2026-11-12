#!/usr/bin/env python3
"""S2: one reference/profile pair, attribution only, no performance acceptance."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys
import time

import run_small_file_baseline as base
from owned_process_profile import OwnedProcess
from run_summary_path_ab import ready

SCRIPT=Path(__file__).resolve()


def summarize_blocking(rows):
    counts={};total=blocked=0
    for row in rows:
        for thread in row['threads']:
            total+=1
            if thread['state'] in ('S','D'):
                blocked+=1;key=thread['wchan'];counts[key]=counts.get(key,0)+1
    return {'scope':'thread snapshot counts, not wait duration or critical-path wall',
        'snapshots':len(rows),'thread_observations':total,'blocked_fraction':blocked/total if total else None,
        'blocked_wchan_counts':dict(sorted(counts.items(),key=lambda kv:-kv[1]))}


def compare(rows):
    assert len(rows)==2,'one reference/profile pair only'
    assert len({r['summary']['tree_hash'] for r in rows})==1,'tree hash changed'
    for r in rows:
        assert r['result']=='pass' and r['output_slot_removed'] and r['t_ready_seconds']>0
        s=r['summary']
        assert s['actual_transport']=='v3' and s['completed_files']==10000
        assert s['summary_path_effective']=='cached' and s['summary_tree_hash_source']=='filesystem_read'
    return {'pairs':1,'scope':'gated release to independent exit; attribution, not gain/CV acceptance',
        'observed_profile_wall_change':rows[1]['t_ready_seconds']/rows[0]['t_ready_seconds']-1,
        'batch_implementation_budget_proven':False,
        'reason':'CPU proportions and overlapping intervals alone do not prove removable critical-path wall',
        'arms':{name:{'t_ready_seconds':r['t_ready_seconds'],
            'pre_summary_elapsed_seconds':r['summary']['elapsed_seconds'],
            'summary_hash_seconds':r['summary']['summary_tree_hash_seconds'],
            'post_pre_summary_and_hash_residual_seconds':r['t_ready_seconds']-r['summary']['elapsed_seconds']-r['summary']['summary_tree_hash_seconds']}
            for name,r in zip(('reference','sampled'),rows)}}


def peer(args,action):
    argv=['python3',str(SCRIPT),'--action',action,'--evidence',str(args.evidence),
          '--payload-root',str(args.payload_root),'--commit',args.commit,'--name',args.name]
    r=subprocess.run(['ssh','-n','-o','BatchMode=yes','r770-2',shlex.join(argv)],text=True,capture_output=True,timeout=60)
    assert r.returncode==0,r.stderr+r.stdout
    return json.loads(r.stdout)


def server_run(args):
    base.receipt(args.commit);before=base.snapshot();state=base.resource_state()
    folder=args.evidence/'peer'/args.name;folder.mkdir(parents=True,exist_ok=True)
    command=[str(base.PROJECT/'build/cpnetflux-gridftp-server'),'--root',str(args.payload_root),
        '--host','192.168.100.2','--port','49324','--data-port-base','61200','--connections','4',
        '--checksum','crc32c','--chunk-size',str(base.MiB),'--buffer-size','65536','--data-transport','tcp',
        '--final-verify-policy','verified_chunks','--commit-sync-policy','none']
    log=folder/'server.log';process=None;started=time.monotonic();profile=None
    try:
        process=OwnedProcess(command,log,args.payload_root,folder/'collector',args.name=='sampled',base.environment())
        process.release();base.save(folder/'server-ready.json',base.proc_info(process.pid))
        while not (folder/'server-stop').exists():
            assert process.process.poll() is None,'isolated server exited'
            process.observe(state)
            if (folder/'profile-stop').exists() and profile is None:profile=process.finish_sampling()
            assert time.monotonic()-started<180,'isolated server lifetime'
            time.sleep(.01)
        if profile is None:profile=process.finish_sampling()
    except BaseException as e:state['error']=repr(e)
    finally:
        if process:process.close()
        state['pswpout_delta']=base.pswpout()-state['pswpout_before']
        base.save(folder/'server-done.json',{'resource':state,'before':before,'after':base.snapshot(),
            'sender_phases':base.compact_log(log) if log.exists() else {},'command':command,'profile':profile})


def wait_json(path,timeout=20):
    deadline=time.monotonic()+timeout
    while True:
        try:return json.loads(path.read_text())
        except (FileNotFoundError,json.JSONDecodeError):pass
        assert time.monotonic()<deadline,('evidence timeout',str(path))
        time.sleep(.01)


def start_server(args):
    base.receipt(args.commit)
    folder=args.evidence/'peer'/args.name;folder.mkdir(parents=True,exist_ok=False)
    command=[sys.executable,str(SCRIPT),'--action','server-run','--evidence',str(args.evidence),
        '--payload-root',str(args.payload_root),'--commit',args.commit,'--name',args.name]
    with (folder/'supervisor.log').open('x') as f:
        process=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT,start_new_session=True)
    base.save(folder/'supervisor.json',base.proc_info(process.pid))
    try:return wait_json(folder/'server-ready.json')
    except BaseException:
        (folder/'server-stop').touch(exist_ok=True)
        process.wait(timeout=20)
        raise


def client_runner(args):
    def run(argv,log,summary_path=None,mapped_download=False):
        assert summary_path=='cached' and not mapped_download
        env=base.environment();env['CPNETFLUX_SUMMARY_PATH']='cached'
        state=base.resource_state();folder=args.evidence/'client'/args.name
        process=OwnedProcess(argv,log,args.payload_root,folder,args.name=='sampled',env)
        try:
            process.release()
            while 'at_ns' not in process.exit_info:
                process.observe(state)
                assert time.monotonic_ns()-process.release_ns<120*1e9,'owned client timeout'
                time.sleep(.01)
            state['exit_code']=process.exit_info['code']
            state['wall_seconds']=(process.exit_info['at_ns']-process.release_ns)/1e9
            state['clock']='gated release to independent exit, setup excluded'
            state['profile']=process.finish_sampling()
            # Stop the peer's collector before the common helper hashes output files.
            state['peer_profile']=peer(args,'profile-stop')
            state['pswpout_delta']=base.pswpout()-state['pswpout_before'];assert state['pswpout_delta']==0
        finally:process.close()
        return state
    return run


def matrix(args):
    assert base.node()==1;args.size,args.count=4096,10000
    local=base.receipt(args.commit);remote=base.peer(args,'receipt');assert local==remote
    base.save(args.evidence/'matching-builds.json',{'R770-1':local,'R770-2':remote})
    assert not args.payload_root.exists()
    args.payload_root.mkdir();(args.payload_root/base.MARKER).write_text(base.TOKEN)
    base.peer(args,'init');rows=[]
    try:
        expected=base.peer(args,'generate',size=args.size,count=args.count)
        for name in ('reference','sampled'):
            args.name=name;started=False
            try:
                peer(args,'server-start');started=True;ready(49324)
                row=base.client_sample(args,name,expected,'2','on',49324,summary_path='cached',runner=client_runner(args))
                rows.append(row);base.save(args.evidence/'results.json',rows)
                print(json.dumps({'arm':name,'t_ready':row['t_ready_seconds'],'summary_hash':row['summary']['summary_tree_hash_seconds']}),flush=True)
            finally:
                if started:
                    server=peer(args,'server-stop');base.save(args.evidence/(name+'.server.json'),server)
                    assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0,server['resource']
                    assert server['sender_phases']['count']==10000
        checked=base.peer(args,'source-check');assert checked=={k:expected[k] for k in ('sha256','files','bytes')}
        base.save(args.evidence/'source-final-check.json',checked)
    finally:
        base.peer(args,'source-cleanup');base.peer(args,'finish')
        base.check_root(args.payload_root);(args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
    report=compare(rows)
    report['receiver_blocking']={name:summarize_blocking(json.loads((args.evidence/'client'/name/'thread-snapshots.json').read_text()))
                                 for name in ('reference','sampled')}
    base.save(args.evidence/'analysis.json',report)
    print(json.dumps({'execution':'pass','report':report,'payload_root_removed':not args.payload_root.exists()}),flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--action',required=True,choices=['matrix','server-start','server-run','server-stop','profile-stop'])
    p.add_argument('--evidence',required=True,type=Path);p.add_argument('--payload-root',required=True,type=Path)
    p.add_argument('--commit',required=True);p.add_argument('--name',choices=['reference','sampled'],default='reference')
    args=p.parse_args();args.evidence.mkdir(parents=True,exist_ok=True)
    if args.action=='matrix':matrix(args);return
    base.check_root(args.payload_root);base.receipt(args.commit)
    folder=args.evidence/'peer'/args.name
    if args.action=='server-run':server_run(args);return
    if args.action=='server-start':result=start_server(args)
    elif args.action=='profile-stop':
        (folder/'profile-stop').touch(exist_ok=False);result=wait_json(folder/'collector/profile.json')
    else:
        (folder/'server-stop').touch(exist_ok=False);result=wait_json(folder/'server-done.json')
    print(json.dumps(result))


if __name__=='__main__':main()
