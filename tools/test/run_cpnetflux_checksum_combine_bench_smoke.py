#!/usr/bin/env python3
"""Independent CRC32C oracle, chunk-tail counts and bounded diagnostic input."""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir', required=True, type=Path)
a = p.parse_args()
binary = a.build_dir.resolve() / 'cpnetflux-checksum-bench'
MiB = 1024**2
# Castagnoli reference; zlib.crc32 has a different polynomial.
table = []
for n in range(256):
    for _ in range(8):
        n = (n >> 1) ^ (0x82f63b78 if n & 1 else 0)
    table.append(n)
crc = 0xffffffff
for i in range(MiB):
    crc = table[(crc ^ (i % 251)) & 255] ^ (crc >> 8)
expected_crc = crc ^ 0xffffffff
rows = []
for frame in (65532, 256*1024, MiB):
    args = [str(binary), '--mode', 'combine', '--bytes', str(2*MiB),
            '--iterations', '2', '--frame-bytes', str(frame)]
    done = subprocess.run(args, capture_output=True, text=True, timeout=20)
    assert done.returncode == 0, done.stderr
    row = json.loads(done.stdout)
    expected_frames = (MiB + frame - 1) // frame
    assert row['diagnostic_only'] and not row['file_io'] and not row['network_io']
    assert row['mode'] == 'chunk_combine' and row['verified']
    assert row['chunk_bytes'] == MiB and row['logical_bytes_per_iteration'] == 2*MiB
    assert row['frame_bytes'] == frame and row['frames_per_chunk'] == expected_frames
    assert row['combine_calls'] == expected_frames*4
    assert row['tail_bytes'] == (MiB % frame)
    assert row['expected_crc32c'] == row['result_crc32c'] == expected_crc
    assert row['elapsed_seconds'] > 0 and row['setup_seconds'] >= 0
    assert row['ns_per_combine'] > 0
    assert 'throughput_gbps' not in row
    rows.append(row['combine_calls'])
# A varied block changes branch inputs; verify it with an independent byte oracle.
state = 0x9e3779b9
varied_crc = 0xffffffff
for j in range(2*MiB):
    if j == MiB: varied_crc = 0xffffffff
    state ^= (state << 13) & 0xffffffff
    state ^= state >> 17
    state ^= (state << 5) & 0xffffffff
    varied_crc = table[(varied_crc ^ (state & 255)) & 255] ^ (varied_crc >> 8)
varied_crc ^= 0xffffffff
varied = subprocess.run([str(binary), '--mode', 'combine', '--bytes', str(2*MiB),
                         '--iterations', '1', '--crc-pattern', 'varied'],
                        capture_output=True,text=True,timeout=10)
assert varied.returncode == 0, varied.stderr
v = json.loads(varied.stdout)
assert v['crc_pattern']=='varied' and v['crc_pool_chunks']==2
assert v['result_crc32c']==v['expected_crc32c']==varied_crc
base = [str(binary), '--mode', 'combine', '--bytes', str(MiB)]
invalid = [ ['--frame-bytes', x] for x in ('0', '65536', '-1', '1048577', '64x', '18446744073709551616') ]
invalid += [['--bytes','0'], ['--bytes','123'], ['--bytes',str(9*1024**3)],
            ['--iterations','101'], ['--iterations','0'], ['--mode','unknown'], ['--crc-pattern','unknown']]
for extra in invalid:
    done = subprocess.run(base+extra, capture_output=True, text=True, timeout=10)
    assert done.returncode != 0, extra
scan = subprocess.run([str(binary), '--backend', 'software', '--bytes', '4097',
                       '--iterations', '1', '--buffer-size', '257'],
                      capture_output=True, text=True, timeout=10)
assert scan.returncode == 0 and 'checksum_bench algorithm=crc32c' in scan.stdout, scan.stderr
print(json.dumps({'result':'pass','valid_combine_runs':4,'invalid_runs':len(invalid),
                  'legacy_scan_runs':1,'combine_calls':rows,'independent_crc32c':expected_crc}))
