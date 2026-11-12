#!/usr/bin/env python3
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
import run_sender_pacing as driver

class TcpExactReadGateTest(unittest.TestCase):
    def rows(self):
        return [dict(channel_index=i,lane_index=0,tcp_exact_read=dict(enabled=True,large_read_calls=1024,
            recv_calls=1031,short_returns=5,bytes=1024*262148,interruptions=2)) for i in range(4)]
    def test_all_large_data_bytes_and_actual_calls_required(self):
        result=driver.validate_exact_read(self.rows())
        self.assertEqual(result['large_read_calls'],4096)
        self.assertEqual(result['bytes'],1073741824+4096*4)
        self.assertEqual(result['recv_calls'],4124)
    def test_missing_duplicate_or_inactive_lane_rejected(self):
        rows=self.rows()
        for bad in (rows[:3],rows+rows[:1],[rows[0]]*4):
            with self.assertRaises(AssertionError):driver.validate_exact_read(bad)
        for field,value in [('channel_index',4),('lane_index',1)]:
            bad=copy.deepcopy(rows);bad[0][field]=value
            with self.subTest(field=field):
                with self.assertRaises(AssertionError):driver.validate_exact_read(bad)
        rows[0]['tcp_exact_read']['enabled']=False
        with self.assertRaises(AssertionError):driver.validate_exact_read(rows)
    def test_partial_or_impossible_receipt_rejected(self):
        for key,value in [('large_read_calls',0),('large_read_calls',1023),
                ('recv_calls',0),('recv_calls',1024),('short_returns',-1),
                ('interruptions',-1),('bytes',1024*262148-1)]:
            rows=self.rows();rows[0]['tcp_exact_read'][key]=value
            with self.subTest(key=key,value=value):
                with self.assertRaises(AssertionError):driver.validate_exact_read(rows)

if __name__=='__main__':unittest.main()
