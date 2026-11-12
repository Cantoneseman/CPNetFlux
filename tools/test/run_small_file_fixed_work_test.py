#!/usr/bin/env python3
"""Attribution results must not become throughput or batching acceptance."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
from run_small_file_fixed_work import compare, summarize_blocking


def row(seconds=2):
    return {'result':'pass','t_ready_seconds':seconds,'output_slot_removed':True,
        'summary':{'tree_hash':'crc32c:12345678','actual_transport':'v3',
            'completed_files':10000,'summary_path_effective':'cached','summary_tree_hash_source':'filesystem_read',
            'elapsed_seconds':1.5,'summary_tree_hash_seconds':.2}}


class FixedWorkTest(unittest.TestCase):
    def test_single_pair_only_measures_observer_overhead(self):
        r=compare([row(),row(2.2)])
        self.assertAlmostEqual(r['observed_profile_wall_change'],.1)
        self.assertFalse(r['batch_implementation_budget_proven'])
        self.assertEqual(r['pairs'],1)

    def test_changed_hash_or_summary_mode_rejects_attribution(self):
        rs=[row(),row()];rs[1]['summary']['tree_hash']='bad'
        with self.assertRaises(AssertionError):compare(rs)
        rs=[row(),row()];rs[1]['summary']['summary_path_effective']='original'
        with self.assertRaises(AssertionError):compare(rs)

    def test_blocked_fraction_is_snapshot_fraction_not_wall(self):
        r=summarize_blocking([{'threads':[{'state':'S','wchan':'do_poll'},{'state':'R','wchan':'0'}]}])
        self.assertEqual(r['thread_observations'],2)
        self.assertEqual(r['blocked_fraction'],.5)
        self.assertIn('not wait duration',r['scope'])


if __name__=='__main__':unittest.main()
