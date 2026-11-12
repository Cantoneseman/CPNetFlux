#!/usr/bin/env python3
"""Generate an opt-in IPv4 TCP source-port plan from current receiver RSS snapshots.
The snapshot digest proves provenance; this tool never changes NIC configuration.
"""
import argparse,hashlib,ipaddress,json,pathlib
from diagnostics.cpnetflux_rss_queue_control_66 import rss

def generate(raw,fields,local,peer,queues,peer_ports,candidates=128,minimum=20000,maximum=60999):
 ipaddress.IPv4Address(local);ipaddress.IPv4Address(peer)
 expected=['TCP over IPV4 flows use these fields for computing Hash flow key:','IP SA','IP DA','L4 bytes 0 & 1 [TCP/UDP src port]','L4 bytes 2 & 3 [TCP/UDP dst port]']
 if [x.strip() for x in fields.splitlines() if x.strip()]!=expected:raise ValueError('IPv4 TCP four-tuple hash fields are not confirmed')
 config=rss.parse(raw)
 if config['transformation'] not in ('none','symmetric-or-xor'):raise ValueError('unsupported/unvalidated RSS transformation')
 if not 1<=len(queues)<=16 or len(set(queues))!=len(queues) or any(q not in config['indirection'] or not 0<=q<=4095 for q in queues):raise ValueError('target queues must be distinct existing queues')
 if not 1<=candidates<=128 or not 1024<=minimum<=maximum<=65535:raise ValueError('candidate bound invalid')
 peer_ports=list(peer_ports)
 if not peer_ports or len(set(peer_ports))!=len(peer_ports) or any(not 1<=p<=65535 for p in peer_ports) or len(queues)*len(peer_ports)>2048 or len(queues)*len(peer_ports)*candidates>262144:raise ValueError('plan row/candidate budget exceeded')
 fingerprint=hashlib.sha256(raw.encode()+b'\0'+fields.encode()).hexdigest()
 lines=[f'CPNETFLUX_TCP_SOURCE_PORT_PLAN_V1 {local} {peer} {len(queues)} {fingerprint}'];manifest=[]
 # Precompute each byte's Toeplitz contribution. This is offline work only.
 key=bytes.fromhex(config['key']);tables=[]
 for byte in range(12):
  table=[]
  for v in range(256):
   data=bytearray(12);data[byte]=v;table.append(rss.toeplitz(key,data))
  tables.append(table)
 for port in peer_ports:
  buckets={q:[] for q in queues}
  for source in range(minimum,maximum+1):
   data=rss.transformed(peer,local,port,source,config['transformation']);value=0
   for i,v in enumerate(data):value^=tables[i][v]
   q=config['indirection'][value&(len(config['indirection'])-1)]
   if q in buckets and len(buckets[q])<candidates:
    channel=queues.index(q);width=(maximum-minimum+1)//len(queues)
    lower=minimum+channel*width;upper=maximum if channel==len(queues)-1 else lower+width-1
    if lower<=source<=upper:buckets[q].append(source)
   if all(len(v)==candidates for v in buckets.values()):break
  for channel,q in enumerate(queues):
   pool=buckets[q]
   if len(pool)!=candidates:raise ValueError(f'insufficient candidate ports for peer-port {port} queue {q}')
   lines.append(' '.join(map(str,[channel,port,q,*pool])));manifest.append(dict(channel=channel,peer_port=port,target_queue=q,candidates=pool))
 text='\n'.join(lines)+'\n'
 if len(text.encode())>2*1024*1024:raise ValueError('plan exceeds2MiB')
 return text,dict(snapshot_sha256=fingerprint,local=local,peer=peer,queues=queues,rows=manifest,live_nic_verification=False,config=config)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for name in ['rss','fields','output']:p.add_argument('--'+name,required=True,type=pathlib.Path)
 for name in ['local','peer','queues']:p.add_argument('--'+name,required=True)
 p.add_argument('--peer-port-base',type=int,required=True);p.add_argument('--peer-port-count',type=int,required=True)
 p.add_argument('--candidates',type=int,default=128);p.add_argument('--source-port-min',type=int,default=20000);p.add_argument('--source-port-max',type=int,default=60999)
 a=p.parse_args();queues=[int(q) for q in a.queues.split(',')]
 text,report=generate(a.rss.read_text(),a.fields.read_text(),a.local,a.peer,queues,range(a.peer_port_base,a.peer_port_base+a.peer_port_count),a.candidates,a.source_port_min,a.source_port_max)
 with a.output.open('x') as f:f.write(text)
 with pathlib.Path(str(a.output)+'.json').open('x') as f:json.dump(report,f,indent=2);f.write('\n')
 print(json.dumps(dict(plan=str(a.output),bytes=len(text.encode()),rows=len(report['rows']),snapshot_sha256=report['snapshot_sha256'])))
if __name__=='__main__':main()
