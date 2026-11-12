#!/usr/bin/env python3
"""Independent CRC oracle, source ownership and bounded read-mode behavior."""
import argparse
import json
import os
from pathlib import Path
import random
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', required=True, type=Path)
p.add_argument('--fault-library', required=True, type=Path)
a = p.parse_args()
binary = a.build_dir.resolve() / 'cpnetflux-source-read-bench'
MiB = 1024**2
table = []
for n in range(256):
    for _ in range(8):
        n = (n >> 1) ^ (0x82F63B78 if n & 1 else 0)
    table.append(n)
def crc32c(data):
    state = 0xFFFFFFFF
    for b in data:
        state = table[(state ^ b) & 255] ^ (state >> 8)
    return state ^ 0xFFFFFFFF
def frames(begin, end):
    calls = 0
    while begin < end:
        begin += min(65532, end-begin, MiB-begin % MiB)
        calls += 1
    return calls
runs = negatives = 0
with tempfile.TemporaryDirectory(prefix='cpnetflux-source-read-smoke-', dir='/dev/shm') as directory:
    root = Path(directory)
    source = root / 'source.bin'
    def invoke(extra=(), fault=None, path=source):
        env = os.environ.copy()
        if fault:
            env.update(LD_PRELOAD=str(a.fault_library.resolve()),
                       CPNETFLUX_SOURCE_READ_FAULT_SOURCE=str(source),
                       CPNETFLUX_SOURCE_READ_FAULT=fault)
        r = subprocess.run([str(binary), '--source', str(path)] + list(extra),
                           env=env, text=True, capture_output=True, timeout=30)
        if fault:
            match = re.search(r'cpnetflux-source-shim: maps=(\d+) unmaps=(\d+) reads=(\d+) retries=(\d+) changes=(\d+)', r.stderr)
            assert match, r.stderr
            fields = tuple(map(int, match.groups()))
            assert fields[0] == fields[1], ('leaked mapping', r.stderr)
            assert 'invalid source mapping' not in r.stderr, r.stderr
        else:
            fields = None
        return r, fields
    def validate(r, mode, data, workers, expected):
        assert r.returncode == 0, (r.returncode, r.stdout, r.stderr)
        row = json.loads(r.stdout)
        assert row['result'] == 'pass' and row['schema'] == 'cpnetflux_source_read_v1', row
        assert row['diagnostic_only'] and not row['network_io'] and not row['async_queue'], row
        assert not row['source_modified'] and not row['source_removed'], row
        assert row['bytes'] == len(data) == row['crc_scan_bytes'] and row['workers'] == workers, row
        assert row['mode'] == mode and row['frame_bytes'] == 65532 and row['chunk_bytes'] == MiB, row
        assert row['frames'] == row['combine_calls'] == sum(frames(x['begin'], x['end']) for x in row['ranges']), row
        offset = 0
        for i, value in enumerate(row['ranges']):
            assert value['begin'] == offset and value['end'] > offset, row
            offset = value['end']
            assert value['crc32c'] == value['expected_crc32c'] == expected[i], row
        assert offset == len(data), row
        assert row['service']['wall_seconds'] > 0 and row['setup']['wall_seconds'] > 0, row
        assert row['worker_cpu_seconds_sum'] >= 0 and row['worker_major_faults'] == 0, row
        assert row['map_calls'] == row['unmap_calls'], row
        if mode == 'pread':
            assert row['read_at_all_calls'] == row['frames'], row
            assert row['read_at_all_bytes'] == row['logical_source_to_buffer_copy_bytes'] == len(data), row
            assert row['sum_worker_peak_mapping_bytes'] == row['map_calls'] == 0, row
        else:
            assert row['read_at_all_calls'] == row['read_at_all_bytes'] == row['logical_source_to_buffer_copy_bytes'] == 0, row
            assert 0 < row['sum_worker_peak_mapping_bytes'] <= workers*64*MiB, row
            assert row['map_calls'] >= workers, row
        assert source.read_bytes() == data
    for size, workers in ((1, 1), (MiB+123, 1), (4*MiB+123, 4), (64*MiB+123, 1)):
        data = random.Random(20+size).randbytes(size)
        source.write_bytes(data)
        chunks = (size+MiB-1)//MiB
        begins = [0]
        for i in range(workers):
            begins.append(min(size, begins[-1]+(chunks//workers+(i < chunks % workers))*MiB))
        expected = [crc32c(memoryview(data)[begins[i]:begins[i+1]]) for i in range(workers)]
        for mode in ('pread', 'mmap'):
            r, fields = invoke(['--mode', mode, '--workers', str(workers)], 'observe')
            validate(r, mode, data, workers, expected)
            expected_maps = sum((begins[i+1]-1)//(64*MiB)-begins[i]//(64*MiB)+1
                                for i in range(workers))
            assert fields[0] == (0 if mode == 'pread' else expected_maps), fields
            runs += 1
    data = random.Random(20).randbytes(2*MiB+123)
    source.write_bytes(data)
    expected = [crc32c(data)]
    r, fields = invoke(['--mode', 'pread'], 'read-retry')
    validate(r, 'pread', data, 1, expected)
    assert fields[3] >= 2, fields
    runs += 1
    for mode, fault, message in (('pread', 'read-fail', 'source read:'),
                                 ('pread', 'read-corrupt', 'CRC mismatch'),
                                 ('mmap', 'map-fail', 'source mmap'),
                                 ('mmap', 'change', 'source changed'),
                                 ('pread', 'change', 'source changed')):
        r, _ = invoke(['--mode', mode], fault)
        assert r.returncode != 0 and message in r.stderr, (r.returncode, r.stderr)
        assert source.read_bytes() == data
        negatives += 1
    for mode in ('pread', 'mmap'):
        r, fields = invoke(['--mode', mode, '--cancel-after-frames', '1'], 'observe')
        assert r.returncode != 0 and 'cancelled' in r.stderr, r.stderr
        assert source.read_bytes() == data
        negatives += 1
    for extra in ([], ['--mode','bad'], ['--mode','mmap','--workers','0'],
                  ['--mode','mmap','--workers','5'], ['--mode','pread','--workers','-1'],
                  ['--mode','pread','--workers','1x'], ['--mode','mmap','--cancel-after-frames','0'],
                  ['--mode','mmap','--cancel-after-frames','-1'], ['--unknown','1'], ['--mode']):
        r, _ = invoke(extra)
        assert r.returncode != 0, (extra, r.stdout)
        negatives += 1
    link = root/'link'; link.symlink_to(source)
    fifo = root/'fifo'; os.mkfifo(fifo)
    empty = root/'empty'; empty.touch()
    large = root/'large'
    with large.open('wb') as f:
        f.truncate(1024**3+1)
    for invalid in (link, fifo, empty, large, root/'missing', root):
        r, _ = invoke(['--mode','mmap'], path=invalid)
        assert r.returncode != 0, (invalid, r.stdout)
        negatives += 1
    source.write_bytes(b'x')
    r, _ = invoke(['--mode','mmap','--workers','2'])
    assert r.returncode != 0 and 'nonempty' in r.stderr, r.stderr
    negatives += 1
    assert source.exists()
print(json.dumps({'result':'pass','valid_runs':runs,'negative_runs':negatives,
                  'crc_oracle':'independent Python CRC32C', 'source_owned_by_caller':True}))
