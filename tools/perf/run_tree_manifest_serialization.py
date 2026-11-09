#!/usr/bin/env python3
"""Fixed local serializer feasibility matrix; no transfer or runtime promotion."""
import argparse
import json
from pathlib import Path
import statistics

import run_small_file_baseline as base

BINARY=base.PROJECT/'build/cpnetflux-tree-manifest-serialization-bench'


def evaluate(rows):
    assert len(rows)==10,'requires five complete pairs'
    assert len({(r['serialized_bytes'],r['series_crc32c']) for r in rows})==1,'byte/digest disagreement'
    groups={mode:[r for r in rows if r['mode']==mode] for mode in ('baseline','candidate')}
    result={'scope':'local metadata CPU/wall including fixture/preparation/validation/CRC/destruction; no transfer IO',
            'arms':{},'transfer_gain_proven':False}
    stable=True
    for mode,rs in groups.items():
        assert len(rs)==5 and {r['rep'] for r in rs}==set(range(1,6)),'requires five complete pairs'
        assert all(r['files']==10000 and r['snapshots']==225 and 0<r['max_rss_bytes']<=256*base.MiB for r in rs)
        arm={}
        for metric in ('cpu_seconds','wall_seconds'):
            values=[r[metric] for r in rs];assert all(v>0 for v in values)
            cv=statistics.pstdev(values)/statistics.mean(values)
            arm[metric]={'values':values,'median':statistics.median(values),'cv_population':cv,
                         'p95_descriptive':base.percentile(values,.95)}
            stable=stable and cv<=.05
        result['arms'][mode]=arm
    a,b=result['arms']['baseline'],result['arms']['candidate']
    result['median_cpu_reduction']=1-b['cpu_seconds']['median']/a['cpu_seconds']['median']
    result['median_wall_reduction']=1-b['wall_seconds']['median']/a['wall_seconds']['median']
    result['local_gate_pass']=stable and result['median_cpu_reduction']>=.30
    result['gate']='five pairs; full byte/parse proof separate; CPU reduction>=30%; CPU and wall CV<=5% per arm'
    return result


def run(args):
    assert base.node()==1
    receipt=base.receipt(args.commit)
    assert not args.evidence.exists(),'fresh evidence directory required'
    args.evidence.mkdir(parents=True)
    base.save(args.evidence/'input.json',{'receipt':receipt,'benchmark_sha256':base.sha(BINARY),
        'benchmark_source_sha256':base.sha(Path(__file__).with_name('tree_manifest_serialization_bench.cpp')),
        'before':base.snapshot(),'matrix':'sorted and reversed; 10000 records x225 snapshots; five pairs each'})
    rows=[];report={}
    for order in ('sorted','reversed'):
        command=[str(BINARY),'--files','10000','--snapshots','225','--order',order]
        verify_log=args.evidence/(order+'.verification.json')
        resources=base.run_guarded(command+['--verify-series'],verify_log)
        assert resources['exit_code']==0,verify_log.read_text()
        verification=json.loads(verify_log.read_text())
        assert verification=={'byte_equal_snapshots':225,'parse_equal_snapshots':225,'input_unchanged':True}
        assert resources['rss_peak_bytes']<=256*base.MiB
        base.save(args.evidence/(order+'.verification-resource.json'),resources)
        case=[]
        for rep in range(1,6):
            for mode in (('baseline','candidate') if rep%2 else ('candidate','baseline')):
                name=f'{order}-{mode}-r{rep}'
                log=args.evidence/(name+'.json')
                resources=base.run_guarded(command+['--mode',mode],log)
                assert resources['exit_code']==0,log.read_text()
                row=json.loads(log.read_text());row.update(rep=rep,resource=resources)
                assert row['mode']==mode and row['order']==order and resources['rss_peak_bytes']<=256*base.MiB
                rows.append(row);case.append(row);base.save(args.evidence/'rows.json',rows)
                print(json.dumps({'sample':name,'cpu':row['cpu_seconds'],'wall':row['wall_seconds']}),flush=True)
        report[order]=evaluate(case);base.save(args.evidence/'analysis.json',report)
    assert len({(r['serialized_bytes'],r['series_crc32c']) for r in rows})==1
    base.save(args.evidence/'post-run.json',{'receipt':base.receipt(args.commit),'snapshot':base.snapshot(),
        'samples':len(rows),'all_local_gates_pass':all(r['local_gate_pass'] for r in report.values()),
        'note':'no runtime integration; no network payload or temporary data tree created'})
    print(json.dumps({'execution':'pass','analysis':report}),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--commit',required=True);p.add_argument('--evidence',required=True,type=Path)
    run(p.parse_args())
