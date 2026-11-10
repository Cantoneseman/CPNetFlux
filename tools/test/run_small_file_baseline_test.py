#!/usr/bin/env python3
"""Filesystem ownership and measurement-scope regression checks for S0 driver."""
import importlib.util
import json
from pathlib import Path
import tempfile
import sys
import time
import unittest
from types import SimpleNamespace
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('s0', Path(__file__).resolve().parents[1]/'perf/run_small_file_baseline.py')
s0 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(s0)

class BaselineTest(unittest.TestCase):
    def test_observer_failure_still_reaps_the_owned_child(self):
        seen=[]
        def observer(proc,state):
            seen.append(proc.pid)
            raise RuntimeError('diagnostic failed')
        with tempfile.TemporaryDirectory(prefix='cpnetflux-observer-test-') as directory:
            log=Path(directory)/'child.log'
            with self.assertRaisesRegex(RuntimeError,'diagnostic failed'):
                s0.run_guarded([sys.executable,'-c','import time;time.sleep(10)'],log,process_observer=observer)
            failure=json.loads(log.with_name('child.resource-failure.json').read_text())
        self.assertEqual(len(seen),1)
        with self.assertRaises(FileNotFoundError):s0.proc_info(seen[0])
        self.assertEqual(failure['failure'],"RuntimeError('diagnostic failed')")
        self.assertIn('exit_code',failure)
        self.assertIn('pswpout_before',failure)
        self.assertIn('pswpout_after',failure)
        self.assertIn('pswpout_delta',failure)
        self.assertIn('samples',failure)

    def test_resource_gate_failure_sidecar_keeps_final_sample(self):
        seen=[]
        def fail_swap_gate(proc,state):
            seen.append(proc.pid)
            state['owned_swap_current_bytes']=4096
            state['owned_swap_peak_bytes']=4096
            state['samples']+=1
            raise AssertionError('swap gate')
        with tempfile.TemporaryDirectory(prefix='cpnetflux-resource-test-') as directory:
            log=Path(directory)/'child.log'
            with patch.object(s0,'observe',fail_swap_gate):
                with self.assertRaisesRegex(AssertionError,'swap gate'):
                    s0.run_guarded([sys.executable,'-c','import time;time.sleep(10)'],log)
            failure=json.loads(log.with_name('child.resource-failure.json').read_text())
        self.assertEqual(len(seen),1)
        with self.assertRaises(FileNotFoundError):s0.proc_info(seen[0])
        self.assertEqual(failure['owned_swap_current_bytes'],4096)
        self.assertEqual(failure['owned_swap_peak_bytes'],4096)
        self.assertEqual(failure['samples'],1)
        self.assertEqual(failure['failure'],"AssertionError('swap gate')")
        self.assertIn('exit_code',failure)
        self.assertIn('pswpout_before',failure)
        self.assertIn('pswpout_after',failure)
        self.assertIn('pswpout_delta',failure)

    def test_client_receipt_keeps_resource_failure_after_owned_cleanup(self):
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-receipt-test-',dir='/dev/shm') as directory:
            root=Path(directory)
            (root/s0.MARKER).write_text(s0.TOKEN)
            evidence=root/'evidence';evidence.mkdir()
            args=SimpleNamespace(payload_root=root,evidence=evidence,size=1,count=1)
            failure={'failure':"AssertionError('swap gate')",'owned_swap_peak_bytes':4096,
                     'pswpout_before':10,'pswpout_after':10,'pswpout_delta':0,'samples':1}
            def runner(command,log,**options):
                log.write_text('controlled failure\n')
                failure_path=log.with_name(log.stem+'.resource-failure.json')
                s0.save(failure_path,failure)
                error=AssertionError('swap gate')
                error.resource_state=failure
                error.resource_failure_path=str(failure_path)
                raise error
            with patch.object(s0,'snapshot',side_effect=[{'pswpout':10},{'pswpout':11}]), \
                 patch.object(s0,'compact_log',return_value={}):
                with self.assertRaisesRegex(AssertionError,'swap gate'):
                    s0.client_sample(args,'failed-sample',{'sha256':'unused','files':1,'bytes':1},
                                     'auto','off',49210,runner=runner)
            receipt=json.loads((evidence/'samples'/'failed-sample.receipt.json').read_text())
            self.assertEqual(receipt['result'],'fail')
            self.assertEqual(receipt['error'],"AssertionError('swap gate')")
            self.assertEqual(receipt['resource_failure'],failure)
            self.assertEqual(receipt['before'],{'pswpout':10})
            self.assertEqual(receipt['after_failure'],{'pswpout':11})
            self.assertTrue(receipt['output_slot_removed'])
            self.assertFalse((root/'failed-sample').exists())

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

    def test_receiver_profile_aggregation_keeps_counts_cpu_and_peak_capacity(self):
        rows=[
            {'receiver_payload_observation':{
                'schema':'payload-v2','scope':'payload scope','data_frame_count':10,
                'frame_payload_peak_capacity_bytes':4096,'logical_copy_bytes':1024,
                'logical_copy_seconds':.01},
             'receiver_cpu_observation':{
                'schema':'cpu-v1','scope':'cpu scope','write_cpu_owner':'writer',
                'physical_write_calls':12,'write_cpu_seconds':.02}},
            {'receiver_payload_observation':{
                'schema':'payload-v2','scope':'payload scope','data_frame_count':20,
                'frame_payload_peak_capacity_bytes':8192,'logical_copy_bytes':2048,
                'logical_copy_seconds':.02},
             'receiver_cpu_observation':{
                'schema':'cpu-v1','scope':'cpu scope','write_cpu_owner':'writer',
                'physical_write_calls':18,'write_cpu_seconds':.03}},
        ]
        profile=s0.phases(rows)['receiver_profile']
        self.assertEqual(profile['payload']['ranges'],2)
        self.assertEqual(profile['cpu']['ranges'],2)
        self.assertEqual(profile['payload']['schema'],'payload-v2')
        self.assertEqual(profile['payload']['scope'],'payload scope')
        self.assertEqual(profile['payload']['totals']['data_frame_count'],30)
        self.assertEqual(profile['payload']['totals']['logical_copy_bytes'],3072)
        self.assertAlmostEqual(profile['payload']['totals']['logical_copy_seconds'],.03)
        self.assertEqual(profile['payload']['peak']['frame_payload_peak_capacity_bytes'],8192)
        self.assertEqual(profile['cpu']['totals']['physical_write_calls'],30)
        self.assertAlmostEqual(profile['cpu']['totals']['write_cpu_seconds'],.05)
        self.assertEqual(profile['cpu']['write_cpu_owners'],['writer'])

    def test_receiver_profile_environment_is_explicit_and_ignores_inherited_values(self):
        with patch.dict(s0.os.environ, {
            'CPNETFLUX_RECEIVER_PROFILE':'1',
            'CPNETFLUX_RECEIVER_CPU_PROFILE':'1',
            'CPNETFLUX_RANGE_TIMING':'on',
        }):
            normal=s0.environment()
            diagnostic=s0.environment(receiver_profile=True)
        self.assertEqual(normal['CPNETFLUX_RECEIVER_PROFILE'],'0')
        self.assertEqual(normal['CPNETFLUX_RECEIVER_CPU_PROFILE'],'0')
        self.assertEqual(normal['CPNETFLUX_RANGE_TIMING'],'off')
        self.assertEqual(diagnostic['CPNETFLUX_RECEIVER_PROFILE'],'1')
        self.assertEqual(diagnostic['CPNETFLUX_RECEIVER_CPU_PROFILE'],'1')
        self.assertEqual(diagnostic['CPNETFLUX_RANGE_TIMING'],'on')

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
