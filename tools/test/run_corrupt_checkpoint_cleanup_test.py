#!/usr/bin/env python3
"""Exercise real checkpoint harness filesystem cleanup with failed endpoint startup."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import run_v3_corrupt_partial_checkpoint_smoke as smoke

class StartupFailure(RuntimeError):
    pass

class Endpoint:
    instances=[]
    behavior='startup_failure'
    def __init__(self,build,root,*args):
        self.local_server_root=root/'server-root'
        self.stopped=False
        self.instances.append(self)
    def prepare(self):
        self.local_server_root.mkdir(parents=True)
        if self.behavior=='prepare_failure':raise StartupFailure('prepare failed')
        if self.behavior=='existing':
            p=self.local_server_root/'uploaded/payload.bin'
            p.parent.mkdir();p.write_bytes(b'existing-unowned-data')
    def start_server(self):pass
    def start_proxy(self):
        if self.behavior=='replace':
            p=self.local_server_root/'uploaded/payload.bin'
            p.rename(p.with_name('retained-original.bin'))
            p.write_bytes(b'replacement-unowned-data')
        raise StartupFailure('proxy failed')
    def stop(self):self.stopped=True

class CleanupTests(unittest.TestCase):
    def setUp(self):
        Endpoint.instances=[];Endpoint.behavior='startup_failure'
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.source=self.root/'original/payload.bin'
        self.source.parent.mkdir();self.source.write_bytes(b'owned source bytes'*64)
        self.saved=self.source.read_bytes()
        self.marker=self.root/'keep.json';self.marker.write_text('retain diagnostics')
    def call(self):
        with patch.object(smoke,'TestEndpoint',Endpoint):
            return smoke.run_direction(self.root/'build',self.root/'case',self.source,'download')
    def assert_preserved(self):
        self.assertEqual(self.source.read_bytes(),self.saved)
        self.assertEqual(self.marker.read_text(),'retain diagnostics')
    def test_proxy_start_failure_releases_copy_and_stops_endpoint(self):
        with self.assertRaisesRegex(StartupFailure,'proxy failed'):self.call()
        endpoint=Endpoint.instances[0]
        self.assertFalse((endpoint.local_server_root/'uploaded/payload.bin').exists(),'download source leaked')
        self.assertTrue(endpoint.stopped,'partially started endpoint was not stopped')
        self.assert_preserved()
    def test_prepare_failure_still_stops_partially_prepared_endpoint(self):
        Endpoint.behavior='prepare_failure'
        with self.assertRaisesRegex(StartupFailure,'prepare failed'):self.call()
        self.assertTrue(Endpoint.instances[0].stopped)
        self.assert_preserved()
    def test_preexisting_copy_is_never_overwritten_or_deleted(self):
        Endpoint.behavior='existing'
        with self.assertRaises(FileExistsError):self.call()
        endpoint=Endpoint.instances[0]
        self.assertEqual((endpoint.local_server_root/'uploaded/payload.bin').read_bytes(),b'existing-unowned-data')
        self.assertTrue(endpoint.stopped)
        self.assert_preserved()
    def test_replaced_copy_is_preserved_and_cleanup_failure_is_reported(self):
        Endpoint.behavior='replace'
        with self.assertRaisesRegex(RuntimeError,'fixture identity changed'):self.call()
        endpoint=Endpoint.instances[0]
        self.assertEqual((endpoint.local_server_root/'uploaded/payload.bin').read_bytes(),b'replacement-unowned-data')
        self.assertTrue(endpoint.stopped)
        self.assert_preserved()
    def test_partial_source_generation_failure_cleans_owned_payload_and_records_error(self):
        def fail_generation(path,size):
            path.write_bytes(b'incomplete source')
            raise StartupFailure('source generation failed')
        evidence=self.root/'evidence'
        with patch.object(smoke,'create_payload',side_effect=fail_generation):
            with self.assertRaisesRegex(StartupFailure,'source generation failed'):
                smoke.run(self.root/'build',evidence,64)
        self.assertEqual(list(evidence.rglob('payload.bin')),[],'partial source leaked')
        failures=list(evidence.rglob('failure.txt'))
        self.assertEqual(len(failures),1)
        self.assertIn('source generation failed',failures[0].read_text())
        self.assert_preserved()

if __name__=='__main__':unittest.main()
