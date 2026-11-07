#!/usr/bin/env python3
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('guard', root / 'tools/perf/cpnetflux_memory_guard.py')
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class MemoryGuardTest(unittest.TestCase):
    def test_only_exact_project_binary_and_owned_path_match(self):
        project, payload = Path('/home/Su/projects/CPNetFlux'), Path('/dev/shm/cpnetflux-test')
        binary = str(project / 'build/cpnetflux-tree-download-client')
        self.assertTrue(guard.owned_arguments([binary, '--dest-dir', str(payload / 'output')], project, payload))
        self.assertFalse(guard.owned_arguments([binary, str(payload) + '-other/output'], project, payload))
        self.assertFalse(guard.owned_arguments(['/other/cpnetflux-tree-download-client', str(payload)], project, payload))
        self.assertFalse(guard.owned_arguments(['python3', str(payload)], project, payload))
        self.assertFalse(guard.owned_arguments([], project, payload))

    def test_upload_sender_requires_exact_project_and_owned_source(self):
        project, payload = Path('/home/Su/projects/CPNetFlux'), Path('/dev/shm/cpnetflux-test')
        binary = str(project / 'build/cpnetflux-tree-upload-client')
        self.assertTrue(guard.owned_arguments([binary, '--source-dir', str(payload / 'input')], project, payload))
        self.assertFalse(guard.owned_arguments([binary, '--source-dir', str(payload) + '-neighbor/input'], project, payload))
        self.assertFalse(guard.owned_arguments(['/other/cpnetflux-tree-upload-client', str(payload)], project, payload))

    def test_available_boundary_and_combined_rss_budget(self):
        self.assertIsNone(guard.violation(6, [{'rss_bytes': 2}, {'rss_bytes': 2}], 6, 4))
        self.assertEqual(guard.violation(5, [], 6, 4), 'host_available_below_minimum')
        self.assertEqual(guard.violation(7, [{'rss_bytes': 3}, {'rss_bytes': 2}], 6, 4),
                         'owned_process_rss_above_maximum')

    @unittest.skipUnless(hasattr(os, 'pidfd_open'), 'Linux pidfd required')
    def test_termination_preserves_neighbor_and_changed_process_identity(self):
        with tempfile.TemporaryDirectory(prefix='cpnetflux-guard-test-') as temp:
            project, payload = Path(temp), Path('/dev/shm/cpnetflux-guard-unit')
            binary = str(project / 'build/cpnetflux-gridftp-server')
            children = [subprocess.Popen([binary, '-c', 'import time; time.sleep(60)', target],
                                         executable=sys.executable)
                        for target in [str(payload), str(payload) + '-neighbor']]
            try:
                deadline = time.monotonic() + 5
                items = []
                while not items and time.monotonic() < deadline:
                    items = guard.processes(project, payload)
                    time.sleep(0.01)
                self.assertEqual([row['pid'] for row in items], [children[0].pid])
                stale = dict(items[0], start_ticks=items[0]['start_ticks'] - 1)
                self.assertEqual(guard.terminate_owned([stale], project, payload), [])
                self.assertIsNone(children[0].poll())
                self.assertEqual(guard.terminate_owned(items, project, payload), [children[0].pid])
                self.assertEqual(children[0].wait(timeout=5), -15)
                self.assertIsNone(children[1].poll())
            finally:
                for child in children:
                    if child.poll() is None:
                        child.terminate()
                    child.wait(timeout=5)


if __name__ == '__main__':
    unittest.main()
