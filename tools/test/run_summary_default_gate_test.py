#!/usr/bin/env python3
"""Large-file default promotion must preserve pairs, work and integrity evidence."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
import run_summary_default_gate as gate
from run_summary_default_gate import evaluate, residency, verify_chunks, verify_ranges


def rows(candidate=1):
    return [{'rep':rep,'summary_path_request':mode,'t_ready_seconds':1 if mode=='original' else candidate,
             'result':'pass','output_slot_removed':True,
             'summary':{'tree_hash':'crc32c:12345678','summary_tree_hash_source':'verified_chunks',
                        'summary_path_requested':mode,'summary_path_effective':'not_used'}}
            for rep in range(1,6) for mode in ('original','cached')]


class DefaultGateTest(unittest.TestCase):
    def test_neutral_reuse_passes_without_speedup_requirement(self):
        self.assertTrue(evaluate(rows())['gate_pass'])
        self.assertFalse(evaluate(rows(1.06))['gate_pass'])

    def test_complete_exact_pairs_required(self):
        for rs in (rows()[:-1],rows()+rows()[:1]):
            with self.assertRaisesRegex(AssertionError,'five complete pairs'):evaluate(rs)

    def test_unstable_arm_fails_without_dropping_outlier(self):
        rs=rows();rs[0]['t_ready_seconds']=2
        self.assertFalse(evaluate(rs)['gate_pass'])

    def test_reuse_and_hash_must_match(self):
        rs=rows();rs[0]['summary']['summary_tree_hash_source']='filesystem_read'
        with self.assertRaises(AssertionError):evaluate(rs)
        rs=rows();rs[0]['summary']['tree_hash']='bad'
        with self.assertRaises(AssertionError):evaluate(rs)

    def test_checkpoint_requires_exact_nonoverlapping_coverage(self):
        text='state=committed\nchunk_size=1048576\ncompleted_ranges=0-2097152\nverified_chunks=0:0:1048576:crc32c:12345678,2:1048576:1048576:crc32c:abcdef01\n'
        self.assertEqual(verify_chunks(text,2*1024**2),2)
        with self.assertRaises(AssertionError):verify_chunks(text.replace('2:1048576:','2:0:'),2*1024**2)
        with self.assertRaises(AssertionError):verify_chunks(text.replace('crc32c:abcdef01','crc32c:bad'),2*1024**2)

    def test_wrong_backend_or_missing_range_cannot_pass(self):
        with self.assertRaises(AssertionError):verify_ranges([])
        rs=[{'offset':i*1024**3,'length':1024**3,'receiver_payload_execution':'inline',
             'sender_payload_execution':'not_sender','receiver_write_backend':'mmap_experimental',
             'range_observation_enabled':False,'lane_thread_cpu_seconds':0,
             'sender_cpu_observation':None,'receiver_cpu_observation':None,
             'mapped_write':{'window_bound_bytes':64*1024**2,'concurrent_window_bound_bytes':256*1024**2,
                 'map_calls':16,'unmap_calls':16,'map_failures':0,'active_mapped_bytes':0,
                 'written_bytes':1024**3,'write_calls':17408,'peak_mapped_bytes':64*1024**2,'profile':None}}
            for i in range(8)]
        self.assertEqual(verify_ranges(rs)['ranges'],8)
        rs[0]['receiver_write_backend']='pwrite'
        with self.assertRaises(AssertionError):verify_ranges(rs)
        rs[0]['receiver_write_backend']='mmap_experimental'
        rs[0]['mapped_write']['peak_mapped_bytes']+=1
        with self.assertRaises(AssertionError):verify_ranges(rs)

    def test_residency_observation_does_not_change_file(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'resident';p.write_bytes(b'x'*8192)
            r=residency(p)
            self.assertEqual(r['pages'],2);self.assertEqual(r['resident_pages'],2)
            self.assertEqual(p.read_bytes(),b'x'*8192)

    def test_seed_swap_abort_retains_diagnostics_before_cleanup(self):
        import json
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-gate-test-',dir='/dev/shm') as d:
            root=Path(d);(root/gate.base.MARKER).write_text(gate.base.TOKEN)
            args=SimpleNamespace(commit='test',payload_root=root,evidence=root/'evidence')
            with patch.object(gate.base,'receipt'),patch.object(gate.base,'node',return_value=2), \
                 patch.object(gate.base,'mem',return_value=32*1024**3), \
                 patch.object(gate.base,'pswpout',side_effect=[10,11,11]), \
                 patch.object(gate.shutil,'disk_usage',return_value=SimpleNamespace(free=16*1024**3)):
                with self.assertRaisesRegex(AssertionError,'seed host swap-out changed'):gate.seed(args)
            r=json.loads((args.evidence/'peer/seed-resource.json').read_text())
            self.assertEqual(r['pswpout_delta'],1)
            self.assertEqual(r['materialized_bytes'],8*1024**2)


if __name__=='__main__':unittest.main()
