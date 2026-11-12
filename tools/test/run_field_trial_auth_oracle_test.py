#!/usr/bin/env python3
import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"perf"))
from field_trial_auth_oracle import validate_auth_rejection

class FieldTrialAuthOracleTest(unittest.TestCase):
    def valid(self):
        return dict(returncode=1, summary=dict(result="fail", bytes_total=0, bytes_transferred=0,
            file_count=0,error=dict(error_code="auth_failed",message="PASS rejected")), published_files=[])
    def test_actual_stage81_message_is_valid_rejection(self):
        self.assertTrue(validate_auth_rejection(**self.valid())["passed"])
    def test_diagnostic_text_is_not_the_contract(self):
        x=self.valid();x["summary"]["error"]["message"]="credential denied"
        self.assertTrue(validate_auth_rejection(**x)["passed"])
    def test_other_failures_cannot_pass_even_if_text_mentions_authentication(self):
        for code in ("tls_failed","data_tls_failed","transport_error","timeout",None):
            x=self.valid();x["summary"]["error"]=dict(error_code=code,message="authentication 530 PASS rejected")
            with self.subTest(code=code),self.assertRaises(ValueError):validate_auth_rejection(**x)
    def test_success_signal_or_deadline_cannot_pass(self):
        for rc,expired in ((0,False),(-15,False),(1,True)):
            x=self.valid();x.update(returncode=rc,deadline_expired=expired)
            with self.subTest(rc=rc,expired=expired),self.assertRaises(ValueError):validate_auth_rejection(**x)
    def test_failed_result_and_structured_error_required(self):
        for change in ({"result":"ok"},{"error":None},{}):
            x=self.valid()
            if change:x["summary"].update(change)
            else:x["summary"]={}
            with self.subTest(change=change),self.assertRaises(ValueError):validate_auth_rejection(**x)
    def test_inventory_or_transferred_payload_cannot_pass(self):
        for key in ("bytes_total","bytes_transferred","file_count"):
            x=self.valid();x["summary"][key]=1
            with self.subTest(key=key),self.assertRaises(ValueError):validate_auth_rejection(**x)
    def test_missing_counters_are_not_zero(self):
        x=self.valid();del x["summary"]["bytes_total"]
        with self.assertRaises(ValueError):validate_auth_rejection(**x)
    def test_published_files_or_missing_external_oracle_rejected(self):
        for files in (["unexpected"],None):
            x=self.valid();x["published_files"]=files
            with self.subTest(files=files),self.assertRaises(ValueError):validate_auth_rejection(**x)

if __name__=="__main__":unittest.main()
