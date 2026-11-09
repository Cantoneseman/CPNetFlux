#!/usr/bin/env python3
"""Ownership and sample analysis checks; no privileged profiler in unit tests."""
from pathlib import Path
import os
import sys
import tempfile
import time
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
from owned_process_profile import OwnedProcess, analyze_samples, component_cpu, validate_identity, parse_script
import run_small_file_baseline as base


class OwnedProfileTest(unittest.TestCase):
    def test_identity_rejects_production_and_reused_pid(self):
        for pid in (126766,164834):
            with self.assertRaises(AssertionError):validate_identity(pid,1,{'pid':pid,'start_ticks':1})
        with self.assertRaises(AssertionError):validate_identity(100,1,{'pid':100,'start_ticks':2})
        validate_identity(100,1,{'pid':100,'start_ticks':1})

    def test_inclusive_categories_cannot_be_summed_as_wall(self):
        samples=[{'tid':1,'period':100,'symbols':['vfs_rename','saveDownloadManifestAtomic']},
                 {'tid':2,'period':300,'symbols':['crc32c','treeHashFromFiles']}]
        r=analyze_samples(samples)
        self.assertEqual(r['samples'],2)
        self.assertEqual(r['sampled_cpu_ns'],400)
        self.assertAlmostEqual(r['inclusive']['checkpoint']['fraction'],.25)
        self.assertAlmostEqual(r['inclusive']['publish']['fraction'],.25)
        self.assertAlmostEqual(r['inclusive']['summary']['fraction'],.75)
        self.assertIn('overlap',r['scope'])

    def test_empty_samples_fail(self):
        with self.assertRaises(AssertionError):analyze_samples([])

    def test_profiler_thread_inheritance_is_marked_as_observer_work(self):
        result=component_cpu([{'period':100,'symbols':['perf_event_alloc','kernel_clone']},
                              {'period':300,'symbols':['serializeTreeManifest']}])
        self.assertEqual(result['components']['profiler_thread_inheritance']['fraction'],.25)
        self.assertEqual(result['components']['thread_creation']['fraction'],.25)
        self.assertEqual(result['components']['tree_manifest_serialization']['fraction'],.75)
        self.assertIn('observer work',result['scope'])

    def test_sample_parser_retains_worker_identity_and_frames(self):
        samples,lost=parse_script('worker 100/102 123.456789012: 1000 cpu-clock:\n'
                                 '  ffffffff vfs_rename ([kernel.kallsyms])\n'
                                 '  12345678 saveDownloadManifestAtomic (/tmp/app)\n')
        self.assertFalse(lost);self.assertEqual(samples[0]['pid'],100)
        self.assertEqual(samples[0]['tid'],102)
        self.assertIn('saveDownloadManifestAtomic',samples[0]['symbols'])

    def test_gated_child_cannot_execute_early_and_close_reaps_it(self):
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-profile-test-',dir='/dev/shm') as d:
            root=Path(d);(root/base.MARKER).write_text(base.TOKEN)
            marker=root/'executed'
            argv=[sys.executable,'-c','from pathlib import Path; import sys,time; Path(sys.argv[1]).write_text("yes"); time.sleep(10)',str(marker)]
            child=OwnedProcess(argv,root/'child.log',root,root/'profile',False)
            try:
                self.assertFalse(marker.exists());child.release()
                deadline=time.monotonic()+3
                while not marker.exists():
                    self.assertLess(time.monotonic(),deadline);time.sleep(.005)
                self.assertEqual(marker.read_text(),'yes')
            finally:child.close()
            with self.assertRaises(FileNotFoundError):base.proc_info(child.process.pid)
            self.assertTrue(all(not (root/'profile'/name).exists() for name in ('ctl','ack','release')))


if __name__=='__main__':unittest.main()
