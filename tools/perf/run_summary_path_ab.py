#!/usr/bin/env python3
"""S1 bounded same-binary summary-path A/B; uses S0's ownership and V3 receipts."""
import argparse
import json
from pathlib import Path
import socket
import statistics
import time

import run_small_file_baseline as base


def evaluate(rows, reuse=False):
    groups={mode:sorted([r for r in rows if r['summary_path_request']==mode],key=lambda r:r['rep'])
            for mode in ('original','cached')}
    for mode, rs in groups.items():
        assert len(rs)==5 and {r['rep'] for r in rs}==set(range(1,6)), 'requires five complete pairs'
        assert all(r['t_ready_seconds']>0 for r in rs)
        assert all(r['summary']['summary_tree_hash_source']==('verified_chunks' if reuse else 'filesystem_read') for r in rs)
        assert all(r['summary']['summary_path_effective']==('not_used' if reuse else mode) for r in rs)
    assert len({r['summary']['tree_hash'] for r in rows})==1, 'tree hash changed'
    result={'scope':'full spawn-to-exit; p95 descriptive for five samples','arms':{}}
    for mode,rs in groups.items():
        times=[r['t_ready_seconds'] for r in rs]
        result['arms'][mode]={'values':times,'median':statistics.median(times),
            'p95_descriptive':base.percentile(times,.95),
            'cv_population':statistics.pstdev(times)/statistics.mean(times),
            'median_summary_hash_seconds':statistics.median(r['summary']['summary_tree_hash_seconds'] for r in rs)}
    a,b=result['arms']['original'],result['arms']['cached']
    result['median_wall_reduction']=1-b['median']/a['median']
    result['paired_wall_reductions']=[1-y['t_ready_seconds']/x['t_ready_seconds'] for x,y in zip(groups['original'],groups['cached'])]
    stable=a['cv_population']<=.05 and b['cv_population']<=.05
    if reuse:
        result['gate_pass']=stable and abs(result['median_wall_reduction'])<=.05 and b['p95_descriptive']<=a['p95_descriptive']
        result['gate']='negative control: abs median change<=5%, CV<=5%, descriptive p95 no worse'
    else:
        result['gate_pass']=(stable and result['median_wall_reduction']>=.20 and
            min(result['paired_wall_reductions'])>0 and b['p95_descriptive']<=a['p95_descriptive'] and
            b['median_summary_hash_seconds']<a['median_summary_hash_seconds'])
        result['gate']='median wall reduction>=20%, CV<=5%, all pairs positive, p95 no worse, hash duration lower'
    return result


def ready(port):
    deadline=time.monotonic()+10
    while True:
        try:
            with socket.create_connection(('192.168.100.2',port),timeout=.2,source_address=('192.168.100.1',0)) as control:
                assert control.recv(1024).startswith(b'220')
            return
        except OSError:
            assert time.monotonic()<deadline;time.sleep(.02)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence',required=True,type=Path);p.add_argument('--payload-root',required=True,type=Path)
    p.add_argument('--commit',required=True)
    args=p.parse_args();assert base.node()==1
    args.evidence.mkdir(parents=True,exist_ok=True)
    local=base.receipt(args.commit);remote=base.peer(args,'receipt');assert local==remote
    base.save(args.evidence/'matching-builds.json',{'R770-1':local,'R770-2':remote})
    assert not args.payload_root.exists()
    args.payload_root.mkdir();(args.payload_root/base.MARKER).write_text(base.TOKEN)
    base.peer(args,'init');rows=[];report={}
    try:
        for index,(size,count) in enumerate(((4096,1000),(4096,10000),(65536,1000),(16384,10000))):
            args.size,args.count=size,count;case=f'{size//1024}k-{count}';port=49310+index
            expected=base.peer(args,'generate',size=size,count=count);started=False;case_rows=[]
            try:
                base.peer(args,'start',name=case,port=port,data_base=60000);started=True;ready(port)
                for rep in range(1,6):
                    modes=('original','cached') if rep%2 else ('cached','original')
                    for mode in modes:
                        name=f'{case}-{mode}-r{rep}'
                        row=base.client_sample(args,name,expected,'2','off',port,summary_path=mode)
                        row['rep']=rep;case_rows.append(row);rows.append(row)
                        base.save(args.evidence/'samples'/(name+'.receipt.json'),row)
                        print(json.dumps({'sample':name,'t_ready':row['t_ready_seconds'],
                            'hash_seconds':row['summary']['summary_tree_hash_seconds']}),flush=True)
            finally:
                if started:
                    server=base.peer(args,'stop',name=case);base.save(args.evidence/(case+'.server.json'),server)
                    assert 'error' not in server['resource'] and server['resource']['pswpout_delta']==0
                try:
                    checked=base.peer(args,'source-check')
                    assert checked=={k:expected[k] for k in ('sha256','files','bytes')}
                finally:base.peer(args,'source-cleanup')
            base.save(args.evidence/(case+'.rows.json'),case_rows)
            report[case]=evaluate(case_rows,reuse=size*count>=128*base.MiB)
            base.save(args.evidence/'analysis.json',report)
            print(json.dumps({'case':case,'analysis':report[case]}),flush=True)
    finally:
        base.peer(args,'finish')
        base.check_root(args.payload_root);(args.payload_root/base.MARKER).unlink();args.payload_root.rmdir()
    base.save(args.evidence/'results.json',rows)
    print(json.dumps({'execution':'pass','samples':len(rows),'all_gates_pass':all(x['gate_pass'] for x in report.values()),
                      'payload_root_removed':not args.payload_root.exists()}),flush=True)


if __name__=='__main__':main()
