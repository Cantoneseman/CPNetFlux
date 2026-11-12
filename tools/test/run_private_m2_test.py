#!/usr/bin/env python3
"""Behavioral M2 experiment contract tests; never allocates an 8GiB fixture."""
import copy,importlib.util,math,sys,unittest
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
import private_tmpfs as private

class PrivateM2Tests(unittest.TestCase):
    def test_fixed_profiles_bound_capacity_and_keep_old_admission(self):
        self.assertTrue(hasattr(private,'profile_spec'),'bounded profile specification missing')
        old=private.profile_spec('admission')
        self.assertEqual((old['size_gib'],old['mount_limit_bytes'],old['huge_policy']),(1,1280*1024**2,'within_size'))
        for name,huge in [('m2-control','never'),('m2-candidate','within_size')]:
            spec=private.profile_spec(name)
            self.assertEqual(spec['size_gib'],8)
            self.assertEqual(spec['mount_limit_bytes'],8448*1024**2)
            self.assertEqual(spec['huge_policy'],huge)
            self.assertEqual(spec['preflight_mem_bytes'],int(21.25*1024**3))
        with self.assertRaises(ValueError):private.profile_spec('custom-unbounded')

    def test_large_profiles_require_native_and_matching_fixed_sender_policy(self):
        self.assertTrue(hasattr(private,'validate_profile'),'large profile validation missing')
        good=SimpleNamespace(profile='m2-candidate',native_setup=True,mode='v3',kernel_tcp_pacing=True)
        private.validate_profile(good)
        for key,value in [('native_setup',False),('kernel_tcp_pacing',False),('mode','smoke')]:
            bad=copy.copy(good);setattr(bad,key,value)
            with self.assertRaises(ValueError):private.validate_profile(bad)
        private.validate_profile(SimpleNamespace(profile='m2-control',native_setup=True,mode='v3',kernel_tcp_pacing=False))
        private.validate_profile(SimpleNamespace(profile='m2-control',native_setup=True,mode='setup-only',kernel_tcp_pacing=False))

    def test_row_driver_supplies_v3_context_before_validating_large_profile(self):
        from unittest.mock import patch
        import run_private_tmpfs_v3 as single
        class StopBeforeFilesystem(Exception):pass
        for profile in ('m2-control','m2-candidate'):
            args=SimpleNamespace(enable_private_tmpfs=True,profile=profile,native_setup=True,
                                 kernel_tcp_pacing=profile=='m2-candidate')
            def check(value):
                self.assertEqual(getattr(value,'mode',None),'v3','driver omitted its V3 context')
                raise StopBeforeFilesystem()
            with patch.object(single.base,'node',return_value=1), patch.object(single.private,'validate_profile',side_effect=check):
                with self.assertRaises(StopBeforeFilesystem):single.run(args)

    def test_memory_preflight_refuses_large_profile_before_creating_any_paths(self):
        import tempfile,uuid
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as folder:
            args=SimpleNamespace(profile='m2-candidate',native_setup=True,mode='v3',
                enable_private_tmpfs=True,kernel_tcp_pacing=True,commit='a'*40,
                payload_root=Path('/dev/shm/cpnetflux-s0-private55-m258-test-'+uuid.uuid4().hex),
                evidence=Path(folder)/'not-created')
            floor=private.profile_spec(args.profile)['preflight_mem_bytes']
            with patch.object(private,'available_memory',return_value=floor-1):
                with self.assertRaisesRegex(AssertionError,'memory preflight'):private.launch(args)
            self.assertFalse(args.payload_root.exists());self.assertFalse(args.evidence.exists())

    def test_large_allocation_requires_physical_free_headroom_before_any_paths(self):
        import tempfile,uuid
        from unittest.mock import patch
        self.assertTrue(hasattr(private,'free_memory'),'physical-free preflight missing')
        with tempfile.TemporaryDirectory() as folder:
            args=SimpleNamespace(profile='m2-candidate',native_setup=True,mode='v3',
                enable_private_tmpfs=True,kernel_tcp_pacing=True,commit='a'*40,
                payload_root=Path('/dev/shm/cpnetflux-s0-private55-free58-'+uuid.uuid4().hex),
                evidence=Path(folder)/'not-created')
            spec=private.profile_spec(args.profile)
            with patch.object(private,'available_memory',return_value=32*1024**3), \
                 patch.object(private,'free_memory',return_value=spec['mount_limit_bytes']+1024**3-1):
                with self.assertRaisesRegex(AssertionError,'physical free'):private.launch(args)
            self.assertFalse(args.payload_root.exists());self.assertFalse(args.evidence.exists())

    def test_reproducible_source_is_identical_without_changing_default_fixture(self):
        import run_large_data_ab as large
        self.assertTrue(hasattr(large,'source_block'),'reproducible source fixture missing')
        first=large.source_block(True);second=large.source_block(True)
        self.assertEqual(len(first),8*1024**2);self.assertEqual(first,second)
        self.assertNotEqual(first,large.source_block(False))

    def module(self):
        self.assertIsNotNone(importlib.util.find_spec('run_private_tmpfs_m2'),'formal matrix driver missing')
        import run_private_tmpfs_m2
        return run_private_tmpfs_m2

    def rows(self):
        rows=[]
        for rep in range(1,6):
            for arm in (('control','candidate') if rep%2 else ('candidate','control')):
                seconds=1.9 if arm=='control' else 1.65
                rows.append(dict(rep=rep,arm=arm,commit='a'*40,bytes=8*1024**3,files=1,
                    sha256='b'*64,components=dict(setup=.008,directory=.001,client=seconds-.009),
                    complete_target_task_seconds=seconds,
                    gates={k:True for k in ('network','resources','content','pages','kernel_pacing',
                        'native_setup','cleanup','no_failures','child_ok')},
                    verified_chunks=8192,data_frames=32768))
        return rows

    def test_formal_gate_requires_ten_interleaved_same_data_tasks_and_recovery(self):
        m=self.module();rows=self.rows()
        report=m.evaluate(rows,recovery_gate=True,scene_gate=True)
        self.assertTrue(report['m2_pass']);self.assertEqual(report['improved_pairs'],5)
        self.assertAlmostEqual(report['arms']['candidate']['median_gbps'],8*1024**3*8/1.65/1e9)
        self.assertFalse(m.evaluate(rows,recovery_gate=False,scene_gate=True)['m2_pass'])
        self.assertFalse(m.evaluate(rows,recovery_gate=True,scene_gate=False)['m2_pass'])
        for values in (rows[:-1],list(reversed(rows))):
            with self.assertRaises(ValueError):m.evaluate(values,recovery_gate=True,scene_gate=True)
        for key,value in [('bytes',1024**3),('sha256','c'*64),('verified_chunks',8191),
                          ('data_frames',32767),('commit','c'*40),('complete_target_task_seconds',float('nan'))]:
            values=copy.deepcopy(rows);values[3][key]=value
            with self.subTest(key=key):
                with self.assertRaises(ValueError):m.evaluate(values,recovery_gate=True,scene_gate=True)

    def test_failure_and_missing_gate_stop_before_next_task_without_retry(self):
        m=self.module()
        for mutation in ('network','resources','missing','bad_duration'):
            rows=self.rows();calls=[];saved=[]
            def run_one(rep,arm):
                calls.append((rep,arm));row=copy.deepcopy(rows[len(calls)-1])
                if len(calls)==2:
                    if mutation=='missing':del row['gates']['content']
                    elif mutation=='bad_duration':row['components']['setup']=-1
                    else:row['gates'][mutation]=False
                return row
            with self.subTest(mutation=mutation):
                with self.assertRaises(ValueError):m.collect_rows(run_one,lambda values:saved.append(list(values)))
                self.assertEqual(len(calls),2);self.assertEqual(len(saved[-1]),1)

    def test_rate_and_variance_gates_do_not_promote_fast_single_sample(self):
        m=self.module();rows=self.rows()
        # Median below 40 despite one fast candidate is a NO-GO.
        for i,row in enumerate(rows):
            if row['arm']=='candidate':
                row['complete_target_task_seconds']=2 if i!=1 else 1.4
                row['components']['client']=row['complete_target_task_seconds']-.009
        report=m.evaluate(rows,recovery_gate=True,scene_gate=True)
        self.assertFalse(report['m2_pass'])
        self.assertGreater(report['arms']['candidate']['time_cv'],.05)

if __name__=='__main__':unittest.main()
