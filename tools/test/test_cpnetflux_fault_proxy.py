#!/usr/bin/env python3
"""Concurrency tests for deterministic lane fault injection."""
import sys
import time
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from cpnetflux_fault_proxy import (
    SequentialResetPlan,
    SynchronizedMixedResetGate,
    SynchronizedResetGate,
)


class SequentialResetPlanTest(unittest.TestCase):
    def test_advances_one_connection_at_a_time(self):
        plan = SequentialResetPlan(3, first_connection=2)
        self.assertTrue(plan.is_target(2))
        self.assertTrue(plan.claim(2, 3))
        self.assertFalse(plan.is_target(2))
        self.assertTrue(plan.is_target(3))
        self.assertTrue(plan.claim(3, 4))
        self.assertTrue(plan.claim(4, 5))
        self.assertFalse(plan.is_target(5))
        self.assertEqual(plan.reset_connections, [2, 3, 4])

    def test_does_not_skip_a_missing_recovery_connection(self):
        plan = SequentialResetPlan(2, first_connection=2)
        self.assertFalse(plan.claim(3, 4))
        self.assertTrue(plan.is_target(2))

    def test_requires_at_least_one_fault(self):
        with self.assertRaises(ValueError):
            SequentialResetPlan(0)


class SynchronizedResetGateTest(unittest.TestCase):
    def test_waits_for_both_lanes_and_releases_them_together(self):
        gate = SynchronizedResetGate({1, 2}, timeout=1.0)
        with ThreadPoolExecutor(max_workers=2) as executor:
            first = executor.submit(gate.wait_and_claim, 1)
            time.sleep(0.02)
            self.assertFalse(first.done(), "first lane must wait for the second")
            second = executor.submit(gate.wait_and_claim, 2)
            self.assertTrue(first.result(timeout=1.0))
            self.assertTrue(second.result(timeout=1.0))

    def test_duplicate_pump_for_one_lane_claims_only_one_reset(self):
        gate = SynchronizedResetGate({1, 2}, timeout=1.0)
        with ThreadPoolExecutor(max_workers=3) as executor:
            results = list(executor.map(gate.wait_and_claim, (1, 1, 2)))
        self.assertEqual(sum(results), 2)
        self.assertEqual(gate.reset_connections, {1, 2})

    def test_missing_lane_times_out_without_resetting_only_one_lane(self):
        gate = SynchronizedResetGate({1, 2}, timeout=0.02)
        self.assertFalse(gate.wait_and_claim(1))
        self.assertFalse(gate.wait_and_claim(2))
        self.assertEqual(gate.reset_connections, set())


class SynchronizedMixedResetGateTest(unittest.TestCase):
    def test_waits_for_data_and_control_fault_then_releases_both(self):
        data_fault = ("data", 1)
        control_fault = ("control", 2)
        gate = SynchronizedMixedResetGate({data_fault, control_fault}, timeout=1.0)
        with ThreadPoolExecutor(max_workers=2) as executor:
            data = executor.submit(gate.wait_and_claim, data_fault)
            time.sleep(0.02)
            self.assertFalse(data.done(), "data fault must wait for the control fault")
            control = executor.submit(gate.wait_and_claim, control_fault)
            self.assertTrue(data.result(timeout=1.0))
            self.assertTrue(control.result(timeout=1.0))

    def test_duplicate_pump_cannot_claim_one_mixed_fault_twice(self):
        data_fault = ("data", 1)
        control_fault = ("control", 2)
        gate = SynchronizedMixedResetGate({data_fault, control_fault}, timeout=1.0)
        with ThreadPoolExecutor(max_workers=3) as executor:
            results = list(executor.map(gate.wait_and_claim,
                                        (data_fault, data_fault, control_fault)))
        self.assertEqual(sum(results), 2)
        self.assertEqual(gate.reset_participants, {data_fault, control_fault})

    def test_missing_control_fault_times_out_without_partial_reset(self):
        data_fault = ("data", 1)
        control_fault = ("control", 2)
        gate = SynchronizedMixedResetGate({data_fault, control_fault}, timeout=0.02)
        self.assertFalse(gate.wait_and_claim(data_fault))
        self.assertFalse(gate.wait_and_claim(control_fault))
        self.assertEqual(gate.reset_participants, set())


if __name__ == "__main__":
    unittest.main()
