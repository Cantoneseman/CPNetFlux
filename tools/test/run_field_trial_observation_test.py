#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"perf"))
from field_trial_observation import merge_process_observation, executable_was_observed

class FieldTrialObservationTest(unittest.TestCase):
    exe="cpnetflux-tree-download-client"
    def row(self,exe,pid=123,start=456,rss=100):
        return dict(pid=pid,start_ticks=start,executable=exe,rss_bytes=rss,swap_bytes=0,fd=4)
    def test_exit_empty_name_cannot_erase_real_executable(self):
        first=merge_process_observation(None,self.row(self.exe))
        last=merge_process_observation(first,self.row("",rss=0))
        self.assertEqual(last["executable"],"")
        self.assertEqual(last["rss_bytes"],0)
        self.assertTrue(executable_was_observed({"123:456":last},self.exe))
    def test_never_observed_client_cannot_pass(self):
        last=None
        for image in ("env","python3",""):last=merge_process_observation(last,self.row(image))
        self.assertFalse(executable_was_observed({"123:456":last},self.exe))
    def test_pid_reuse_or_different_pid_cannot_merge(self):
        first=self.row(self.exe)
        for row in (self.row("",start=457),self.row("",pid=124)):
            with self.subTest(row=row),self.assertRaises(ValueError):merge_process_observation(first,row)
    def test_exec_sequence_keeps_actual_images_once(self):
        last=None
        for image in ("env",self.exe,self.exe,""):last=merge_process_observation(last,self.row(image))
        self.assertEqual(last["observed_executables"],sorted(["env",self.exe]))
    def test_merging_does_not_mutate_evidence_or_resource_snapshot(self):
        old=self.row(self.exe,rss=100);new=self.row("",rss=0)
        result=merge_process_observation(old,new)
        self.assertNotIn("observed_executables",old);self.assertNotIn("observed_executables",new)
        self.assertEqual(result["rss_bytes"],0)
    def test_other_executable_history_cannot_pass(self):
        p=merge_process_observation(None,self.row("cpnetflux-tree-upload-client"))
        self.assertFalse(executable_was_observed({"123:456":p},self.exe))
    def test_existing_nonempty_legacy_snapshot_is_valid(self):
        self.assertTrue(executable_was_observed({"123:456":self.row(self.exe)},self.exe))

if __name__=="__main__":unittest.main()
