#!/usr/bin/env python3
"""A bounded raw-TCP RSS-layout diagnostic, never a file performance result."""
import copy,hashlib,json,os,pathlib,re,signal,socket,subprocess,sys,time,unittest
import netprobe,rss
E=netprobe.E;P=netprobe.P;IP1='192.168.100.1';IP2='192.168.100.2';TARGET=134217728;BLOCK=262144
sha=lambda p:hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def save(name,value):netprobe.save(name,value)
def load(name):return json.loads((E/name).read_text())
def command(*a):return subprocess.check_output(a,text=True).strip()
def plan():
 cfg=rss.parse((E/'rss.txt').read_text());used=set();out={}
 for layout,queues in [('spread',[9,27,7,28]),('concentrated',[9]*4)]:
  rows=[]
  for i,q in enumerate(queues):
   dport=49370+i
   for sport in range(30000,32000):
    if sport in used:continue
    prediction=rss.predict(cfg,IP2,IP1,sport,dport)
    if prediction['queue']==q:used.add(sport);rows.append(dict(index=i,source_port=sport,server_port=dport,target_queue=q,prediction=prediction));break
   else:raise RuntimeError('no source port for requested queue')
  out[layout]=rows
 return out
def identity(pid):
 root=pathlib.Path('/proc')/str(pid);st=dict(l.split(':',1) for l in (root/'status').read_text().splitlines() if ':' in l)
 return dict(pid=pid,start_ticks=int((root/'stat').read_text().split(') ')[1].split()[19]),rss_bytes=int(st.get('VmRSS','0 kB').split()[0])*1024,swap_bytes=int(st.get('VmSwap','0 kB').split()[0])*1024,fd=len(list((root/'fd').iterdir())),threads=int(st['Threads']))
def preparation(node):
 conf=[l for l in (P/'build/CMakeCache.txt').read_text().splitlines() if l.startswith(('CMAKE_BUILD_TYPE:','CMAKE_CXX_COMPILER:','CMAKE_PROJECT_VERSION:','CPNETFLUX_'))]
 bins={n:sha(P/'build'/n) for n in ['cpnetflux-gridftp-server','cpnetflux-tree-download-client','cpnetflux-tree-upload-client','cpnetflux-private-tmpfs-launcher']}
 libraries={}
 for l in command('ldd','/usr/bin/iperf3').splitlines():
  m=re.search(r'=> (/[^ ]+)',l)
  if m:libraries[pathlib.Path(m[1]).name]=sha(pathlib.Path(m[1]).resolve())
 if node=='r2':
  for row in sum(list(plan().values()),[]):
   with socket.socket() as s:s.bind((IP2,row['source_port']))
 ip=IP1 if node=='r1' else IP2;peer=IP2 if node=='r1' else IP1
 assert 'eno17095np0' in command('ip','route','get',peer,'from',ip)
 receipt=dict(commit=command('git','-C',str(P),'rev-parse','HEAD'),configuration=conf,binaries=bins,iperf_sha256=sha('/usr/bin/iperf3'),iperf_version=command('iperf3','--version'),libraries=libraries,scripts={n:sha(E/n) for n in ['run66.py','rss.py','netprobe.py','rss.txt']},route=command('ip','route','get',peer,'from',ip),snapshot=netprobe.snapshot())
 assert not command('git','-C',str(P),'status','--porcelain','--untracked-files=no')
 save('receipt-'+node+'.json',receipt);return receipt
def gate():
 a=load('receipt-r1.json');b=load('receipt-r2.json')
 for key in ('commit','configuration','binaries','iperf_sha256','libraries','scripts'):assert a[key]==b[key],key
 assert a['commit']==command('git','-C',str(P),'rev-parse','HEAD')
 assert not command('git','-C',str(P),'status','--porcelain','--untracked-files=no')
 for n,d in a['binaries'].items():assert sha(P/'build'/n)==d
 for n,d in a['scripts'].items():assert sha(E/n)==d
 assert sha('/usr/bin/iperf3')==a['iperf_sha256']
 if sys.argv[2]=='r1':assert command('ethtool','-x','eno17095np0')==(E/'rss.txt').read_text().strip()
 return a

def parse_result(doc,row,node,target=TARGET):
 assert not doc.get('error'),doc.get('error');connections=doc['start']['connected'];assert len(connections)==1
 c=connections[0]
 expected=(IP2,row['source_port'],IP1,row['server_port']) if node=='r2' else (IP1,row['server_port'],IP2,row['source_port'])
 assert (c['local_host'],c['local_port'],c['remote_host'],c['remote_port'])==expected,(c,expected)
 received=doc['end']['sum_received']['bytes'];assert target<=received<=256*1024**2,received
 sent=doc['end'].get('sum_sent',{}).get('bytes') if node=='r2' else None
 if sent is not None:assert sent>=received,('sender accepted less than receiver delivered',sent,received)
 return dict(bytes=received,accepted_sent_bytes=sent,accounting_gap_bytes=sent-received if sent is not None else None,complete_file_validation=False,connection=c,receiver=doc['end']['sum_received'],sender=doc['end'].get('sum_sent'),timestamp=doc['start'].get('timestamp'),intervals=doc.get('intervals'))
def quick_network():
 root=pathlib.Path('/sys/class/net/eno17095np0/statistics');names=['rx_bytes','tx_bytes','rx_missed_errors','rx_errors','rx_dropped','tx_errors','tx_dropped']
 return {n:int((root/n).read_text()) for n in names}
def connected_count(rows,node):
 expected={(r['server_port'],r['source_port']) if node=='r1' else (r['source_port'],r['server_port']) for r in rows}
 observed=set()
 for l in pathlib.Path('/proc/net/tcp').read_text().splitlines()[1:]:
  cols=l.split()
  if cols[3]!='01':continue
  a=int(cols[1].split(':')[1],16);b=int(cols[2].split(':')[1],16)
  if (a,b) in expected:observed.add((a,b))
 return len(observed)
def run(node,layout):
 assert node in ('r1','r2') and layout in ('spread','concentrated');gate()
 rows=plan()[layout];folder=E/layout;folder.mkdir(exist_ok=False);processes=[];opened=[];failure=None;before=None;observations=[];results=[]
 def cancel(a,b):raise InterruptedError('diagnostic terminated')
 signal.signal(signal.SIGTERM,cancel)
 try:
  before=netprobe.snapshot();netprobe.resource_gate(before['resource'],before['resource'])
  (folder/'irq-before.txt').write_text(pathlib.Path('/proc/interrupts').read_text());(folder/'softirq-before.txt').write_text(pathlib.Path('/proc/softirqs').read_text())
  if node=='r1':
   listeners=command('ss','-ltnH')
   assert all((':'+str(r['server_port'])+' ') not in listeners for r in rows)
  started=time.monotonic()
  for row in rows:
   path=folder/('iperf-'+str(row['index'])+'.json');out=path.open('w');err=(folder/('iperf-'+str(row['index'])+'.stderr')).open('w');opened.extend([out,err])
   if node=='r1':cmd=['/usr/bin/iperf3','-s','-1','-B',IP1,'-p',str(row['server_port']),'--idle-timeout','60','--rcv-timeout','3000','-J']
   else:cmd=['/usr/bin/iperf3','-c',IP1,'-B',IP2,'--cport',str(row['source_port']),'-p',str(row['server_port']),'-P','1','-n',str(TARGET),'-b','12000000000','-l',str(BLOCK),'-i','0.1','--connect-timeout','2000','--snd-timeout','3000','-J']
   p=subprocess.Popen(cmd,stdout=out,stderr=err,start_new_session=True);processes.append((p,row,cmd,time.monotonic()))
  ready=False
  while True:
   active=[]
   for p,row,cmd,t0 in processes:
    if p.poll() is None:
     try:active.append(identity(p.pid))
     except (FileNotFoundError,ProcessLookupError):pass
    elif p.returncode!=0:raise RuntimeError('iperf process exited '+str(p.returncode))
   current=netprobe.resource();netprobe.resource_gate(current,before['resource']);network=quick_network()
   assert all(x['swap_bytes']==0 and x['rss_bytes']<=128*1024**2 for x in active) and sum(x['rss_bytes'] for x in active)<=512*1024**2
   assert current['pswpout']==before['resource']['pswpout']
   assert all(network[n]==before['net'][n] for n in ['rx_missed_errors','rx_errors','rx_dropped','tx_errors','tx_dropped']),'network error detected; stop group'
   assert network['rx_bytes']-before['net']['rx_bytes']<1024**3 and network['tx_bytes']-before['net']['tx_bytes']<1024**3,'wire budget watchdog'
   observations.append(dict(monotonic=time.monotonic(),active=active,resource=current,network=network,established_data_connections=connected_count(rows,node)))
   if node=='r1' and not ready:
    listeners=command('ss','-ltnH')
    if all((':'+str(r['server_port'])+' ') in listeners for r in rows):
     save(layout+'-ready.json',dict(processes=[dict(**identity(p.pid),command=cmd) for p,row,cmd,t0 in processes],rows=rows));ready=True;print('READY '+layout,flush=True)
   if not active:break
   assert time.monotonic()-started<(75 if node=='r1' else 8),'diagnostic lease expired'
   time.sleep(.01)
  for f in opened:f.flush()
  for p,row,cmd,t0 in processes:results.append(dict(index=row['index'],command=cmd,spawn_monotonic=t0,exit=p.returncode,**parse_result(json.loads((folder/('iperf-'+str(row['index'])+'.json')).read_text()),row,node)))
  assert sum(x['bytes'] for x in results)<=1024**3
 except BaseException as ex:failure=repr(ex)
 finally:
  for p,row,cmd,t0 in processes:
   if p.poll() is None:
    try:
     old=identity(p.pid);p.terminate();p.wait(3)
    except subprocess.TimeoutExpired:p.kill();p.wait(3)
  for f in opened:f.close()
  time.sleep(.3);after=netprobe.snapshot();changes=None
  try:changes=netprobe.network_gate(before,after);netprobe.resource_gate(after['resource'],before['resource'])
  except BaseException as ex:failure=(failure or '')+' final gate '+repr(ex)
  (folder/'irq-after.txt').write_text(pathlib.Path('/proc/interrupts').read_text());(folder/'softirq-after.txt').write_text(pathlib.Path('/proc/softirqs').read_text())
  queue_packets={int(m[1]):v-before['ethtool'][k] for k,v in after['ethtool'].items() if (m:=re.fullmatch(r'rx(\d+)_packets',k))} if before else {}
  save(layout+'-report-'+node+'.json',dict(node=node,layout=layout,failure=failure,before=before,after=after,network_delta=changes,observations=observations,results=results,queue_packets=queue_packets,rows=rows,benchmark=False,cpnetflux_result=False))
 print(json.dumps(dict(node=node,layout=layout,failure=failure,completed=len(results))),flush=True);return int(failure is not None)

class OracleTests(unittest.TestCase):
 def test_layout(self):
  p=plan();self.assertEqual([x['target_queue'] for x in p['spread']],[9,27,7,28]);self.assertEqual([x['target_queue'] for x in p['concentrated']],[9]*4)
  self.assertEqual(len({x['source_port'] for rows in p.values() for x in rows}),8)
 def test_live_calibration(self):
  calibration=load('calibration.json');self.assertTrue(calibration['passed']);self.assertEqual(calibration['sent_bytes'],calibration['received_bytes'])
 def test_tuple_oracle(self):
  document=load('calibration-client.json');entry=document['start']['connected'][0];row=dict(source_port=entry['local_port'],server_port=entry['remote_port'])
  entry['local_host']=IP2;entry['remote_host']=IP1
  self.assertEqual(parse_result(document,row,'r2',65536)['bytes'],65536)
  bad=copy.deepcopy(document);bad['start']['connected'][0]['local_port']+=1
  with self.assertRaises(AssertionError):parse_result(bad,row,'r2',65536)
 def test_accepted_vs_delivered(self):
  d=load('calibration-client.json');c=d['start']['connected'][0];row=dict(source_port=c['local_port'],server_port=c['remote_port']);c['local_host']=IP2;c['remote_host']=IP1
  d['end']['sum_sent']['bytes']+=16384
  observed=parse_result(d,row,'r2',65536);self.assertEqual(observed['accounting_gap_bytes'],16384);self.assertFalse(observed['complete_file_validation'])
 def test_impossible_accounting(self):
  d=load('calibration-client.json');c=d['start']['connected'][0];row=dict(source_port=c['local_port'],server_port=c['remote_port']);c['local_host']=IP2;c['remote_host']=IP1
  d['end']['sum_sent']['bytes']=1
  with self.assertRaises(AssertionError):parse_result(d,row,'r2',65536)
 def test_error_oracle(self):
  with self.assertRaises(AssertionError):parse_result(dict(error='failed authentication'),{},'r2')
if __name__=='__main__':
 mode=sys.argv[1]
 if mode=='selftest':
  result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(OracleTests));save('selftest.json',dict(passed=result.wasSuccessful(),tests=result.testsRun,errors=len(result.errors),failures=len(result.failures)));raise SystemExit(not result.wasSuccessful())
 if mode=='prepare':preparation(sys.argv[2]);save('layout-plan.json',plan());print(json.dumps(plan()));raise SystemExit(0)
 if mode=='run':raise SystemExit(run(sys.argv[2],sys.argv[3]))
 raise SystemExit('unknown mode')
