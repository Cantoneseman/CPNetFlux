#!/usr/bin/env python3
"""Bounded lifecycle behavior/cleanup; actual collapse optional outside capable hosts."""
import argparse, json, math, pathlib, subprocess, tempfile
p=argparse.ArgumentParser()
p.add_argument('--build-dir',required=True)
p.add_argument('--require-collapse',action='store_true')
a=p.parse_args()
binary=pathlib.Path(a.build_dir).resolve()/'cpnetflux-mapped-cost-bench'
MiB=1024**2
runs=0;invalid=0;capable=False
with tempfile.TemporaryDirectory(prefix='cpnetflux-collapse-smoke-',dir='/dev/shm') as directory:
    base=[str(binary),'--directory',directory,'--bytes',str(64*MiB),'--workers','1','--profile','off']
    def invoke(extra):
        r=subprocess.run(base+extra,text=True,capture_output=True,timeout=30)
        assert not list(pathlib.Path(directory).iterdir()),('payload leaked',r.stderr)
        return r
    def validate(row,mode,initial,workers=1,frame=65532):
        size=64*MiB*workers
        assert row['result']=='pass' and row['schema']=='cpnetflux_mapped_lifecycle_v1',row
        assert row['page_mode']==mode and row['initial_state']==initial,row
        assert row['payload_removed'] and not row['profile_enabled'],row
        assert workers<=row['map_calls']==row['unmap_calls']<=2*workers,row
        assert row['sum_worker_peak_mapping_bytes']==size,row
        assert row['written_bytes']==row['output_verified_bytes']==size,row
        assert row['write_calls']==workers*64*math.ceil(MiB/frame),row
        assert row['mapping_observations']==workers,row
        assert row['preserved_windows_verified']==(workers if initial=='pattern' else 0),row
        assert row['whole_diagnostic_wall_seconds']>0 and row['whole_process_cpu_seconds']>0,row
        assert row['cleanup_wall_seconds']>0 and row['cleanup_process_cpu_seconds']>=0,row
        for work in ('worker_work','copy_work','output_verification_work'):
            assert row[work]['worker_wall_seconds_sum']>0,row
            assert row[work]['worker_cpu_seconds_sum']>=0 and row[work]['major_faults']==0,row
        if mode=='collapse':
            assert row['eligible_bytes_sum']==row['shmem_pmd_mapped_bytes_lower_bound_sum']==size,row
            assert row['edge_bytes_sum']==0 and row['seed_writes']==size//(2*MiB),row
            assert row['prepare_work']['worker_wall_seconds_sum']>0,row
        else:
            assert row['seed_writes']==row['eligible_bytes_sum']==row['edge_bytes_sum']==0,row
    for workers,initial,frame in ((1,'fresh',65532),(4,'fresh',65532),(1,'pattern',1048576)):
        r=invoke(['--lifecycle-page-mode','ordinary','--initial-state',initial,
                  '--workers',str(workers),'--bytes',str(64*MiB*workers),'--frame-bytes',str(frame)])
        assert r.returncode==0,(r.returncode,r.stdout,r.stderr)
        validate(json.loads(r.stdout),'ordinary',initial,workers,frame);runs+=1
    r=invoke(['--lifecycle-page-mode','collapse'])
    if r.returncode:
        unsupported=('MADV_COLLAPSE errno=22','MADV_COLLAPSE errno=95','MADV_COLLAPSE errno=38',
                     'MADV_HUGEPAGE errno=22','MADV_HUGEPAGE errno=95','unsupported PMD size')
        assert not a.require_collapse and any(s in r.stderr for s in unsupported),r.stderr
        print('actual collapse unsupported on this host:',r.stderr.strip())
    else:
        capable=True;validate(json.loads(r.stdout),'collapse','fresh');runs+=1
        for workers in (1,4):
            r=invoke(['--lifecycle-page-mode','collapse','--initial-state','pattern',
                      '--workers',str(workers),'--bytes',str(64*MiB*workers)])
            assert r.returncode==0,(r.returncode,r.stderr)
            validate(json.loads(r.stdout),'collapse','pattern',workers);runs+=1
        r=invoke(['--lifecycle-page-mode','collapse','--cancel-after-seeds','1'])
        assert r.returncode!=0 and 'collapse cancelled' in r.stderr,r.stderr
        invalid+=1
    for extra in (
        ['--lifecycle-page-mode','bad'],
        ['--lifecycle-page-mode','ordinary','--initial-state','bad'],
        ['--lifecycle-page-mode','ordinary','--bytes',str(2*1024**3)],
        ['--lifecycle-page-mode','ordinary','--profile','on'],
        ['--lifecycle-page-mode','ordinary','--cancel-after-seeds','1'],
        ['--initial-state','pattern'],['--cancel-after-seeds','1'],
        ['--lifecycle-page-mode','collapse','--cancel-after-seeds','-1'],
        ['--lifecycle-page-mode','collapse','--cancel-after-seeds','33'],
        ['--lifecycle-page-mode','ordinary','--bytes','1'],
    ):
        r=invoke(extra);assert r.returncode!=0,(extra,r.stdout);invalid+=1
print(json.dumps({'result':'pass','valid_runs':runs,'negative_runs':invalid,'actual_collapse':capable}))
