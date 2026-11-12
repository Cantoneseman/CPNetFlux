#!/usr/bin/env python3
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"perf"))
from field_trial_owner import is_private_source_command

class FieldTrialOwnerTest(unittest.TestCase):
    root="/dev/shm/cpnetflux-s0-private55-83-source"
    exe="/home/Su/projects/CPNetFlux/build/cpnetflux-gridftp-server"
    def cmd(self,*args):return b"\0".join(x.encode() for x in args)+b"\0"
    def test_detached_source_command_owned_without_evidence_path(self):
        self.assertTrue(is_private_source_command(self.cmd(self.exe,"--root",self.root,"--port","49337"),self.root))
    def test_nearby_source_and_other_stage_are_not_owned(self):
        for root in (self.root+"-other",self.root.replace("83","82"),self.root+"/nested"):
            with self.subTest(root=root):self.assertFalse(is_private_source_command(self.cmd(self.exe,"--root",root),self.root))
    def test_unrelated_executable_cannot_claim_path(self):
        self.assertFalse(is_private_source_command(self.cmd("python3","--root",self.root),self.root))
    def test_text_mention_is_not_root_argument(self):
        self.assertFalse(is_private_source_command(self.cmd(self.exe,"--label",self.root),self.root))
    def test_missing_or_duplicate_root_is_rejected(self):
        for tail in (("--root",),("--root",self.root,"--root",self.root),()):
            with self.subTest(tail=tail):self.assertFalse(is_private_source_command(self.cmd(self.exe,*tail),self.root))

if __name__=="__main__":unittest.main()
