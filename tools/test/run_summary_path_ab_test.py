#!/usr/bin/env python3
"""Acceptance analysis must retain complete paired samples and reject weak gains."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
from run_summary_path_ab import evaluate


def rows(candidate=.6,reuse=False):
    return [{'rep':rep,'summary_path_request':mode,'t_ready_seconds':1 if mode=='original' else candidate,
             'summary':{'tree_hash':'crc32c:12345678','summary_tree_hash_source':'verified_chunks' if reuse else 'filesystem_read',
                        'summary_path_effective':'not_used' if reuse else mode,
                        'summary_tree_hash_seconds':.4 if mode=='original' else .05}}
            for rep in range(1,6) for mode in ('original','cached')]


class AcceptanceTest(unittest.TestCase):
    def test_complete_stable_improvement_passes(self):
        result=evaluate(rows())
        self.assertTrue(result['gate_pass']);self.assertAlmostEqual(result['median_wall_reduction'],.4)
        self.assertEqual(len(result['paired_wall_reductions']),5)

    def test_missing_pairs_fail(self):
        with self.assertRaisesRegex(AssertionError,'five complete pairs'):evaluate(rows()[:-1])

    def test_hash_disagreement_fails(self):
        values=rows();values[0]['summary']['tree_hash']='different'
        with self.assertRaisesRegex(AssertionError,'tree hash changed'):evaluate(values)

    def test_small_gain_does_not_pass(self):
        self.assertFalse(evaluate(rows(.98))['gate_pass'])

    def test_negative_control_does_not_require_speedup(self):
        self.assertTrue(evaluate(rows(1,reuse=True),reuse=True)['gate_pass'])
        self.assertFalse(evaluate(rows(1.1,reuse=True),reuse=True)['gate_pass'])


if __name__=='__main__':unittest.main()
