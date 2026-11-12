#!/usr/bin/env python3
"""Offline, read-only IPv4 TCP RSS prediction. Must validate against the actual receiver."""
import ipaddress,re,struct
def toeplitz(key,data):
 if len(key)*8<len(data)*8+31:raise ValueError('RSS key too short')
 bits=int.from_bytes(key,'big');width=len(key)*8;value=0
 for index in range(len(data)*8):
  if data[index//8] & (1 << (7-index%8)):value^=(bits>>(width-32-index))&0xffffffff
 return value
def transformed(src,dst,sport,dport,mode):
 a=int(ipaddress.IPv4Address(src));b=int(ipaddress.IPv4Address(dst))
 if not (0<sport<=65535 and 0<dport<=65535):raise ValueError('TCP port outside range')
 if mode=='none':return struct.pack('!IIHH',a,b,sport,dport)
 if mode=='symmetric-xor':return struct.pack('!IIHH',a^b,a^b,sport^dport,sport^dport)
 if mode=='symmetric-or-xor':return struct.pack('!IIHH',a|b,a^b,sport|dport,sport^dport)
 raise ValueError('unsupported RSS input transformation')
def parse(text):
 table=[]
 for line in text.splitlines():
  m=re.fullmatch(r'\s*(\d+):\s+((?:\d+\s*)+)',line)
  if m:
   if int(m[1])!=len(table):raise ValueError('noncontiguous RSS table')
   table.extend(int(x) for x in m[2].split())
 if not table or len(table)&(len(table)-1):raise ValueError('non-power-of-two RSS table')
 after=text.split('RSS hash key:\n',1)
 if len(after)!=2:raise ValueError('missing RSS key')
 key=bytes.fromhex(after[1].splitlines()[0].strip().replace(':',''))
 function=text.split('RSS hash function:',1)[-1].split('RSS input transformation:',1)[0]
 enabled=re.findall(r'^\s*([\w-]+):\s*on\s*$',function,re.M)
 if enabled!=['toeplitz']:raise ValueError('unsupported RSS hash function')
 if 'RSS input transformation:' not in text:raise ValueError('unknown RSS transformation, refusing inference')
 active=re.findall(r'^\s*([\w-]+):\s*on\s*$',text.split('RSS input transformation:',1)[1],re.M)
 if len(active)>1 or any(x not in ('symmetric-xor','symmetric-or-xor') for x in active):raise ValueError('unsupported RSS transformation')
 return dict(key=key.hex(),indirection=table,transformation=active[0] if active else 'none')
def predict(config,src,dst,sport,dport):
 data=transformed(src,dst,sport,dport,config['transformation'])
 value=toeplitz(bytes.fromhex(config['key']),data);index=value&(len(config['indirection'])-1)
 return dict(hash_hex=f'{value:08x}',index=index,queue=config['indirection'][index],transformed_bytes_hex=data.hex())
