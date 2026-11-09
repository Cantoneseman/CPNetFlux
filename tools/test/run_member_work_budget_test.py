#!/usr/bin/env python3
"""Reject incomplete observation and avoid interpreting payload waits as savings."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'perf'))
from run_tree_state_budget_test import fixture as previous_fixture
from run_member_work_budget import STAGES, evaluate


def fixture():
    rows = previous_fixture()
    for r in rows:
        s = r['summary']; s['tree_manifest_profile'] = {'enabled':False}
        s['member_work_profile'] = {'enabled':r['profile_mode'] == '1'}
        if r['profile_mode'] == '1':
            s['member_work_profile'].update(effective='receiver', truncated=False,
                scope='completed_local_whole_file_attempts_excludes_final_audit',
                started_members=10000, completed_members=10000, excluded_ranges=0,
                manifest_save=dict(attempts=30000, failures=0, publish_successes=30000),
                stages={k:dict(calls=10000, sum_seconds=.12, union_seconds=.05) for k in STAGES},
                thread_envelope_union_seconds=.08,
                lanes=[dict(completed_members=2500, stage_sums={k:.03 for k in STAGES}) for _ in range(4)])
    return rows


class MemberBudgetTest(unittest.TestCase):
    def test_envelope_does_not_add_payload_or_lane_sums(self):
        report = evaluate(fixture())
        self.assertTrue(report['observer_gate_pass'])
        self.assertEqual(report['budget_status'], 'below_10_percent_thread_envelope')
        self.assertAlmostEqual(report['thread_envelope_fraction_max'], .08/1.01)

    def test_incomplete_or_mis_scoped_accounting(self):
        for key, value in (('truncated',True), ('started_members',10001), ('completed_members',9999),
                           ('excluded_ranges',1), ('effective','not_used_sender'), ('thread_envelope_union_seconds',.15)):
            rows = fixture(); rows[1]['summary']['member_work_profile'][key] = value
            with self.subTest(key=key), self.assertRaises(AssertionError): evaluate(rows)
        with self.assertRaises(AssertionError): evaluate(fixture()[:-1])

    def test_observer_gate_is_required(self):
        for times in ((1.2,1.2,1.2), (.8,1.,1.2)):
            rows = fixture()
            for r,t in zip([x for x in rows if x['profile_mode'] == '1'],times): r['t_ready_seconds'] = t
            self.assertEqual(evaluate(rows)['budget_status'], 'inconclusive_observer_gate')

    def test_material_envelope_still_does_not_accept_pool(self):
        rows = fixture()
        for r in rows:
            if r['profile_mode'] == '1':
                p = r['summary']['member_work_profile']; p['thread_envelope_union_seconds'] = .2
                for k in ('thread_create','thread_join_tail'):
                    p['stages'][k].update(sum_seconds=.4, union_seconds=.15)
                    for lane in p['lanes']: lane['stage_sums'][k] = .1
        self.assertEqual(evaluate(rows)['budget_status'], 'requires_removable_mechanism_and_discount_before_design')


if __name__ == '__main__': unittest.main()
