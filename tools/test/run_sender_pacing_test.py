#!/usr/bin/env python3
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
import run_sender_pacing as driver
import run_small_file_baseline as base

class SenderPacingGateTest(unittest.TestCase):
    def receipts(self):
        return [dict(lane=i,rate_bytes_per_second=1500000000,frames=1024,
            wait_backend='timerfd',timer_fd_creates=1,timer_arm_calls=900,timer_read_calls=900,
            wire_bytes=1024*262212,waits=900,sleep_calls=900,wait_ns=180000000,
            required_spacing_ns=1023*174808,spacing_violations=0,
            first_admission_ns=1000000000,last_admission_ns=1200000000) for i in range(4)]
    def test_full_wire_coverage_and_actual_wait_required(self):
        result=driver.validate_receipts(self.receipts())
        self.assertEqual(result['frames'],4096)
        self.assertEqual(result['wire_bytes'],base.GIB+4096*68)
    def test_missing_lane_or_duplicate_receipt_rejected(self):
        for rows in (self.receipts()[:3], self.receipts()+self.receipts()[:1]):
            with self.assertRaises(AssertionError):driver.validate_receipts(rows)
    def test_inactive_pacer_or_rate_violation_cannot_pass(self):
        for key,value in [('waits',0),('wait_ns',0),('spacing_violations',1),
                          ('last_admission_ns',1000000001),('rate_bytes_per_second',1400000000),
                          ('required_spacing_ns',1)]:
            with self.subTest(key=key):
                rows=self.receipts();rows[0][key]=value
                with self.assertRaises(AssertionError):driver.validate_receipts(rows)
    def test_partial_or_unpaced_byte_coverage_rejected(self):
        for key in ('frames','wire_bytes'):
            rows=self.receipts();rows[0][key]-=1
            with self.assertRaises(AssertionError):driver.validate_receipts(rows)
    def test_wait_backend_and_bounded_timer_usage_required(self):
        for key,value in [('wait_backend','std_sleep_for'),('timer_fd_creates',0),
                          ('timer_fd_creates',2),('timer_arm_calls',0),('timer_read_calls',0)]:
            with self.subTest(key=key,value=value):
                rows=self.receipts();rows[0][key]=value
                with self.assertRaises(AssertionError):driver.validate_receipts(rows)
    def test_log_compaction_preserves_pacing_receipts(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'server.log'
            rows=self.receipts()
            path.write_text(''.join('CPNETFLUX_SENDER_PACING '+json.dumps(row)+'\n' for row in rows))
            compact=base.compact_log(path)
            self.assertEqual(compact['sender_pacing'],rows)
            self.assertTrue(Path(str(path)+'.gz').exists())
            self.assertFalse(path.exists())

if __name__=='__main__':unittest.main()
