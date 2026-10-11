#!/usr/bin/env python3
"""Experiment peer controlled by an authenticated GSI mailbox; no extra credentials."""
import argparse, hashlib, json, os, re, signal, subprocess, time, shutil, pwd
from pathlib import Path

def payload_hash(root):
    entries=[]
    for p in sorted(root.rglob('*')):
        if not p.is_file() or p.is_symlink():continue
        name=p.relative_to(root).as_posix()
        if '.cpnetflux.' in name or '.part.' in name:continue
        h=hashlib.sha256()
        with p.open('rb') as f:
            for block in iter(lambda:f.read(1048576),b''):h.update(block)
        entries.append([name,p.stat().st_size,h.hexdigest()])
    return entries

def safe_case(name):
    if not isinstance(name,str) or not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,80}',name):
        raise ValueError('invalid case name')
    return name

def operation(run,request):
    name=safe_case(request['case']);arm=request.get('arm','cp')
    if arm not in ('cp','grid'):raise ValueError('invalid arm')
    root=run/arm/name
    if root.is_symlink() or not root.resolve().is_relative_to(run/arm):raise ValueError('unsafe root')
    op=request['op']
    if op=='prepare':
        root.mkdir(exist_ok=False)
        if arm=='grid':
            owner=(run/'grid').stat();os.chown(root,owner.st_uid,owner.st_gid);os.chmod(root,0o750)
        return {'prepared':name}
    if op=='verify':
        manifests=list(root.rglob('*.cpnetflux.manifest'))
        return {'files':payload_hash(root),'committed_manifests':sum(
            'state=committed\n' in p.read_text() for p in manifests),'manifests':len(manifests)}
    if op=='state':
        checkpoints=[]
        for m in root.rglob('*.cpnetflux.manifest'):
            text=m.read_text()
            checkpoints.append({'manifest':m.relative_to(root).as_posix(),
                'verified_records':sum(len(line.partition('=')[2].split(',')) for line in text.splitlines() if line.startswith('verified_chunks=') and line.partition('=')[2]),
                'text':text})
        lines=(run/'services.log').read_text(errors='replace').splitlines() if (run/'services.log').exists() else []
        return {'checkpoints':checkpoints,'receiver_summaries':[line for line in lines if line.startswith('file_server received_bytes=')][-20:]}
    if op=='corrupt':
        candidates=list(root.rglob('*.part.*'))
        if len(candidates)!=1:raise ValueError('expected one partial')
        manifests=list(root.rglob('*.cpnetflux.manifest'))
        if len(manifests)!=1:raise ValueError('expected one manifest')
        records=[line.partition('=')[2] for line in manifests[0].read_text().splitlines() if line.startswith('verified_chunks=')]
        if not records or not records[0]:raise ValueError('no verified chunks to corrupt')
        offset=int(records[0].split(',')[0].split(':')[1])
        with candidates[0].open('r+b') as f:
            f.seek(offset);value=f.read(1)
            if not value:raise ValueError('empty partial')
            f.seek(offset);f.write(bytes([value[0]^255]));f.flush();os.fsync(f.fileno())
        return {'corrupted_offset':offset}
    if op=='remove':
        if root.exists():shutil.rmtree(root)
        return {'removed':name}
    if op=='ready':
        return {'free_bytes':shutil.disk_usage(run).free,'run':str(run)}
    raise ValueError('unknown operation')

def main():
    ap=argparse.ArgumentParser()
    for key in ('run-root','binary','cert','key'):ap.add_argument('--'+key,type=Path,required=True)
    ap.add_argument('--cp-port',type=int,default=22421)
    ap.add_argument('--gsi-port',type=int,default=28411)
    ap.add_argument('--ttl',type=int,default=2400)
    ap.add_argument('--gsi-user',default='gridftp')
    a=ap.parse_args();run=a.run_root.resolve()
    if not str(run).startswith('/tmp/cpnetflux-runs/EXP-V3-GRIDFTP-01/'):raise SystemExit('invalid run root')
    if not 60<=a.ttl<=3600:raise SystemExit('invalid lifetime')
    for folder in ('cp','grid','.requests','.responses'):(run/folder).mkdir(parents=True,exist_ok=True)
    mapped=pwd.getpwnam(a.gsi_user)
    for folder in ('grid','.requests'):
        os.chown(run/folder,mapped.pw_uid,mapped.pw_gid);os.chmod(run/folder,0o750)
    log=(run/'services.log').open('a')
    commands=[
      [str(a.binary),'--host','0.0.0.0','--port',str(a.cp_port),'--root',str(run/'cp'),
       '--data-port-base','38000','--connections','4','--checksum','crc32c',
       '--tls-mode','required','--tls-cert-file',str(a.cert),'--tls-key-file',str(a.key),
       '--data-tls-mode','required'],
      ['/usr/sbin/globus-gridftp-server','-p',str(a.gsi_port),'-allow-root',
       '-home-dir',str(run),'-restrict-paths','RW'+str(run),'-port-range','36000,36511',
       '-data-interface','47.116.174.181',
       '-no-detach','-l',str(run/'gsi-server.log'),'-Z',str(run/'gsi-transfers.log')]
    ]
    env=os.environ.copy();env['X509_USER_CERT']='/etc/grid-security/hostcert.pem'
    env['X509_USER_KEY']='/etc/grid-security/hostkey.pem'
    processes=[]
    def terminated(*_):raise RuntimeError('peer terminated')
    signal.signal(signal.SIGTERM,terminated)
    deadline=time.monotonic()+a.ttl
    restarts=0
    try:
        for cmd in commands:processes.append(subprocess.Popen(cmd,stdout=log,stderr=subprocess.STDOUT,
            env=env,start_new_session=True))
        (run/'peer-pids.json').write_text(json.dumps({'pid':os.getpid(),'services':[p.pid for p in processes]}))
        print(json.dumps({'state':'peer_started','pid':os.getpid(),'ttl_seconds':a.ttl}),flush=True)
        while time.monotonic()<deadline:
            # A deliberately aborted CP data session can terminate the single-session
            # server process. Restart only that task-owned service; GSI stays intact.
            if processes[1].poll() is not None:raise RuntimeError('GSI peer service exited')
            if processes[0].poll() is not None:
                if restarts>=8:raise RuntimeError('CP peer service restart limit exceeded')
                processes[0]=subprocess.Popen(commands[0],stdout=log,stderr=subprocess.STDOUT,
                    env=env,start_new_session=True);restarts+=1
                (run/'peer-restarts.json').write_text(json.dumps({'count':restarts}))
            for request in sorted((run/'.requests').glob('*.json')):
                if request.stat().st_size>4096:continue
                response=run/'.responses'/request.name
                if response.exists():continue
                try:body=json.loads(request.read_text())
                except (OSError,json.JSONDecodeError):continue
                try:result={'ok':True,**operation(run,body)}
                except Exception as error:result={'ok':False,'error':str(error)}
                temp=response.with_suffix('.tmp');temp.write_text(json.dumps(result));temp.replace(response)
            time.sleep(.05)
    finally:
        for p in processes:
            if p.poll() is None:
                try:os.killpg(p.pid,signal.SIGTERM);p.wait(timeout=5)
                except (ProcessLookupError,subprocess.TimeoutExpired):
                    try:os.killpg(p.pid,signal.SIGKILL)
                    except ProcessLookupError:pass
        log.close()
if __name__=='__main__':main()
