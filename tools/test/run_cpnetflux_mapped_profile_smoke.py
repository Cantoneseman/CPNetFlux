#!/usr/bin/env python3
"""Exercise actual V3 mapped/pwrite downloads and nullable fault/CPU observations."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from gridftp_port_window import clamp_passive_data_port_base
from tree_smoke_common import file_sha256, free_port, stop_server, wait_for_control


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--profile', choices=('0', '1'), default='1')
    parser.add_argument('--observation', choices=('on', 'off'), default='on')
    parser.add_argument('--backend', choices=('mmap', 'pwrite'), default='mmap')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    env = {k:v for k,v in os.environ.items() if not k.startswith('CPNETFLUX_')}
    env.update(CPNETFLUX_STAGE_READ_CONCURRENCY='0', CPNETFLUX_STAGE_WRITE_CONCURRENCY='0',
               CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION='inline', CPNETFLUX_RANGE_TIMING=args.observation,
               CPNETFLUX_RECEIVER_CPU_PROFILE='1', CPNETFLUX_MAPPED_WRITE_PROFILE=args.profile)
    with tempfile.TemporaryDirectory(prefix='cpnetflux-mapped-profile-') as directory:
        root = Path(directory)
        source = root/'server/payload/payload.bin'
        source.parent.mkdir(parents=True)
        block = bytes(range(256))*4096
        with source.open('wb') as stream:
            for _ in range(128):
                stream.write(block)
        expected = file_sha256(source)
        port, log = free_port(), root/'server.log'
        with log.open('w') as output:
            server = subprocess.Popen([str(build/'cpnetflux-gridftp-server'),
                '--host','127.0.0.1','--port',str(port),'--root',str(root/'server'),
                '--data-port-base',str(clamp_passive_data_port_base(free_port())),
                '--connections','4','--checksum','crc32c'],
                env=env, stdout=output, stderr=subprocess.STDOUT)
        try:
            wait_for_control(port)
            if args.backend == 'mmap':
                env['CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE'] = 'mmap'
            summary = root/'download.json'
            command = [str(build/'cpnetflux-tree-download-client'),
                '--host','127.0.0.1','--local-bind-address','127.0.0.1','--port',str(port),
                '--source-dir','payload','--dest-dir',str(root/'received'),
                '--connections','4','--file-parallelism','4','--control-reuse','worker',
                '--data-session-reuse','tree','--file-scheduling','dynamic','--scheduler','off',
                '--checksum','crc32c','--buffer-size','65536','--posix-write-strategy','direct',
                '--file-io-buffer-size','0','--final-verify-policy','verified_chunks',
                '--phase-timing','on','--json-summary',str(summary)]
            done = subprocess.run(command, env=env, capture_output=True, text=True, timeout=60)
            assert done.returncode == 0, done.stdout+done.stderr
            row = json.loads(summary.read_text())
            assert row['result']=='pass' and row['actual_transport']=='v3'
            assert row['bytes_transferred']==source.stat().st_size
            assert file_sha256(root/'received/payload.bin')==expected
            timings = row['file_timings']
            assert len(timings)>=4 and all(t['range_id'] for t in timings)
            enabled = args.backend=='mmap' and args.profile=='1' and args.observation=='on'
            for t in timings:
                mapped = t['mapped_write']
                assert 'profile' in mapped, 'missing mapped write profile contract'
                p = mapped['profile']
                if args.backend=='mmap':
                    assert t['receiver_write_backend']=='mmap_experimental'
                    assert mapped['map_calls']==mapped['unmap_calls']>0
                    assert mapped['active_mapped_bytes']==mapped['map_failures']==0
                    assert mapped['written_bytes']==t['length']
                if args.observation=='on':
                    cpu = t['receiver_cpu_observation']
                    assert cpu['local_work_executor']
                    assert cpu['lease_start_calls']==cpu['lease_finish_calls']==cpu['write_calls']>0
                if not enabled:
                    assert p is None
                    continue
                assert p['schema']=='cpnetflux_mapped_write_profile_v1'
                assert p['cpu_clock']=='CLOCK_THREAD_CPUTIME_ID'
                assert p['fault_scope']=='RUSAGE_THREAD_copy_brackets'
                assert p['errors']==0
                assert p['copy_calls']==mapped['write_calls']
                assert p['copy_bytes']==mapped['written_bytes']
                assert p['copy_minor_faults']>0 and p['copy_major_faults']==0
                assert p['copy_cpu_seconds']>0 and p['copy_wall_seconds']>0
                assert p['map_cpu_seconds']>0 and p['unmap_cpu_seconds']>0
                nested = sum(p[k+'_cpu_seconds'] for k in ('map','copy'))
                assert nested <= t['receiver_cpu_observation']['physical_write_cpu_seconds']+.02
            print(json.dumps({'result':'pass','profile_enabled':enabled,'backend':args.backend,
                              'bytes':row['bytes_transferred'],'ranges':len(timings)}))
        finally:
            stop_server(server, log)


if __name__=='__main__':
    main()
