#!/usr/bin/env python3
"""Explicit private mount experiment lifecycle; never changes shared /dev/shm."""
import argparse
import hashlib
import json
import mmap
import os
from pathlib import Path
import pwd
import re
import signal
import subprocess
import sys
import time
PREFIX='cpnetflux-s0-private55-'
EXPERIMENTS=Path('/home/Su/experiments/CPNetFlux')
GIB=1024**3

def save(path,value):
    path.write_text(json.dumps(value,indent=2)+'\n')

def require_enabled(enabled):
    if not enabled:raise ValueError('private tmpfs requires explicit --enable-private-tmpfs')

def validate_path(path):
    if (path.parent!=Path('/dev/shm') or path!=path.resolve() or
        not re.fullmatch(PREFIX+r'[a-zA-Z0-9-]+',path.name) or path.is_symlink()):
        raise ValueError('invalid private tmpfs mount path')
    return path

def validate_evidence(path):
    if (not path.is_absolute() or path!=path.resolve() or path.is_symlink() or
        EXPERIMENTS not in path.parents):
        raise ValueError('evidence must be a direct experiment-owned path')
    return path

def target_pmd_bytes(text,target):
    total=0;selected=False
    for line in text.splitlines():
        if re.match(r'^[0-9a-f]+-[0-9a-f]+ ',line):
            fields=line.split(None,5)
            selected=len(fields)==6 and fields[5].removesuffix(' (deleted)').startswith(str(target)+'/')
        elif selected and line.startswith('ShmemPmdMapped:'):
            total+=int(line.split()[1])*1024
    return total

def admission(gbps,**gates):
    return gbps>=40 and bool(gates) and all(value is True for value in gates.values())

def drain_group(pgid):
    deadline=time.monotonic()+5
    while True:
        live=[]
        for procdir in Path('/proc').iterdir():
            if not procdir.name.isdecimal():continue
            try:fields=(procdir/'stat').read_text().rsplit(')',1)[1].split()
            except (FileNotFoundError,ProcessLookupError):continue
            if int(fields[2])==pgid and fields[0] not in ('Z','X'):live.append(int(procdir.name))
        if not live:return
        if time.monotonic()>deadline:raise TimeoutError('owned process group did not drain: '+str(live))
        time.sleep(.005)

def run_child(command,log,timeout,drop=None,cancel_after=None,cancel_path=None,on_cancel=None):
    """Drain the owned process group before returning, including timeout/cancellation."""
    start=time.monotonic();reason='exit'
    def cancel(signum,frame):raise InterruptedError('cancelled by signal '+str(signum))
    previous={s:signal.signal(s,cancel) for s in (signal.SIGTERM,signal.SIGINT)}
    proc=None
    try:
        with log.open('x') as output:
            proc=subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT,
                                  start_new_session=True,preexec_fn=drop)
            try:
                while proc.poll() is None:
                    elapsed=time.monotonic()-start
                    if ((cancel_after is not None and elapsed>=cancel_after) or
                        (cancel_path is not None and cancel_path.exists())):
                        reason='cancelled';break
                    if elapsed>=timeout:reason='timeout';break
                    time.sleep(.01)
            except InterruptedError:reason='cancelled'
            finally:
                for s in previous:signal.signal(s,signal.SIG_IGN)
                if proc.poll() is None:
                    if on_cancel is not None:on_cancel()
                    else:os.killpg(proc.pid,signal.SIGTERM)
                    try:proc.wait(timeout=45)
                    except subprocess.TimeoutExpired:
                        os.killpg(proc.pid,signal.SIGKILL);proc.wait()
                try:os.killpg(proc.pid,signal.SIGKILL)
                except ProcessLookupError:pass
                proc.wait()
                drain_group(proc.pid)
    finally:
        for s,handler in previous.items():signal.signal(s,handler)
    return dict(exit_code=proc.returncode,reason=reason,reaped=True,
                wall_seconds=time.monotonic()-start,pid=proc.pid)

def host_state():
    return dict(namespace=os.readlink('/proc/self/ns/mnt'),
                mountinfo_sha256=hashlib.sha256(Path('/proc/self/mountinfo').read_bytes()).hexdigest(),
                shmem_policy=Path('/sys/kernel/mm/transparent_hugepage/shmem_enabled').read_text())

def namespace(args):
    owner=pwd.getpwnam('Su')
    assert os.geteuid()==0 and args.uid==owner.pw_uid and args.gid==owner.pw_gid
    assert os.readlink('/proc/self/ns/mnt')!=args.host_namespace,'not a private mount namespace'
    mp=validate_path(args.payload_root);st=mp.stat()
    assert st.st_ino==args.inode and st.st_uid==args.uid
    assert mp.is_dir() and not list(mp.iterdir()) and not os.path.ismount(mp)
    validate_evidence(args.evidence)
    assert args.evidence.is_dir()
    assert args.evidence.stat().st_uid==args.uid
    mounted=False;record=dict(host_namespace=args.host_namespace,
        private_namespace=os.readlink('/proc/self/ns/mnt'),uid=args.uid,gid=args.gid)
    started=time.monotonic()
    try:
        options=f'size=1280M,uid={args.uid},gid={args.gid},mode=0700,nosuid,nodev,huge=within_size'
        subprocess.run(['mount','-t','tmpfs','-o',options,'cpnetflux-private55',str(mp)],check=True)
        mounted=True
        record['mount_setup_seconds']=time.monotonic()-started
        record['mount_options']=subprocess.check_output(
            ['findmnt','-no','TARGET,FSTYPE,OPTIONS','--target',str(mp)],text=True).strip()
        assert 'huge=within_size' in record['mount_options'] and 'tmpfs' in record['mount_options']
        record['target_setup_seconds']=time.monotonic()-args.setup_started
        save(args.evidence/'mount-ready.json',record)
        os.chown(args.evidence/'mount-ready.json',args.uid,args.gid)
        def drop():
            os.setgroups([]);os.setgid(args.gid);os.setuid(args.uid)
        if args.mode=='v3':
            script=Path(__file__).with_name('run_private_tmpfs_v3.py')
            command=[sys.executable,str(script),'--action','run','--evidence',str(args.evidence),
                     '--payload-root',str(mp),'--commit',args.commit,'--enable-private-tmpfs']
            if args.kernel_tcp_pacing:command.append('--kernel-tcp-pacing')
        else:
            command=[sys.executable,str(Path(__file__).resolve()),'--action','probe',
                     '--payload-root',str(mp),'--evidence',str(args.evidence),'--mode',args.mode]
        record['child']=run_child(command,args.evidence/'child.log',
            .3 if args.mode=='smoke-timeout' else 600,drop=drop,
            cancel_after=.3 if args.mode=='smoke-cancel' else None,
            cancel_path=args.evidence/'cancel.request')
        record['remaining_names']=sorted(x.name for x in mp.iterdir())
    except BaseException as error:
        record['error']=repr(error)
        raise
    finally:
        if mounted:
            subprocess.run(['umount',str(mp)],check=True)
            record['unmounted']=not os.path.ismount(mp)
        else:record['unmounted']=False
        save(args.evidence/'namespace.json',record)
        os.chown(args.evidence/'namespace.json',args.uid,args.gid)
    return record['child']['exit_code'] if record['child']['reason']=='exit' else 124

def probe(args):
    assert os.getuid()==pwd.getpwnam('Su').pw_uid and os.geteuid()!=0
    path=args.payload_root/'probe.bin'
    with path.open('x+b') as f:
        f.truncate(64*1024**2)
        with mmap.mmap(f.fileno(),0) as mapping:
            block=os.urandom(262144)
            for at in range(0,len(mapping),len(block)):mapping[at:at+len(block)]=block
            proof=target_pmd_bytes(Path('/proc/self/smaps').read_text(),args.payload_root)
            assert proof>0,'no actual target huge PMD mapping'
            if args.mode=='smoke-fail':raise RuntimeError('intentional lifecycle failure')
            if args.mode in ('smoke-timeout','smoke-cancel'):time.sleep(30)
            with path.open('rb') as reader:
                for _ in range(256):assert reader.read(len(block))==block
    path.unlink()
    if args.mode=='smoke-v3':
        project=Path(__file__).resolve().parents[2]
        env={k:v for k,v in os.environ.items() if not k.startswith('CPNETFLUX_')}
        env.update(TMPDIR=str(args.payload_root))
        subprocess.run([sys.executable,str(project/'tools/test/run_cpnetflux_mapped_profile_smoke.py'),
                        '--build-dir',str(project/'build'),'--profile','0','--observation','off'],
                       env=env,check=True,timeout=180)
    save(args.evidence/'probe.json',dict(result='pass',actual_uid=os.getuid(),bytes=64*1024**2,
                                       shmem_pmd_mapped_bytes=proof))

def launch(args):
    require_enabled(args.enable_private_tmpfs)
    assert os.geteuid()!=0,'launch as Su'
    owner=pwd.getpwnam('Su');assert os.getuid()==owner.pw_uid
    mp=validate_path(args.payload_root)
    args.evidence=validate_evidence(args.evidence)
    args.evidence.mkdir(parents=True,exist_ok=False)
    before=host_state();save(args.evidence/'host-before.json',before)
    setup_started=time.monotonic()
    mp.mkdir(mode=0o700);st=mp.stat()
    command=['sudo','-n','unshare','--mount','--propagation','private','--fork',
        sys.executable,str(Path(__file__).resolve()),'--action','namespace',
        '--payload-root',str(mp),'--evidence',str(args.evidence),'--mode',args.mode,
        '--uid',str(owner.pw_uid),'--gid',str(owner.pw_gid),'--inode',str(st.st_ino),
        '--host-namespace',before['namespace'],'--commit',args.commit,
        '--setup-started',str(setup_started)]
    if args.kernel_tcp_pacing:command.append('--kernel-tcp-pacing')
    try:
        result=run_child(command,args.evidence/'launcher.log',720,
                         on_cancel=lambda:(args.evidence/'cancel.request').touch(exist_ok=True))
        save(args.evidence/'launcher.json',result)
        return result['exit_code'] if result['reason']=='exit' else 124
    finally:
        after=host_state()
        host_unchanged=before==after
        assert mp.stat().st_ino==st.st_ino and mp.stat().st_uid==owner.pw_uid
        assert not os.path.ismount(mp) and not list(mp.iterdir())
        mp.rmdir()
        save(args.evidence/'host-after.json',dict(**after,payload_root_removed=not mp.exists(),host_unchanged=host_unchanged))
        assert host_unchanged,'host mount/global shmem state changed'
        if args.mode=='v3' and (args.evidence/'analysis.json').exists():
            report=json.loads((args.evidence/'analysis.json').read_text())
            ns=json.loads((args.evidence/'namespace.json').read_text())
            gates=dict(report['gates'],cleanup=ns.get('unmounted') is True and not mp.exists(),
                       no_failures=not report['failures'],child_ok=ns['child']['exit_code']==0)
            save(args.evidence/'acceptance.json',dict(gates=gates,
                formal_admission=admission(report.get('complete_target_task_gbps',0),**gates),
                complete_target_task_gbps=report.get('complete_target_task_gbps'),
                m2_pass=False,scope='single feasibility only'))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--action',choices=['launch','namespace','probe'],default='launch')
    p.add_argument('--enable-private-tmpfs',action='store_true')
    p.add_argument('--payload-root',type=Path,required=True)
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--mode',choices=['v3','smoke','smoke-fail','smoke-timeout','smoke-cancel','smoke-v3'],default='v3')
    p.add_argument('--kernel-tcp-pacing',action='store_true')
    p.add_argument('--commit',default='')
    p.add_argument('--uid',type=int);p.add_argument('--gid',type=int);p.add_argument('--inode',type=int)
    p.add_argument('--host-namespace')
    p.add_argument('--setup-started',type=float)
    args=p.parse_args()
    if args.kernel_tcp_pacing and args.mode!='v3':p.error('kernel pacing requires V3 mode')
    if args.action=='namespace':return namespace(args)
    if args.action=='probe':probe(args);return 0
    return launch(args)

if __name__=='__main__':sys.exit(main())
