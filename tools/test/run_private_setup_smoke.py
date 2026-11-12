#!/usr/bin/env python3
"""Real bounded native supervisor lifecycle. Lab-only sudo; no cross-host performance claim."""
import argparse
import json
import os
from pathlib import Path
import pwd
import signal
import subprocess
import sys
import time
import uuid

P=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(P/'tools/perf'))
import private_tmpfs as private

def read(path):return json.loads(path.read_text())
def existing_mount_check(args):
    assert os.geteuid()==0 and os.readlink('/proc/self/ns/mnt')!=args.host_namespace
    su=pwd.getpwnam('Su');payload=args.payload_root
    assert payload.parent==Path('/dev/shm') and payload.name.startswith('cpnetflux-s0-private55-')
    assert payload.stat().st_uid==su.pw_uid and not list(payload.iterdir()) and not os.path.ismount(payload)
    subprocess.run(['mount','-t','tmpfs','-o','size=2M,uid='+str(su.pw_uid)+',gid='+str(su.pw_gid),
                    'cpnetflux-existing-mount-test',str(payload)],check=True)
    try:
        assert os.path.ismount(payload)
        command=[str(args.build_dir/'cpnetflux-private-tmpfs-launcher'),'--enable-private-tmpfs',
            '--payload-root',str(payload),'--evidence',str(args.evidence),'--mode','setup-only','--commit','',
            '--uid',str(su.pw_uid),'--gid',str(su.pw_gid),'--inode',str(payload.stat().st_ino),
            '--host-namespace',os.readlink('/proc/self/ns/mnt'),'--setup-started',str(time.monotonic())]
        result=subprocess.run(command,capture_output=True,text=True)
        assert result.returncode==2 and 'already mounted' in result.stderr,(result.returncode,result.stderr)
    finally:
        subprocess.run(['umount',str(payload)],check=True)
    return 0

def run(build):
    try:su=pwd.getpwnam('Su')
    except KeyError:return 77
    if os.geteuid()!=su.pw_uid or subprocess.run(['sudo','-n','true'],capture_output=True).returncode:
        print('SKIP: requires Su and existing noninteractive sudo');return 77
    helper=build/'cpnetflux-private-tmpfs-launcher'
    assert helper.is_file(),'native supervisor binary missing'
    evidence=private.EXPERIMENTS/('private-setup-smoke57-'+uuid.uuid4().hex)
    evidence.mkdir(mode=0o700)
    initial=private.host_state()
    modes=['setup-only','smoke','smoke-fail','smoke-timeout','smoke-cancel','smoke-descendant','smoke-v3','external-cancel']
    receipts=[]
    for mode in modes:
        folder=evidence/mode
        payload=Path('/dev/shm/cpnetflux-s0-private55-unit57-'+uuid.uuid4().hex)
        actual='smoke-timeout' if mode=='external-cancel' else mode
        command=[sys.executable,str(P/'tools/perf/private_tmpfs.py'),'--enable-private-tmpfs',
                 '--native-setup','--payload-root',str(payload),'--evidence',str(folder),'--mode',actual]
        with (evidence/(mode+'.log')).open('x') as log:
            proc=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
            try:
                if mode=='external-cancel':
                    deadline=time.monotonic()+10
                    while not (folder/'mount-ready.json').exists():
                        assert proc.poll() is None,(mode,log.name)
                        assert time.monotonic()<deadline,'mount ready timed out'
                        time.sleep(.005)
                    proc.send_signal(signal.SIGTERM)
                code=proc.wait(timeout=60)
            finally:
                if proc.poll() is None:proc.terminate();proc.wait(timeout=50)
        ns=read(folder/'namespace.json');host=read(folder/'host-after.json')
        assert not payload.exists() and host['host_unchanged'] and host['payload_root_removed'],mode
        assert private.host_state()==initial,mode
        assert ns['setup_strategy']=='native_syscalls' and ns['supervisor_euid']==0
        assert ns['uid']==su.pw_uid and ns['gid']==su.pw_gid
        assert ns['private_namespace']!=ns['host_namespace'] and ns['unmounted'] and ns['child']['reaped'],mode
        assert code==(1 if mode=='smoke-fail' else 124 if mode in ('smoke-timeout','smoke-cancel','external-cancel') else 0),(mode,code)
        if code==0:
            probe=read(folder/'probe.json')
            assert probe['actual_uid']==su.pw_uid and probe['actual_gid']==su.pw_gid
            assert probe['resuid']==[su.pw_uid]*3 and probe['resgid']==[su.pw_gid]*3
            assert probe['groups']==[] and probe['no_new_privs']==1
            assert ns['remaining_names']==[],(mode,ns['remaining_names'])
        if mode=='smoke-descendant':
            descendant=read(folder/'descendant.json')
            assert not (Path('/proc')/str(descendant['pid'])).exists(),'escaped descendant survived'
        receipts.append(dict(mode=mode,exit_code=code,unmounted=True,reaped=True,target_setup_seconds=ns['target_setup_seconds']))
    profiles=[]
    for profile in ('m2-control','m2-candidate'):
        folder=evidence/profile
        payload=Path('/dev/shm/cpnetflux-s0-private55-capacity58-'+uuid.uuid4().hex)
        command=[sys.executable,str(P/'tools/perf/private_tmpfs.py'),'--enable-private-tmpfs',
                 '--native-setup','--profile',profile,'--payload-root',str(payload),
                 '--evidence',str(folder),'--mode','setup-only']
        result=subprocess.run(command,capture_output=True,text=True,timeout=30)
        assert result.returncode==0,(profile,result.stdout,result.stderr)
        ns=read(folder/'namespace.json');host=read(folder/'host-after.json')
        spec=private.profile_spec(profile)
        assert ns['experiment_profile']==profile and ns['mount_limit_bytes']==spec['mount_limit_bytes']
        assert ns['huge_policy']==spec['huge_policy'] and ns['child']['exit_code']==0
        assert ns['unmounted'] and ns['child']['reaped'] and ns['remaining_names']==[]
        assert host['host_unchanged'] and host['payload_root_removed'] and not payload.exists()
        assert private.host_state()==initial
        profiles.append(dict(profile=profile,mount_limit_bytes=ns['mount_limit_bytes'],
                             huge_policy=ns['huge_policy'],payload_bytes=0,unmounted=True,reaped=True))
    nested=evidence/'existing-mount';nested.mkdir()
    existing=Path('/dev/shm/cpnetflux-s0-private55-existing57-'+uuid.uuid4().hex);existing.mkdir(mode=0o700)
    try:
        command=['sudo','-n','unshare','--mount','--propagation','private','--fork',sys.executable,
                 str(Path(__file__).resolve()),'--build-dir',str(build),'--existing-mount-check',
                 '--payload-root',str(existing),'--evidence',str(nested),'--host-namespace',initial['namespace']]
        result=subprocess.run(command,capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        assert private.host_state()==initial and not os.path.ismount(existing) and not list(existing.iterdir())
    finally:
        existing.rmdir()
    # Native entry rejects unsafe identities/paths before unshare or mount; no arbitrary child command.
    badroot=evidence/'negative';badroot.mkdir()
    payload=Path('/dev/shm/cpnetflux-s0-private55-negative57-'+uuid.uuid4().hex);payload.mkdir(mode=0o700)
    inode=payload.stat().st_ino
    args=['sudo','-n',str(helper),'--enable-private-tmpfs','--payload-root',str(payload),
          '--evidence',str(badroot),'--mode','setup-only','--commit','',
          '--uid',str(su.pw_uid),'--gid',str(su.pw_gid),'--inode',str(inode),
          '--host-namespace',initial['namespace'],'--setup-started',str(time.monotonic())]
    def replaced(key,value):
        result=args.copy();result[result.index(key)+1]=value;return result
    cases=[args+['--command','/bin/true'],replaced('--uid','0'),replaced('--inode',str(inode+1)),
           replaced('--payload-root','/dev/shm'),replaced('--evidence','/tmp'),
           replaced('--mode','arbitrary'),replaced('--host-namespace','mnt:[0]'),
           replaced('--setup-started','nan'),
           args+['--profile','unbounded'],
           args+['--profile','m2-control','--kernel-tcp-pacing'],
           replaced('--mode','smoke')+['--profile','m2-candidate']]
    try:
        for command in cases:
            result=subprocess.run(command,capture_output=True,text=True,timeout=10)
            assert result.returncode==2,result.stderr
            assert not list(badroot.iterdir()) and private.host_state()==initial
        guard=evidence/'guard';guard.write_text('unchanged')
        (badroot/'mount-ready.json').symlink_to(guard)
        result=subprocess.run(args,capture_output=True,text=True,timeout=10)
        assert result.returncode==1 and guard.read_text()=='unchanged'
        assert read(badroot/'namespace.json')['unmounted'] and private.host_state()==initial
    finally:
        assert payload.stat().st_ino==inode and not os.path.ismount(payload) and not list(payload.iterdir())
        payload.rmdir()
    report=dict(result='pass',lifecycle=receipts,m2_capacity_profiles=profiles,negative_cases=len(cases),symlink_receipt_rejected=True,
                host_unchanged=True,evidence=str(evidence),performance_claim=False)
    (evidence/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report));return 0

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--existing-mount-check',action='store_true')
    parser.add_argument('--payload-root',type=Path);parser.add_argument('--evidence',type=Path)
    parser.add_argument('--host-namespace')
    args=parser.parse_args()
    raise SystemExit(existing_mount_check(args) if args.existing_mount_check else run(args.build_dir.resolve()))
