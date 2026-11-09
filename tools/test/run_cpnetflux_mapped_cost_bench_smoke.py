#!/usr/bin/env python3
"""Bounds, actual-writer accounting, two-phase verification and owned cleanup."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', type=Path, required=True)
args = p.parse_args()
binary = args.build_dir.resolve()/'cpnetflux-mapped-cost-bench'
with tempfile.TemporaryDirectory(prefix='cpnetflux-mapped-cost-test-', dir='/dev/shm') as path:
    command = [str(binary), '--directory', path]
    for extra in (['--workers','0'], ['--workers','5'], ['--bytes',str(9*1024**3)],
                  ['--bytes','123'], ['--bytes','64x'], ['--unknown','1'],
                  ['--frame-bytes','65536'], ['--frame-bytes','-1'],
                  ['--frame-bytes','64x'], ['--profile','invalid']):
        done = subprocess.run(command+extra, capture_output=True, text=True, timeout=10)
        assert done.returncode != 0, extra
        assert not list(Path(path).iterdir()), extra
    for workers in (1, 2):
        size = workers*64*1024**2
        done = subprocess.run(command+['--bytes',str(size),'--workers',str(workers)],
                              capture_output=True, text=True, timeout=60)
        assert done.returncode == 0, done.stdout+done.stderr
        row = json.loads(done.stdout)
        assert row['result']=='pass' and row['verified_phases']==2 and row['payload_removed']
        assert row['map_calls']==row['unmap_calls']==workers
        assert row['sum_worker_peak_mapping_bytes']==size
        assert row['whole_process_major_faults']==0
        for phase in ('cold','warm'):
            v = row[phase]
            assert v['copy_calls']==1088*workers and v['copy_bytes']==size and v['errors']==0
            assert v['copy_cpu_seconds']>0 and v['copy_major_faults']==0
        assert row['cold']['copy_minor_faults']>0
        assert row['warm']['map_cpu_seconds']==0
        assert row['total']['copy_bytes']==2*size
        assert row['total']['unmap_cpu_seconds']>0
        assert not list(Path(path).iterdir())
    for frame in (65532, 256*1024, 1024**2):
        for profile in ('on', 'off'):
            size = 64*1024**2
            done = subprocess.run(command+['--bytes',str(size),'--workers','1',
                                  '--frame-bytes',str(frame),'--profile',profile],
                                  capture_output=True,text=True,timeout=30)
            assert done.returncode == 0, done.stdout+done.stderr
            row = json.loads(done.stdout)
            assert row['frame_bytes']==frame and row['profile_enabled']==(profile=='on')
            calls=64*((1024**2+frame-1)//frame)
            for phase in ('cold','warm'):
                work=row[phase+'_work']
                assert work['write_calls']==calls and work['write_bytes']==size
                assert work['worker_cpu_seconds_sum']>0 and work['worker_wall_seconds_sum']>0
                assert work['major_faults']==0
                assert row[phase]['copy_calls']==(calls if profile=='on' else 0)
            assert row['cold_work']['minor_faults']>0
            assert row['verified_phases']==2 and row['payload_removed']
            assert not list(Path(path).iterdir())
print(json.dumps({'result':'pass','valid_runs':8,'invalid_runs':10,'payload_removed':True}))
