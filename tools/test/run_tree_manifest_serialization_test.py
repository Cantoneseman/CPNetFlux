#!/usr/bin/env python3
"""Independent legacy byte/CRC oracle for the diagnostic tree serializer."""
import argparse
from pathlib import Path
import subprocess
import sys
import unittest

p=argparse.ArgumentParser();p.add_argument('--build-dir',required=True,type=Path)
a,rest=p.parse_known_args();sys.argv=[sys.argv[0],*rest]
BIN=a.build_dir.resolve()/'cpnetflux-tree-manifest-serialization-bench'
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
from run_tree_manifest_serialization import evaluate


def invoke(mode,fixture,*extra):
    return subprocess.run([str(BIN),'--mode',mode,'--fixture',fixture,'--emit-body',*extra],capture_output=True,timeout=10)


def oracle(empty=False,mode='download',algorithm='crc32c'):
    names=[b'z.dat',b'a/z',b'a-1',b'bb:line,.dat',b'\xc3\xa9/\xe6\x95\xb0\xe6\x8d\xae',b'zero']
    sizes=[0,2**64-1,4096,1,65536,0]
    times=[-(2**63),2**63-1,-1,0,42,0]
    states=['pending','transferring','completed','failed','changed','completed']
    errors=[b'',b'',b'',b'bad:\n,\0',b'\xff error',b'']
    rows=[]
    if not empty:
        for i in sorted(range(6),key=lambda j:names[j]):
            rows.append(f'{names[i].hex()}:{sizes[i]}:{times[i]}:{i+1:032x}:{states[i]}:{errors[i].hex()}')
    body=(f'manifest_version=1\nmode={mode}\nroot_logical_path_hex={b"/tmp/root: data".hex()}\n'
          f'checksum_algorithm={algorithm}\ncreated_at_unix_ns=0\nupdated_at_unix_ns={2**64-1}\n'
          f'files={",".join(rows)}\n').encode()
    crc=0xffffffff
    for byte in body:
        crc^=byte
        for _ in range(8):crc=(crc>>1)^(0x82f63b78 if crc&1 else 0)
    return body+f'manifest_body_crc32c={crc^0xffffffff:08x}\n'.encode()


class SerializationTest(unittest.TestCase):
    def test_analysis_requires_complete_stable_pairs_and_equal_bytes(self):
        rows=[{'rep':rep,'mode':mode,'cpu_seconds':1 if mode=='baseline' else .5,
               'wall_seconds':1 if mode=='baseline' else .5,'serialized_bytes':100,
               'series_crc32c':123,'files':10000,'snapshots':225,'max_rss_bytes':1000000}
              for rep in range(1,6) for mode in ('baseline','candidate')]
        self.assertTrue(evaluate(rows)['local_gate_pass'])
        with self.assertRaises(AssertionError):evaluate(rows[:-1])
        rows[0]['series_crc32c']=999
        with self.assertRaises(AssertionError):evaluate(rows)

    def test_analysis_rejects_weak_or_unstable_local_gain(self):
        rows=[{'rep':rep,'mode':mode,'cpu_seconds':1 if mode=='baseline' else .8,
               'wall_seconds':1 if mode=='baseline' else .8,'serialized_bytes':100,
               'series_crc32c':123,'files':10000,'snapshots':225,'max_rss_bytes':1000000}
              for rep in range(1,6) for mode in ('baseline','candidate')]
        self.assertFalse(evaluate(rows)['local_gate_pass'])
        for row in rows:
            if row['mode']=='candidate':row['cpu_seconds']=row['wall_seconds']=.5
        rows[0]['cpu_seconds']=rows[0]['wall_seconds']=2
        self.assertFalse(evaluate(rows)['local_gate_pass'])

    def test_independent_bytes_and_crc_for_boundaries_and_empty(self):
        for fixture in ('mixed','empty','shuffle'):
            for mode in ('baseline','candidate'):
                r=invoke(mode,fixture)
                self.assertEqual(r.returncode,0,r.stderr)
                self.assertEqual(r.stdout,oracle(empty=fixture=='empty'))

    def test_other_header_modes_preserve_bytes(self):
        for transfer_mode,algorithm in (('upload','none'),('upload','crc32c')):
            for mode in ('baseline','candidate'):
                r=invoke(mode,'mixed','--transfer-mode',transfer_mode,'--checksum',algorithm)
                self.assertEqual(r.returncode,0,r.stderr)
                self.assertEqual(r.stdout,oracle(mode=transfer_mode,algorithm=algorithm))

    def test_duplicate_serializer_behavior_matches_legacy(self):
        # Legacy serialization accepts duplicates; parsing rejects them. Preserve both.
        x,y=invoke('baseline','duplicate'),invoke('candidate','duplicate')
        self.assertEqual(x.returncode,0,x.stderr);self.assertEqual(y.returncode,0,y.stderr)
        self.assertEqual(x.stdout,y.stdout)
        x,y=invoke('baseline','short-id'),invoke('candidate','short-id')
        self.assertEqual(x.returncode,0,x.stderr);self.assertEqual(y.returncode,0,y.stderr)
        self.assertEqual(x.stdout,y.stdout)

    def test_validation_rejections_match(self):
        for fixture in ('bad-version','bad-root','bad-path','bad-id','bad-control'):
            x,y=invoke('baseline',fixture),invoke('candidate',fixture)
            self.assertNotEqual(x.returncode,0);self.assertEqual(x.returncode,y.returncode)
            self.assertEqual(x.stderr,y.stderr)

    def test_input_unchanged_and_series_roundtrip(self):
        import json
        for order in ('sorted','reversed'):
            r=subprocess.run([str(BIN),'--verify-series','--files','32','--snapshots','5','--order',order],
                             capture_output=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stderr)
            v=json.loads(r.stdout)
            self.assertEqual(v['byte_equal_snapshots'],5);self.assertEqual(v['parse_equal_snapshots'],5)
            self.assertTrue(v['input_unchanged'])

    def test_invalid_mode_or_unbounded_work_rejected(self):
        for extra in (['--mode','bad'],['--files','0'],['--files','100001'],['--snapshots','0'],['--snapshots','1001']):
            r=subprocess.run([str(BIN),*extra],capture_output=True,timeout=10)
            self.assertNotEqual(r.returncode,0)


if __name__=='__main__':unittest.main()
