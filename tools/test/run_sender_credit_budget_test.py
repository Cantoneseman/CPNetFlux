#!/usr/bin/env python3
"""Reject sender scope errors and inflated observer performance claims."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
from run_member_work_budget_test import fixture as previous_fixture
from run_sender_credit_budget import STAGES,evaluate


def fixture():
    rows=previous_fixture()
    for r in rows:
        for k in ('member_work_profile','sender_work_profile'):r['summary'][k]={'enabled':False}
        p={'enabled':r['profile_mode']=='1'}
        if p['enabled']:
            p.update(effective='sender',scope='local_whole_file_sender_attempts_excludes_ranges',truncated=False,
                started_members=10000,sent_members=10000,completed_members=10000,excluded_ranges=0,result_failures=0,
                thread_envelope_union_seconds=.08,
                stages={k:dict(calls=10000,sum_seconds=.12,union_seconds=.05) for k in STAGES},
                lanes=[dict(completed_members=2500,stage_sums={k:.03 for k in STAGES}) for _ in range(4)])
            for k in ('credit_unproven','credit_terminal'):p['stages'][k]['calls']=0
            p['stages']['result_drain']['calls']=4
        r['resource'].update(owned_swap_peak_bytes=0,mem_available_min_bytes=16*1024**3)
        r['server']=dict(sender_work_profile_mode=r['profile_mode'],resource=dict(r['resource']),
            sender_phases=dict(sender_work_profiles=[dict(channels=4,attempt_records=4,profile=p)]))
    return rows


class SenderBudgetTest(unittest.TestCase):
    def test_result_wait_is_not_a_thread_saving(self):
        report=evaluate(fixture());self.assertTrue(report['observer_gate_pass'])
        self.assertAlmostEqual(report['budgets'][0]['thread_envelope_fraction'],.08/1.01)
        self.assertEqual(report['budget_status'],'below_10_percent_sender_thread_envelope')

    def test_incomplete_or_wrong_scope_is_rejected(self):
        for key,value in (('truncated',True),('sent_members',9999),('excluded_ranges',1),('result_failures',1),
                          ('effective','not_used_receiver'),('thread_envelope_union_seconds',.2)):
            rows=fixture();rows[1]['server']['sender_phases']['sender_work_profiles'][0]['profile'][key]=value
            with self.subTest(key=key),self.assertRaises(AssertionError):evaluate(rows)
        rows=fixture();rows[1]['server']['sender_phases']['sender_work_profiles'][0]['attempt_records']=5
        with self.assertRaises(AssertionError):evaluate(rows)
        with self.assertRaises(AssertionError):evaluate(fixture()[:-1])

    def test_unknown_ready_waits_and_swap_cannot_pass(self):
        rows=fixture();rows[1]['server']['sender_phases']['sender_work_profiles'][0]['profile']['stages']['credit_unproven']['calls']=1
        with self.assertRaises(AssertionError):evaluate(rows)
        rows=fixture();rows[1]['server']['resource']['pswpout_delta']=1
        with self.assertRaises(AssertionError):evaluate(rows)

    def test_overhead_or_instability_is_inconclusive(self):
        for values in ((1.2,1.2,1.2),(.8,1.,1.2)):
            rows=fixture()
            for r,t in zip([r for r in rows if r['profile_mode']=='1'],values):r['t_ready_seconds']=t
            self.assertEqual(evaluate(rows)['budget_status'],'inconclusive_observer_gate')


if __name__=='__main__':unittest.main()
