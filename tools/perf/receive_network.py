#!/usr/bin/env python3
"""Read-only mlx5/CPU/TCP evidence; counters have interface or host scope, never flow causality."""
import json
import os
from pathlib import Path
import re
import subprocess
import time

IFACE='eno17095np0'
PCI='0000:0b:00.0'


def command(*args):
    result=subprocess.run(args,capture_output=True,text=True,timeout=5)
    if result.returncode:raise RuntimeError(f'{args}: {result.stderr}')
    return result.stdout


def parse_ethtool(text):
    return {m[1]:int(m[2]) for line in text.splitlines()
            if (m:=re.fullmatch(r'\s*([A-Za-z0-9_]+):\s*(\d+)\s*',line))}


def delta(before,after):
    missing=before.keys()-after.keys()
    if missing:raise ValueError(f'missing counters: {sorted(missing)}')
    result={key:after[key]-value for key,value in before.items()}
    if any(value<0 for value in result.values()):raise ValueError('counter regression')
    return result


def parse_interrupts(text,pci=PCI):
    lines=text.splitlines();cpus=re.findall(r'CPU(\d+)',lines[0]);result={}
    for line in lines[1:]:
        if pci not in line or not (match:=re.search(r'mlx5_comp(\d+)@',line)):continue
        irq,body=line.split(':',1);values=body.split()[:len(cpus)]
        result[match[1]]=dict(irq=int(irq),cpus={cpu:int(value) for cpu,value in zip(cpus,values)})
    return result


def parse_softnet(text):
    result={}
    for index,line in enumerate(text.splitlines()):
        values=[int(x,16) for x in line.split()]
        cpu=values[12] if len(values)>=13 else index
        result[str(cpu)]=dict(processed=values[0],dropped=values[1],time_squeeze=values[2])
    return result


def parse_netstat(text):
    lines=text.splitlines();result={}
    if len(lines)%2:raise ValueError('unpaired netstat columns')
    for index in range(0,len(lines),2):
        names,values=lines[index].split(),lines[index+1].split()
        if len(names)!=len(values) or names[0]!=values[0]:raise ValueError('netstat columns mismatch')
        result.update({names[0].rstrip(':')+'.'+key:int(value) for key,value in zip(names[1:],values[1:])})
    return result


def cpu_snapshot():
    return {parts[0][3:]:[int(x) for x in parts[1:9]] for line in Path('/proc/stat').read_text().splitlines()
            if (parts:=line.split()) and re.fullmatch(r'cpu\d+',parts[0])}


def thread_snapshot(pid):
    result={}
    for thread in (Path('/proc')/str(pid)/'task').glob('*'):
        try:
            raw=(thread/'stat').read_text();fields=raw.rsplit(')',1)[1].split()
            result[thread.name]=dict(name=raw.split('(',1)[1].rsplit(')',1)[0],
                user_ticks=int(fields[11]),system_ticks=int(fields[12]),last_cpu=int(fields[36]),
                start_ticks=int(fields[19]),state=fields[0])
        except (FileNotFoundError,ProcessLookupError):continue
    return result


def snapshot(pid=None,full=True):
    started=time.monotonic()
    result=dict(started_monotonic=started,cpu=cpu_snapshot(),
        softnet=parse_softnet(Path('/proc/net/softnet_stat').read_text()),
        interrupts=parse_interrupts(Path('/proc/interrupts').read_text()),
        netdev={p.name:int(p.read_text()) for p in (Path('/sys/class/net')/IFACE/'statistics').iterdir()})
    result['tcp']=parse_netstat(Path('/proc/net/netstat').read_text())
    result['tcp'].update(parse_netstat(Path('/proc/net/snmp').read_text()))
    if full:result['ethtool']=parse_ethtool(command('ethtool','-S',IFACE))
    if pid is not None:
        result['threads']=thread_snapshot(pid)
        # Record the exact process and TCP endpoints, not unrelated host sockets.
        lines=command('ss','-tinp').splitlines();selected=[]
        for index,line in enumerate(lines):
            if f'pid={pid},' in line:selected+=lines[index:index+2]
        result['owned_ss']=selected
    result['ended_monotonic']=time.monotonic()
    result['collection_seconds']=result['ended_monotonic']-started
    return result


def inventory():
    result={key:command('ethtool',option,IFACE) for key,option in
            [('driver','-i'),('channels','-l'),('ring','-g'),('coalesce','-c'),('rss','-x'),('offload','-k')]}
    result['irq_affinity']={p.parent.name:p.read_text().strip() for p in Path('/proc/irq').glob('*/effective_affinity_list')
                            if (Path('/sys/class/net')/IFACE/'device/msi_irqs'/p.parent.name).exists()}
    result['rps']={p.parent.name:p.read_text().strip() for p in (Path('/sys/class/net')/IFACE/'queues').glob('rx-*/rps_cpus')}
    result['cpu_topology']=command('lscpu','-p=CPU,CORE,SOCKET,NODE')
    result['kernel']=command('uname','-r').strip()
    return result


RX_QUEUE_COUNTER_RE=re.compile(
    r'rx(\d+)_(packets|wqe_err|buff_alloc_err|pp_alloc_(?:fast|slow|slow_high_order|empty|refill))')

def rx_queue_counter_deltas(counters):
    queues={}
    for key,value in counters.items():
        match=RX_QUEUE_COUNTER_RE.fullmatch(key)
        if match:queues.setdefault(match[1],{})[match[2]]=value
    return {queue:dict(sorted(values.items()))
            for queue,values in sorted(queues.items(),key=lambda item:int(item[0]))
            if any(values.values())}


def analyze(before,after):
    for side in (before,after):
        if 'rx_out_of_buffer' not in side.get('ethtool',{}) or 'rx_missed_errors' not in side.get('netdev',{}):
            raise ValueError('missing required receive counters')
    # Some ethtool fields are live percentage gauges, not monotonic counters.
    eth_keys=[key for key in before['ethtool'] if key not in
              ('outbound_pci_stalled_rd','outbound_pci_stalled_wr')]
    eth=delta({k:before['ethtool'][k] for k in eth_keys},after['ethtool'])
    dev=delta(before['netdev'],after['netdev'])
    soft={cpu:delta(old,after['softnet'][cpu]) for cpu,old in before['softnet'].items()}
    gauge={'Tcp.CurrEstab','Tcp.RtoAlgorithm','Tcp.RtoMin','Tcp.RtoMax','Tcp.MaxConn',
           'Ip.Forwarding','Ip.DefaultTTL'}
    tcp=delta({k:v for k,v in before['tcp'].items() if k not in gauge},after['tcp'])
    active={match[1]:value for key,value in eth.items()
            if value and (match:=re.fullmatch(r'rx(\d+)_packets',key))}
    queue_deltas=rx_queue_counter_deltas(eth)
    result=dict(layer='hardware_receive_queue_no_buffer' if eth['rx_out_of_buffer']>0 else 'not_reproduced',
        rx_out_of_buffer_delta=eth['rx_out_of_buffer'],rx_missed_errors_delta=dev['rx_missed_errors'],
        active_rx_queues=active,flow_cause_proven=False,allocation_failure_proven=any(v>0 for k,v in eth.items() if k.endswith('buff_alloc_err')),
        softnet_changed={k:v for k,v in soft.items() if any(v.values())},
        tcp_changed={k:v for k,v in tcp.items() if v and any(x in k for x in ('Drop','Prun','Retrans','Timeout','MemoryPressure'))},
        ethtool_changed={k:v for k,v in eth.items() if v and any(x in k for x in ('err','drop','discard','out_of_buffer','congst','recover'))},
        scope='interface/host boundary deltas; no per-flow causality; asynchronous driver refresh may shift sample boundaries')
    result['rx_queue_counter_deltas']=queue_deltas
    result['rx_out_of_buffer_queue_attribution']='unavailable_interface_aggregate_only'
    cpu=[]
    for key,old in before.get('cpu',{}).items():
        diffs=[a-b for a,b in zip(after['cpu'][key],old)];total=sum(diffs)
        if total>0:cpu.append(dict(cpu=int(key),busy_fraction=1-(diffs[3]+diffs[4])/total,softirq_fraction=diffs[6]/total))
    result['cpu']=sorted(cpu,key=lambda row:row['busy_fraction'],reverse=True)
    return result
