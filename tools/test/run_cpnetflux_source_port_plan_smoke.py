#!/usr/bin/env python3
"""Real V3 files, port exhaustion, repeated tasks, TLS and lane-local reconnect."""
import argparse,hashlib,json,pathlib,socket,subprocess,tempfile
from cpnetflux_fault_proxy import FaultProxy
from gridftp_port_window import clamp_passive_data_port_base
from run_cpnetflux_lane_recovery_smoke import make_tls_files
from run_cpnetflux_lane_recovery_interop import wait_for_tcp
from tree_smoke_common import free_port,stop_server,tree_hash

def free_pool(count):
 sockets=[];ports=[]
 try:
  for i in range(count):
   s=socket.socket();s.bind(('127.0.0.1',0));sockets.append(s);ports.append(s.getsockname()[1])
 finally:
  for s in sockets:s.close()
 return ports

def run_case(build,root,name,tls=False,fault=None,repetitions=1,invalid=None):
 case=root/name;case.mkdir();server_root=case/'server';source=server_root/'dataset';source.mkdir(parents=True)
 (source/'large.bin').write_bytes(bytes(range(256))*8192)
 (source/'small.bin').write_bytes(bytes(range(255,-1,-1))*4096)
 expected=tree_hash(source);control=free_port();base=clamp_passive_data_port_base(free_port());pools=[free_pool(64),free_pool(64)]
 # Keep one candidate occupied for channel0 to prove bounded skip in full runtime.
 blocker=socket.socket();blocker.bind(('127.0.0.1',0));occupied=blocker.getsockname()[1];pools[0].insert(0,occupied)
 peer='127.0.0.2' if fault or invalid=='deadline' else '127.0.0.1';sha='a'*64
 lines=[f'CPNETFLUX_TCP_SOURCE_PORT_PLAN_V1 127.0.0.1 {peer} 2 {sha}']
 for c in range(2):
  for port in range(base,base+16):
   pool=[occupied] if invalid=='exhausted' else pools[c]
   if invalid=='missing-row':continue
   lines.append(' '.join(map(str,[c,port,9+c,*pool])))
 # A missing-row plan must still describe both channels, just for an unrelated remote port.
 if invalid=='missing-row':lines.extend([f'0 1 9 {pools[0][-1]}',f'1 1 10 {pools[1][-1]}'])
 plan=case/'plan.txt';plan.write_text('\n'.join(lines)+'\n');cert,key=make_tls_files(case);proxy=None;records=[]
 mode='required' if tls else 'off'
 command=[str(build/'cpnetflux-gridftp-server'),'--host','127.0.0.1','--port',str(control),'--data-port-base',str(base),'--root',str(server_root),'--connections','2','--checksum','crc32c','--tls-mode',mode,'--data-tls-mode',mode,'--tls-cert-file',str(cert),'--tls-key-file',str(key)]
 with (case/'server.log').open('w') as f:server=subprocess.Popen(command,stdout=f,stderr=subprocess.STDOUT)
 try:
  wait_for_tcp('127.0.0.1',control)
  if fault or invalid=='deadline':proxy=FaultProxy(peer,'127.0.0.1',control,base,drops=1 if fault else 0,drop_bytes=512*1024,delay=.01 if invalid=='deadline' else .0001,target_data_connections=[1,2],drop_control_after_data_connection=1 if fault=='control-reset' else None,log=case/'proxy.jsonl')
  last=set()
  for index in range(repetitions):
   output=case/('output-'+str(index));summary=case/('summary-'+str(index)+'.json')
   command=[str(build/'cpnetflux-tree-download-client'),'--host',peer,'--local-bind-address','127.0.0.1','--port',str(control),'--source-dir','dataset','--dest-dir',str(output),'--connections','2','--file-parallelism','2','--control-reuse','worker','--checksum','crc32c','--reconnect-attempts','2' if fault else '0','--transfer-timeout-ms','50' if invalid=='deadline' else '10000','--tcp-data-source-port-plan',str(plan),'--tls-mode',mode,'--data-tls-mode',mode,'--json-summary',str(summary)]
   if tls:command+=['--tls-ca-file',str(cert),'--tls-server-name','localhost']
   result=subprocess.run(command,capture_output=True,text=True,timeout=15);(case/('client-'+str(index)+'.log')).write_text(result.stdout+result.stderr)
   s=json.loads(summary.read_text());assert s['actual_transport']=='v3'
   if invalid:
    assert result.returncode!=0 and s['result']=='fail',s
    if invalid=='deadline':
     assert any(x in result.stderr+json.dumps(s) for x in ['deadline','timeout','timed out','retry']),s
     for ep in s['lane_endpoints']:
      if ep['data_local_port']:assert ep['data_local_port'] in pools[ep['channel_index']]
    else:assert ('exhausted' if invalid=='exhausted' else 'no channel/peer-port row') in result.stderr+json.dumps(s),(result.stderr,s)
   else:
    assert result.returncode==0 and s['result']=='pass',(name,result.stderr,s)
    assert tree_hash(output)==expected;assert s['tcp_source_plan_snapshot_sha256']==sha
    assert len(s['lane_endpoints'])==2
    selected=set()
    for ep in s['lane_endpoints']:
     c=ep['channel_index'];assert ep['planned_rx_queue']==9+c and ep['data_local_port'] in pools[c] and ep['data_local_port']!=occupied
     assert ep['source_port_bind_attempts']>=ep['data_connections']+(c==0)
     selected.add(ep['data_local_port'])
    # Active client ports from preceding tasks remain allocated/TIME_WAIT, not silently reused.
    if last:assert not last.intersection(selected),(last,selected)
    last|=selected
    if fault:assert s['lane_local_recovery_count']>=1 and sum(ep['data_connections'] for ep in s['lane_endpoints'])>=3,s
   records.append(dict(summary=s,exit=result.returncode))
  if fault:
   events=[json.loads(l) for l in (case/'proxy.jsonl').read_text().splitlines()]
   assert any(x['event'] in ('reset','control_reset') for x in events)
  return dict(case=name,expected_failure=invalid,records=records)
 finally:
  if proxy:proxy.close()
  stop_server(server,case/'server.log');blocker.close();key.unlink(missing_ok=True)

def main():
 p=argparse.ArgumentParser();p.add_argument('--build-dir',type=pathlib.Path,required=True);p.add_argument('--evidence-dir',type=pathlib.Path);a=p.parse_args()
 with tempfile.TemporaryDirectory(prefix='cpnetflux-source-plan-') as temporary:
  root=a.evidence_dir or pathlib.Path(temporary)
  if a.evidence_dir:root.mkdir(exist_ok=False)
  rows=[run_case(a.build_dir.absolute(),root,name,**kw) for name,kw in [('repeat',dict(repetitions=3)),('tls',dict(tls=True)),('data-reset',dict(fault='data-reset')),('control-reset',dict(fault='control-reset')),('exhausted',dict(invalid='exhausted')),('missing-row',dict(invalid='missing-row')),('deadline',dict(invalid='deadline'))]]
  (root/'result.json').write_text(json.dumps(dict(passed=True,cases=rows),indent=2)+'\n');print(json.dumps(dict(passed=True,cases=len(rows))))
if __name__=='__main__':main()
