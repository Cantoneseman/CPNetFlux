#!/usr/bin/env python3
"""Filesystem ownership and measurement-scope regression checks for S0 driver."""
import importlib.util
from pathlib import Path
import tempfile
import sys
import time
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('s0', Path(__file__).resolve().parents[1]/'perf/run_small_file_baseline.py')
s0 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s0)

class BaselineTest(unittest.TestCase):
    def test_timeout_reaps_only_its_owned_child(self):
        seen=[]
        original=s0.observe
        def observe(proc, state):
            seen.append(proc.pid)
            original(proc,state)
        with tempfile.TemporaryDirectory(prefix='cpnetflux-timer-test-') as d:
            with patch.object(s0,'observe',observe):
                with self.assertRaisesRegex(AssertionError,'owned process timeout'):
                    s0.run_guarded([sys.executable,'-c','import time; time.sleep(10)'],Path(d)/'child.log',timeout=.03)
            self.assertTrue(seen)
            with self.assertRaises(FileNotFoundError):s0.proc_info(seen[0])

    def test_task_wall_excludes_observer_work_after_child_exit(self):
        def slow_observer(proc, state):
            proc.wait(timeout=5)
            time.sleep(.15)
        with tempfile.TemporaryDirectory(prefix='cpnetflux-timer-test-') as d:
            started=time.monotonic()
            with patch.object(s0,'observe',slow_observer):
                result=s0.run_guarded([sys.executable,'-c','import time; time.sleep(.05)'],Path(d)/'child.log')
            outer_wall=time.monotonic()-started
            self.assertEqual(result['exit_code'],0)
            self.assertGreater(outer_wall-result['wall_seconds'],.10)
            self.assertGreater(result['observer_seconds_after_exit'],.10)

    def test_matrix_has_seven_bounded_cases(self):
        values = s0.cases()
        self.assertEqual(len(values), 7)
        self.assertTrue(all(size*count <= 1024**3 for size, count in values))
        self.assertNotIn((256*1024,10000), values)

    def test_owned_path_rejects_traversal_root_and_symlinks(self):
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-test-',dir='/dev/shm') as d:
            root = Path(d)
            (root/'.cpnetflux-s0-owned').write_text('cpnetflux-s0-v1\n')
            for name in ('', '.', '..', '../elsewhere', '/tmp', 'a/b'):
                with self.assertRaises((ValueError, AssertionError)):
                    s0.owned(root, name)
            (root/'link').symlink_to('/tmp')
            with self.assertRaises((ValueError, AssertionError)):
                s0.owned(root,'link')
            self.assertEqual(s0.owned(root,'sample-01'),root/'sample-01')

    def test_cleanup_only_removes_marked_child(self):
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-test-',dir='/dev/shm') as d:
            root=Path(d)
            with self.assertRaises((ValueError, AssertionError)):
                s0.remove_owned(root,root/'slot')
            (root/'.cpnetflux-s0-owned').write_text('cpnetflux-s0-v1\n')
            slot=root/'slot';slot.mkdir();(slot/'payload').write_bytes(b'payload')
            keep=root/'keep';keep.write_text('keep')
            s0.remove_owned(root,slot)
            self.assertFalse(slot.exists())
            self.assertEqual(keep.read_text(),'keep')
            with self.assertRaises((ValueError, AssertionError)):
                s0.remove_owned(root,root)

    def test_phase_aggregation_preserves_overlap_and_quantiles(self):
        rows=[{'wall_seconds':2.,'read_seconds':.2,'finalize_seconds':None},
              {'wall_seconds':4.,'read_seconds':.4,'finalize_seconds':None}]
        result=s0.phases(rows)
        self.assertEqual(result['count'],2)
        self.assertEqual(result['wall_seconds']['sum'],6.)
        self.assertEqual(result['wall_seconds']['median'],3.)
        self.assertAlmostEqual(result['wall_seconds']['p95'],3.9)
        self.assertNotIn('finalize_seconds',result)
        self.assertIn('overlap',result['scope'])

if __name__=='__main__':
    unittest.main()
