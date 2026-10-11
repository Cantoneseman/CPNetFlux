#!/usr/bin/env python3
"""Bounded encrypted WAN comparison and functional ablation; evidence is written automatically."""
import argparse, hashlib, json, os, re, shutil, signal, ssl, socket, subprocess, sys, time, uuid
from pathlib import Path
CODE_SHA='b3e4dc0e8468cad4d09d7d4a47c8dc69fd15c55f'
MIN_FREE=10*1024**3
class Config:
    def __init__(self,run,build,ca,host='47.116.174.181',cp_port=22421,gsi_port=28411):
        self.run=run;self.build=build;self.ca=ca;self.host=host
        self.cp_port=cp_port;self.gsi_port=gsi_port;self.deadline=time.monotonic()+1800
        self.counter=0
    def url(self,path):return f'gsiftp://{self.host}:{self.gsi_port}/{path}'
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
def grid_command(a,source,case,profile):
    parallel,concurrency=(4,1) if profile=='single' else (1,4)
    # -rp restricts all remote paths to the peer experiment home directory.
    return ['globus-url-copy','-dcpriv','-rp','-cd','-r','-st','30',
            '-p',str(parallel),'-cc',str(concurrency),source.as_uri()+'/',a.url('grid/'+case+'/')]
def tree_command(a,source,case,channels,scheduling,summary):
    return [str(a.build/'cpnetflux-tree-upload-client'),'--host',a.host,'--port',str(a.cp_port),
        '--source-dir',str(source),'--dest-dir',case,'--connections','1',
        '--file-parallelism',str(channels),'--data-session-reuse','tree',
        '--file-scheduling',scheduling,'--checksum','crc32c','--compression','off',
        '--chunk-size','1048576','--buffer-size','65536',
        '--tls-mode','required','--tls-ca-file',str(a.ca),'--data-tls-mode','required',
        '--phase-timing','on','--json-summary',str(summary)]
def execute(a,command,label,expected=None,timeout=180):
    if time.monotonic()>=a.deadline:raise RuntimeError('experiment lifetime exhausted')
    timeout=min(timeout,max(1,a.deadline-time.monotonic()))
    path=a.run/'logs'/f'{label}.log';path.parent.mkdir(exist_ok=True)
    start=time.monotonic()
    with path.open('w') as out:
        out.write('$ '+' '.join(command)+'\n');out.flush()
        p=subprocess.Popen(command,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
        try:p.wait(timeout=timeout)
        except BaseException:
            try:os.killpg(p.pid,signal.SIGTERM);p.wait(timeout=5)
            except (ProcessLookupError,subprocess.TimeoutExpired):
                try:os.killpg(p.pid,signal.SIGKILL);p.wait(timeout=5)
                except ProcessLookupError:pass
            raise
    elapsed=time.monotonic()-start
    if expected is not None and p.returncode!=expected:raise RuntimeError(f'{label} exit={p.returncode}; see {path}')
    return {'exit':p.returncode,'wall_seconds':elapsed,'log':str(path)}
def mailbox(a,op,case='resource',arm='cp'):
    a.counter+=1;name=f'{a.counter:04d}-{uuid.uuid4().hex}.json'
    req=a.run/'mailbox'/name;req.parent.mkdir(exist_ok=True)
    req.write_text(json.dumps({'op':op,'case':case,'arm':arm}))
    execute(a,['globus-url-copy','-dcpriv','-rp','-st','15',req.as_uri(),a.url('.requests/'+name)],
            'mail-send-'+name,0,30)
    response=req.with_suffix('.response')
    for retry in range(5):
        result=execute(a,['globus-url-copy','-dcpriv','-rp','-st','15',
            a.url('.responses/'+name),response.as_uri()],'mail-get-'+name+str(retry),None,30)
        if result['exit']==0:break
        time.sleep(.2)
    else:raise RuntimeError('GSI mailbox did not return a response')
    data=json.loads(response.read_text())
    if not data.get('ok'):raise RuntimeError('peer: '+data.get('error','invalid response'))
    return data
def emit(a,result):
    result['utc']=time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())
    with (a.run/'results.jsonl').open('a') as f:f.write(json.dumps(result)+'\n');f.flush();os.fsync(f.fileno())
    print(json.dumps(result),flush=True)
def resource_gate(a):
    left=int(subprocess.check_output(['grid-proxy-info','-timeleft'],text=True).strip())
    if left<max(600,a.deadline-time.monotonic()+600):raise RuntimeError('GSI proxy lifetime too short')
    local=shutil.disk_usage(a.run).free;remote=mailbox(a,'ready')['free_bytes']
    if min(local,remote)<MIN_FREE:raise RuntimeError('disk free below 10GiB safety floor')
    return {'gsi_timeleft_seconds':left,'local_free_bytes':local,'peer_free_bytes':remote}
def dataset(a,profile):
    root=a.run/'payload'/profile;root.mkdir(parents=True,exist_ok=False)
    sizes={'preflight':[65536], 'single':[256*1048576], 'dense':[1048576]*128,
        'mixed':[129*1048576]+[4096,65536,1048576]*16,
        # sorted indices divisible by four carry heavy files: static mod assignment is deliberately skewed.
        'skew':[17*1048576 if i%4==0 else 65536 for i in range(32)],
        'range':[129*1048576]}[profile]
    block=os.urandom(1048576)
    for i,size in enumerate(sizes):
        with (root/f'f-{i:03d}.bin').open('wb') as f:
            for offset in range(0,size,len(block)):f.write(block[:min(len(block),size-offset)])
    expected=payload_hash(root)
    (a.run/f'{profile}-input.json').write_text(json.dumps({'files':expected,'total_bytes':sum(sizes)}))
    return root,expected
def validate_tree_summary(summary,count,require_ranges=False):
    if (summary.get('actual_transport')!='v3' or summary.get('actual_mode')!='persistent_tree'
        or summary.get('completed_files')!=count or summary.get('failed_files')!=0):
        raise RuntimeError('unified V3 tree identity or completion mismatch')
    ranges={f.get('range_id',0) for f in summary.get('file_timings',[]) if f.get('range_id',0)}
    if require_ranges and len(ranges)<2:raise RuntimeError('large file did not produce observed ranges')
def transfer(a,source,expected,case,arm,profile,channels=4,scheduling='dynamic',require_ranges=False):
    resource_gate(a);mailbox(a,'prepare',case,arm)
    total=sum(row[1] for row in expected);summary_path=a.run/'logs'/f'{case}.json'
    command=tree_command(a,source,case,channels,scheduling,summary_path) if arm=='cp' else grid_command(a,source,case,profile)
    measured=execute(a,command,case,0)
    peer=mailbox(a,'verify',case,arm)
    if peer['files']!=expected:raise RuntimeError(f'{case} independent SHA256 mismatch')
    result={'case':case,'arm':arm,'profile':profile,'connections_budget':4,'channels':channels,
        'scheduling':scheduling,'bytes':total,'sha256_ok':True,'committed_manifests':peer['committed_manifests'],
        'payload_mbps':total*8/measured['wall_seconds']/1000000,**measured}
    if arm=='cp':
        if peer['committed_manifests']!=len(expected):raise RuntimeError('manifest commit count mismatch')
        summary=json.loads(summary_path.read_text());validate_tree_summary(summary,len(expected),require_ranges)
        if profile=='skew' and any(f.get('range_id',0) for f in summary.get('file_timings',[])):
            raise RuntimeError('small files unexpectedly split')
        result['transport_summary']=summary
    emit(a,result);mailbox(a,'remove',case,arm)
    return result
def parse_metrics(line):
    pairs=dict(re.findall(r'([a-z_]+)=([^\s]+)',line))
    return {key:int(pairs.get(key,0)) for key in ('skipped_bytes','removed_corrupt_chunks','sent_bytes','received_bytes','loaded_verified_chunks')}
def validate_resume(metrics,size):
    if metrics['skipped_chunks']<=0 or not 0<metrics['bytes_sent']<size:
        raise RuntimeError('resume failed to skip verified blocks or reduce retransmission')
def line(sock,buffer):
    while b'\n' not in buffer:
        data=sock.recv(4096)
        if not data:raise RuntimeError('control closed')
        buffer.extend(data)
    i=buffer.index(10);value=bytes(buffer[:i+1]).decode().rstrip('\r\n');del buffer[:i+1];return value
def reply(sock,buffer):
    first=line(sock,buffer);lines=[first]
    if first[3:4]=='-':
        while not lines[-1].startswith(first[:3]+' '):lines.append(line(sock,buffer))
    return int(first[:3]),'\n'.join(lines)
def command(sock,buffer,text):
    sock.sendall((text+'\r\n').encode());return reply(sock,buffer)
def stor(a,source,case,resume_id=None,partial=False):
    ctx=ssl.create_default_context(cafile=str(a.ca))
    with ctx.wrap_socket(socket.create_connection((a.host,a.cp_port),timeout=30),server_hostname=a.host) as sock:
        sock.settimeout(180);buf=bytearray()
        if reply(sock,buf)[0]!=220:raise RuntimeError('bad TLS greeting')
        for text,code in [('USER cpnetflux',331),('PASS cpnetflux',230),('TYPE I',200)]:
            if command(sock,buf,text)[0]!=code:raise RuntimeError('control login failed')
        # Server default single-file connection count is four; tree channel count is independently negotiated.
        if resume_id and command(sock,buf,'REST GFID:'+resume_id)[0]!=350:raise RuntimeError('REST failed')
        code,epsv=command(sock,buf,'EPSV')
        if code!=229:raise RuntimeError('EPSV failed')
        port=int(re.search(r'\(\|\|\|(\d+)\|\)',epsv).group(1))
        code,start=command(sock,buf,'STOR '+case+'/f-000.bin')
        if code!=150:raise RuntimeError('STOR start failed')
        transfer_id=re.search(r'transfer_id=GFID:([A-Za-z0-9._-]+)',start).group(1)
        cmd=[str(a.build/'cpnetflux-file-client'),'--host',a.host,'--port',str(port),
            '--input',str(source),'--connections','4','--chunk-size','1048576','--buffer-size','65536',
            '--checksum','crc32c','--transfer-id',transfer_id,'--data-tls-mode','required','--tls-ca-file',str(a.ca)]
        if resume_id:cmd.append('--resume')
        if partial:cmd+=['--max-chunks','4']
        measured=execute(a,cmd,case+('-partial' if partial else '-resume'),None)
        try:
            code,end=reply(sock,buf)
        except RuntimeError:
            # A bounded fault may close the old single-session control process
            # before its 550; the peer restarts only that task-owned service.
            if not partial:raise
            code,end=550,'control closed after bounded fault'
        if partial:
            if measured['exit']==0 or code!=550:raise RuntimeError('fault attempt did not fail closed')
        elif measured['exit']!=0 or code!=226:raise RuntimeError('resume did not commit')
        return transfer_id,measured
def resume_case(a,source,expected):
    case='functional-resume';resource_gate(a);mailbox(a,'prepare',case)
    transfer_id,partial=stor(a,source/'f-000.bin',case,partial=True)
    state=mailbox(a,'state',case)
    records=sum(cp['verified_records'] for cp in state['checkpoints'])
    if records<=1 or any('state=committed\n' in cp['text'] for cp in state['checkpoints']):
        raise RuntimeError('fault did not leave multiple verified uncommitted chunks')
    damaged=mailbox(a,'corrupt',case)
    _,resumed=stor(a,source/'f-000.bin',case,transfer_id)
    state=mailbox(a,'state',case);verify=mailbox(a,'verify',case)
    if verify['files']!=expected or verify['committed_manifests']!=1:raise RuntimeError('resume hash/commit failed')
    sender=Path(resumed['log']).read_text()
    sender_line=next(line for line in sender.splitlines() if line.startswith('file_client sent_bytes='))
    sm=parse_metrics(sender_line)
    receivers=state['receiver_summaries']
    if not receivers:raise RuntimeError('receiver metrics missing')
    rm=parse_metrics(receivers[-1])
    metrics={'skipped_chunks':sm['skipped_bytes']//1048576,'removed_corrupt_chunks':rm['removed_corrupt_chunks'],
        'bytes_sent':sm['sent_bytes']}
    validate_resume(metrics,expected[0][1])
    emit(a,{'case':case,'arm':'cp','sha256_ok':True,'metrics':metrics,'partial':partial,
        'resume':resumed,'checkpoint_records_before':records,'damaged':damaged,
        'corrupt_repair_evidence':'final_sha256_after_corruption_and_sparse_resume','transfer_id':transfer_id})
    mailbox(a,'remove',case)
def preflight(a):
    resources=resource_gate(a)
    source,expected=dataset(a,'preflight')
    try:
        transfer(a,source,expected,'preflight-cp','cp','preflight',1)
        transfer(a,source,expected,'preflight-grid','grid','preflight',1)
    finally:
        shutil.rmtree(source)
    evidence={'state':'preflight_passed','code_sha':CODE_SHA,'resources':resources,
              'utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
    (a.run/'preflight.json').write_text(json.dumps(evidence));return evidence
def experiment(a):
    if not (a.run/'preflight.json').exists():raise RuntimeError('foreground preflight evidence missing')
    script_commit=subprocess.check_output(['git','-C',str(Path(__file__).resolve().parents[2]),'rev-parse','HEAD'],text=True).strip()
    provenance={'code_sha':CODE_SHA,'script_commit':script_commit,
        'binary_sha256':{name:hashlib.sha256((a.build/name).read_bytes()).hexdigest() for name in ('cpnetflux-tree-upload-client','cpnetflux-file-client')},
        'gridftp_version':(lambda p:(p.stdout+p.stderr).strip())(subprocess.run(['globus-url-copy','-version'],text=True,capture_output=True,check=False)),
        'security':'CP TLS/control+data CRC32C+manifest; GridFTP GSI PRIVATE; authentication policies differ'}
    (a.run/'provenance.json').write_text(json.dumps(provenance,indent=2))
    emit(a,{'state':'STARTED','pid':os.getpid(),**provenance,'lifetime_seconds':1800})
    (a.run/'runner.pid').write_text(str(os.getpid()))
    for profile in ('single','dense','mixed'):
        source,expected=dataset(a,profile)
        for repetition in range(3):
            order=('cp','grid') if repetition%2==0 else ('grid','cp')
            for arm in order:
                transfer(a,source,expected,f'perf-{profile}-{repetition}-{arm}',arm,profile,require_ranges=arm=='cp' and profile in ('single','mixed'))
        shutil.rmtree(source)
    source,expected=dataset(a,'skew')
    for repetition in range(3):
        modes=('static','dynamic') if repetition%2==0 else ('dynamic','static')
        for mode in modes:transfer(a,source,expected,f'functional-skew-{repetition}-{mode}','cp','skew',4,mode)
    shutil.rmtree(source)
    source,expected=dataset(a,'range')
    for channels in (1,4):
        transfer(a,source,expected,f'functional-range-{channels}','cp','range',channels,require_ranges=channels==4)
    resume_case(a,source,expected);shutil.rmtree(source)
    rows=[json.loads(line) for line in (a.run/'results.jsonl').read_text().splitlines()]
    summary={'state':'COMPLETE','code_sha':CODE_SHA,'rows':len(rows),'all_verified':True,
             'comparison_boundary':'encrypted end-to-end service modes; user authentication differs; no production readiness claim'}
    (a.run/'summary.json').write_text(json.dumps(summary,indent=2));emit(a,summary)
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--run-root',type=Path,required=True);ap.add_argument('--build-dir',type=Path,required=True)
    ap.add_argument('--ca-file',type=Path,required=True);ap.add_argument('--preflight',action='store_true')
    args=ap.parse_args();run=args.run_root.resolve()
    if not str(run).startswith('/tmp/cpnetflux-runs/EXP-V3-GRIDFTP-01/run-'):raise SystemExit('invalid run root')
    run.mkdir(parents=True,exist_ok=True)
    a=Config(run,args.build_dir.resolve(),args.ca_file.resolve())
    def stop(*_):raise RuntimeError('runner terminated')
    signal.signal(signal.SIGTERM,stop)
    try:
        if args.preflight:print(json.dumps(preflight(a)),flush=True)
        else:experiment(a)
    except BaseException as error:
        failure={'state':'FAILED','error':str(error),'code_sha':CODE_SHA}
        (run/'summary.json').write_text(json.dumps(failure));emit(a,failure);raise
if __name__=='__main__':main()
