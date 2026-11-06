#!/usr/bin/env python3
"""Measure an existing source file without networking or modifying its contents."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import time


def run(*command):
    return subprocess.check_output(command, text=True).strip()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def identity(path):
    stat = path.stat()
    return {key: getattr(stat, key) for key in ['st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns']}


def snapshot():
    return {name: Path('/proc', name).read_text() for name in ['diskstats', 'meminfo', 'vmstat', 'loadavg']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--production-pid', type=int, required=True)
    args = parser.parse_args()
    project, source, root = args.project.resolve(), args.source.resolve(), args.evidence.resolve()
    assert source.is_file() and root != source.parent
    assert len(args.sha256) == 64 and len(args.commit) == 40
    assert run('git', '-C', str(project), 'rev-parse', 'HEAD') == args.commit
    assert not run('git', '-C', str(project), 'diff', '--name-only', 'HEAD')
    binary = project / 'build/cpnetflux-storage-bench'
    binary_sha = hashlib.sha256(binary.read_bytes()).hexdigest()
    before = identity(source)
    assert before['st_size'] > 0
    production_comm = Path('/proc', str(args.production_pid), 'comm').read_text().strip()
    assert production_comm.startswith('cpnetflux-')
    root.mkdir(parents=True, exist_ok=False)
    provenance = {'commit': args.commit, 'binary_sha256': binary_sha, 'source': str(source),
                  'source_identity': before, 'expected_sha256': args.sha256,
                  'production_pid': args.production_pid, 'production_comm': production_comm,
                  'started_at_utc': datetime.now(timezone.utc).isoformat(),
                  'scope': 'buffered POSIX source reads; no cache dropping; no network or payload writes',
                  'physical_io_scope': 'GNU time child filesystem input blocks * 512; includes read-ahead',
                  'cpu_scope': 'GNU time child user/system seconds; process lifetime',
                  'checksum_scope': 'per-read-block CRC32C/auto plus combine; no allocation/frame encode/queue/network',
                  'order_limit': 'rotated read-size order, checksum group after read-only group; not cold-cache runs'}
    save(root / 'provenance.json', provenance)
    sizes = [65536, 1048576, 8388608]
    cases = [(f'read-r{repeat+1}-b{size}', size, 'none') for repeat in range(3)
             for size in sizes[repeat:] + sizes[:repeat]]
    cases += [(f'crc-r{repeat+1}-b65532', 65532, 'crc32c') for repeat in range(3)]
    rows = []
    for name, size, checksum in cases:
        assert identity(source) == before
        assert run('git', '-C', str(project), 'rev-parse', 'HEAD') == args.commit
        assert not run('git', '-C', str(project), 'diff', '--name-only', 'HEAD')
        assert hashlib.sha256(binary.read_bytes()).hexdigest() == binary_sha
        case = root / name
        case.mkdir()
        save(case / 'before.json', snapshot())
        command = [str(binary), '--path', str(source), '--mode', 'read', '--bytes',
                   str(before['st_size']), '--buffer-size', str(size), '--iterations', '1',
                   '--file-io-backend', 'posix', '--file-io-advice', 'off',
                   '--file-io-queue-depth', '1', '--file-io-batch-size', '1',
                   '--read-checksum', checksum, '--keep-file']
        started = time.monotonic()
        with (case / 'stdout.log').open('w') as stdout, (case / 'stderr.log').open('w') as stderr:
            result = subprocess.run(['/usr/bin/time', '-f', '%U %S %I %O %M', '-o', str(case / 'resource.txt'),
                                     *command], stdout=stdout, stderr=stderr, timeout=600)
        wall = time.monotonic() - started
        save(case / 'after.json', snapshot())
        assert result.returncode == 0, (name, (case / 'stderr.log').read_text())
        parsed = [dict(field.split('=', 1) for field in line.split()[1:])
                  for line in (case / 'stdout.log').read_text().splitlines() if line.startswith('storage_bench ')]
        metrics = next(row for row in parsed if row['aggregate'] == 'false')
        assert metrics['result'] == 'pass' and int(metrics['read_total_bytes']) == before['st_size']
        assert int(metrics['read_call_count']) == (before['st_size'] + size - 1) // size
        assert int(metrics['write_call_count']) == 0 and identity(source) == before
        resource = (case / 'resource.txt').read_text().split()
        row = {'case': name, 'command': command, 'buffer_size': size, 'checksum': checksum,
               'process_wall_seconds': wall, 'elapsed_seconds': float(metrics['elapsed_seconds']),
               'gbps': float(metrics['throughput_gbps']), 'user_seconds': float(resource[0]),
               'system_seconds': float(resource[1]), 'physical_read_bytes': int(resource[2]) * 512,
               'physical_write_bytes': int(resource[3]) * 512, 'max_rss_kib': int(resource[4]),
               'metrics': metrics}
        save(case / 'metrics.json', row)
        rows.append(row)
        print(json.dumps({key: row[key] for key in ['case', 'elapsed_seconds', 'gbps',
                                                  'physical_read_bytes', 'user_seconds', 'system_seconds']}), flush=True)
    print('all timed reads passed; independent source SHA-256 started', flush=True)
    sha = run('sha256sum', str(source)).split()[0]
    assert sha == args.sha256 and identity(source) == before
    assert Path('/proc', str(args.production_pid), 'comm').read_text().strip() == production_comm
    assert len({row['metrics']['read_checksum_value'] for row in rows if row['checksum'] == 'crc32c'}) == 1
    groups = []
    for size, checksum in [(size, 'none') for size in sizes] + [(65532, 'crc32c')]:
        selected = [row for row in rows if row['buffer_size'] == size and row['checksum'] == checksum]
        rates = [row['gbps'] for row in selected]
        groups.append({'buffer_size': size, 'checksum': checksum, 'n': len(selected),
                       'gbps_median': statistics.median(rates),
                       'gbps_cv_population_percent': statistics.pstdev(rates) / statistics.mean(rates) * 100,
                       **{key + '_median': statistics.median(row[key] for row in selected) for key in
                          ['elapsed_seconds', 'user_seconds', 'system_seconds', 'physical_read_bytes',
                           'physical_write_bytes', 'max_rss_kib']},
                       'checksum_seconds_median': statistics.median(float(row['metrics']['read_checksum_seconds'])
                                                                   for row in selected)})
    save(root / 'summary.json', {'complete': True, 'provenance': provenance, 'sha256': sha,
                                'source_identity_unchanged': True, 'groups': groups, 'rows': rows,
                                'completed_at_utc': datetime.now(timezone.utc).isoformat()})
    print(json.dumps({'complete': True, 'groups': groups, 'sha256': sha}), flush=True)


if __name__ == '__main__':
    main()
