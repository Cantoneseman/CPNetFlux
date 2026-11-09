#!/usr/bin/env python3
"""Validate observer gates and avoid overlapping-wait budget inflation."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'perf'))
from run_tree_serializer_ab_test import fixture as serializer_fixture
from run_tree_state_budget import evaluate


def fixture():
    rows = [r for r in serializer_fixture() if r['rep'] <= 3]
    for r in rows:
        mode = '0' if r['tree_manifest_serializer_request'] == 'original' else '1'
        r['profile_mode'] = mode; r['tree_manifest_serializer_request'] = 'original'
        r['t_ready_seconds'] = 1. if mode == '0' else 1.01
        s = r['summary']; s['tree_manifest_serializer_requested'] = s['tree_manifest_serializer_effective'] = 'original'
        s['tree_manifest_profile'] = {'enabled': mode == '1'}
        if mode == '1':
            s['tree_manifest_profile'].update(truncated=False, callback_wait_intervals=20000,
                scope='persistent_writer_attempts_excludes_initial_and_preflight', snapshot_copy_records=100000,
                snapshot_copy_seconds=.015, snapshot_destroy_seconds=.005, snapshot_hold_union_seconds=.02,
                callback_wait_union_seconds=.05, callback_wait_on_snapshot_union_seconds=.01,
                final_reconcile_seconds=.005, finish_barrier_seconds=.005,
                save=dict(attempts=100, publish_successes=100, failures=0, serialized_bytes=1000000,
                          record_sort_calls=100, record_copy_items=100000))
    return rows


class BudgetTest(unittest.TestCase):
    def test_disjoint_envelope_is_not_service_sum(self):
        report = evaluate(fixture())
        self.assertTrue(report['observer_gate_pass'])
        self.assertEqual(report['budget_status'], 'below_10_percent_direct_sync_envelope')
        self.assertAlmostEqual(report['budgets'][0]['direct_sync_envelope_seconds'], .02)
        self.assertAlmostEqual(report['budgets'][0]['all_callback_wait_envelope_seconds'], .06)

    def test_missing_or_duplicate_pairs(self):
        rows = fixture(); rows[0]['rep'] = 2
        for rs in (fixture()[:-1], rows):
            with self.assertRaises(AssertionError): evaluate(rs)

    def test_invalid_or_truncated_accounting(self):
        for key, value in (('truncated', True), ('callback_wait_on_snapshot_union_seconds', .2),
                           ('snapshot_copy_seconds', 1.), ('callback_wait_intervals', 0)):
            rows = fixture(); rows[1]['summary']['tree_manifest_profile'][key] = value
            with self.subTest(key=key), self.assertRaises(AssertionError): evaluate(rows)

    def test_observer_overhead_or_instability_is_inconclusive(self):
        for times in ((1.2, 1.2, 1.2), (1., .8, 1.2)):
            rows = fixture()
            for r, value in zip([x for x in rows if x['profile_mode'] == '1'], times):
                r['t_ready_seconds'] = value
            self.assertEqual(evaluate(rows)['budget_status'], 'inconclusive_observer_gate')

    def test_material_envelope_does_not_accept_design(self):
        rows = copy.deepcopy(fixture())
        for r in rows:
            if r['profile_mode'] == '1': r['summary']['tree_manifest_profile']['finish_barrier_seconds'] = .2
        self.assertEqual(evaluate(rows)['budget_status'], 'requires_removable_mechanism_and_discount_before_design')


if __name__ == '__main__': unittest.main()
