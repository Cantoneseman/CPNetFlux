import json,pathlib,re,subprocess,time
E=pathlib.Path('/home/Su/experiments/CPNetFlux/2026-11-12-rss-queue-control-66')
P=pathlib.Path('/home/Su/projects/CPNetFlux')
def save(name,value):(E/name).write_text(json.dumps(value,indent=2)+chr(10))

def resource():
 mem={l.split(':')[0]:int(l.split()[1])*1024 for l in pathlib.Path('/proc/meminfo').read_text().splitlines()}
 st={l.split(':')[0]:l.split(':')[1].strip() for l in pathlib.Path('/proc/self/status').read_text().splitlines() if ':' in l}
 vm=dict(l.split() for l in pathlib.Path('/proc/vmstat').read_text().splitlines())
 return dict(mem_available_bytes=mem['MemAvailable'],mem_free_bytes=mem['MemFree'],pswpout=int(vm['pswpout']),
  rss_bytes=int(st['VmRSS'].split()[0])*1024,swap_bytes=int(st['VmSwap'].split()[0])*1024,
  fd_count=len(list(pathlib.Path('/proc/self/fd').iterdir())),threads=int(st['Threads']))
def resource_gate(value,baseline):
 assert value['mem_available_bytes']>=12*1024**3 and value['swap_bytes']==0 and value['rss_bytes']<=64*1024**2
 assert value['pswpout']==baseline['pswpout']
def snapshot():
 c=subprocess.run(['ethtool','-S','eno17095np0'],capture_output=True,text=True,check=True)
 eth={}
 for l in c.stdout.splitlines():
  m=re.fullmatch(r'\s*([\w]+):\s*(\d+)\s*',l)
  if m:eth[m[1]]=int(m[2])
 net={f.name:int(f.read_text()) for f in pathlib.Path('/sys/class/net/eno17095np0/statistics').iterdir()}
 hca={}
 for f in pathlib.Path('/sys/class/infiniband').glob('*/ports/1/*/*'):
  if f.parent.name not in ('counters','hw_counters'):continue
  try:hca[str(f.relative_to('/sys/class/infiniband'))]=int(f.read_text())
  except (OSError,ValueError):pass
 return dict(monotonic=time.monotonic(),ethtool=eth,net=net,hca=hca,resource=resource())
def network_gate(before,after):
 names=('rx_missed_errors','rx_errors','rx_dropped','rx_crc_errors','rx_fifo_errors','tx_errors','tx_dropped')
 delta={k:after['net'][k]-before['net'][k] for k in names}
 delta['rx_out_of_buffer']=after['ethtool']['rx_out_of_buffer']-before['ethtool']['rx_out_of_buffer']
 delta['hca_errors']={k:v-before['hca'].get(k,v) for k,v in after['hca'].items() if any(x in k for x in ('error','discard','out_of_buffer','symbol','link_downed')) and v!=before['hca'].get(k,v)}
 assert all(v==0 for k,v in delta.items() if k!='hca_errors') and not delta['hca_errors'],delta
 return delta
