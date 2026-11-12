#!/usr/bin/env python3
"""Owned child procfs teardown races, active-process negatives and real collectors."""
import errno
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'perf'))
import run_small_file_baseline as base
import run_private_tmpfs_v3 as mapped

class ProcfsSamplingTest(unittest.TestCase):
    def child(self):
        p = subprocess.Popen([sys.executable, '-c', 'import sys;sys.stdin.buffer.read(1)'], stdin=subprocess.PIPE)
        self.addCleanup(self.reap, p)
        return p

    @staticmethod
    def reap(p):
        if p.poll() is None:
            p.terminate()
        p.wait(timeout=5)
        if p.stdin:
            p.stdin.close()

    @staticmethod
    def finish(p):
        p.stdin.write(b'x');p.stdin.flush()
        assert p.wait(timeout=5) == 0

    def test_real_stat_esrch_at_resource_collector_exit_keeps_successful_samples(self):
        p = self.child();state = base.resource_state();base.observe(p, state)
        fd = (Path('/proc') / str(p.pid) / 'stat').open()
        self.addCleanup(fd.close)
        self.finish(p)
        with self.assertRaises(ProcessLookupError):fd.read()
        fd.seek(0)
        original = Path.read_text
        def read(path, *a, **kw):
            if path == Path('/proc') / str(p.pid) / 'stat':return fd.read()
            return original(path, *a, **kw)
        previous = {k: state[k] for k in ['owned_swap_current_bytes','rss_peak_bytes','cpu_seconds_sampled_lower_bound']}
        with patch.object(Path, 'read_text', read):base.observe(p, state)
        self.assertEqual({k: state[k] for k in previous}, previous)
        self.assertEqual(state['process_samples'], 1)
        self.assertEqual(state['terminal_procfs_reads'][0]['errno'], errno.ESRCH)
        self.assertEqual(state['terminal_procfs_reads'][0]['confirmed_exit_code'], 0)

    def test_real_smaps_esrch_uses_actual_mapped_page_observer(self):
        p = self.child();state = base.resource_state()
        page = dict(samples=0, target_shmem_pmd_mapped_peak_bytes=0)
        mapped.observe_receiver_pages(p, state, page, Path('/not-a-target'))
        previous = dict(page)
        fd = (Path('/proc') / str(p.pid) / 'smaps').open();self.addCleanup(fd.close)
        self.finish(p)
        with self.assertRaises(ProcessLookupError):fd.read()
        fd.seek(0);original = Path.read_text
        def read(path, *a, **kw):
            if path == Path('/proc') / str(p.pid) / 'smaps':return fd.read()
            return original(path, *a, **kw)
        with patch.object(Path, 'read_text', read):mapped.observe_receiver_pages(p, state, page, Path('/not-a-target'))
        self.assertEqual(page, previous)
        self.assertEqual(state['terminal_procfs_reads'][0]['entry'], 'smaps')

    def test_gone_owned_process_without_successful_sample_stays_uncovered(self):
        p=self.child();self.finish(p);state=base.resource_state();base.observe(p,state)
        self.assertIsNone(state['owned_swap_current_bytes'])
        self.assertEqual(state['process_samples'],0)
        source={'mem_available_bytes':32*base.GIB,'process':{'swap_bytes':0},'host_pswpout_delta_since_seed':0}
        sender={'mem_available_min_bytes':32*base.GIB,'owned_swap_peak_bytes':0,'pswpout_delta':0}
        self.assertFalse(mapped.resource_gate(dict(state,pswpout_delta=0),sender,source,32*base.GIB,0))

    def test_active_owned_process_esrch_is_fatal(self):
        p=self.child();state=base.resource_state()
        original=Path.read_text
        def read(path,*a,**kw):
            if path==Path('/proc')/str(p.pid)/'stat':raise ProcessLookupError(errno.ESRCH,'real process still alive')
            return original(path,*a,**kw)
        with patch.object(Path,'read_text',read):
            with self.assertRaises(ProcessLookupError):base.observe(p,state)
        self.assertIsNone(p.poll());self.assertFalse(state.get('terminal_procfs_reads'))

    def test_active_owned_process_enoent_is_fatal(self):
        p=self.child();state=base.resource_state()
        original=Path.read_text
        def read(path,*a,**kw):
            if path==Path('/proc')/str(p.pid)/'stat':raise FileNotFoundError(errno.ENOENT,'still alive')
            return original(path,*a,**kw)
        with patch.object(Path,'read_text',read):
            with self.assertRaises(FileNotFoundError):base.observe(p,state)
        self.assertIsNone(p.poll())

    def test_permission_and_io_errors_remain_fatal_even_after_exit(self):
        import procfs_sampling as reader
        p=self.child();self.finish(p)
        for number in [errno.EACCES,errno.EPERM,errno.EIO]:
            with self.subTest(errno=number), patch.object(Path,'read_text',side_effect=OSError(number,'not an exit race')):
                with self.assertRaises(OSError):reader.read_owned_process_text(p,'smaps',{})

    def test_reader_rejects_non_proc_artifacts_and_traversal(self):
        import procfs_sampling as reader
        p=self.child()
        for entry in ['../status','missing-summary.json','fd','/tmp/summary','']:
            with self.subTest(entry=entry),self.assertRaises(ValueError):reader.read_owned_process_text(p,entry,{})

    def test_same_pid_different_start_ticks_is_fatal(self):
        p=self.child();state=base.resource_state();base.observe(p,state)
        value=base.proc_info(p.pid);value['start_ticks']+=1
        with patch.object(base,'proc_info',return_value=value):
            with self.assertRaisesRegex(RuntimeError,'identity changed'):base.observe(p,state)

    def test_procfs_race_cannot_erase_previously_observed_swap(self):
        p=self.child();state=base.resource_state();base.observe(p,state);self.finish(p)
        state['owned_swap_peak_bytes']=4096
        with self.assertRaisesRegex(AssertionError,'swap gate'):base.observe(p,state)
        self.assertEqual(state['owned_swap_peak_bytes'],4096)

    def test_run_guarded_real_exit_race_does_not_become_resource_failure(self):
        def observer(p,state):
            # Finish before the normal resource collector runs, retaining a real
            # procfs descriptor so the actual kernel returns ESRCH during read.
            if state.get('injected'):return
            base.observe(p,state);state['injected']=True
            fd=(Path('/proc')/str(p.pid)/'stat').open()
            p.wait(timeout=5)
            original=Path.read_text
            def read(path,*a,**kw):
                if path==Path('/proc')/str(p.pid)/'stat':return fd.read()
                return original(path,*a,**kw)
            try:
                with patch.object(Path,'read_text',read):base.observe(p,state)
            finally:fd.close()
        with tempfile.TemporaryDirectory(prefix='cpnetflux-procfs-test-') as d:
            log=Path(d)/'child.log'
            result=base.run_guarded([sys.executable,'-c','import time;time.sleep(.05)'],log,process_observer=observer)
            self.assertEqual(result['exit_code'],0)
            self.assertEqual(result['process_samples'],1)
            self.assertEqual(result['pswpout_delta'],0)
            self.assertFalse(log.with_name('child.resource-failure.json').exists())
            self.assertEqual(len(result['terminal_procfs_reads']),2)

if __name__=='__main__':unittest.main()
