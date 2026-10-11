#!/usr/bin/env python3
import importlib.util, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod);return mod
runner=module('experiment_runner','tools/perf/run_v3_gridftp_experiment.py')
peer=module('experiment_peer','tools/perf/v3_experiment_peer.py')
class ExperimentTest(unittest.TestCase):
    def test_path_and_case_boundaries(self):
        for name in ('../x','/tmp/x','a/b','','a b'):
            with self.assertRaises(ValueError):peer.safe_case(name)
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            (root/'cp').mkdir();(root/'grid').mkdir()
            outside=root/'outside';outside.mkdir()
            (root/'cp'/'escape').symlink_to(outside,target_is_directory=True)
            with self.assertRaises(ValueError):peer.operation(root,{'op':'remove','case':'escape'})
            self.assertTrue(outside.exists())
    def test_hash_excludes_checkpoints_and_partial(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'a.bin').write_bytes(b'abc')
            (p/'a.bin.cpnetflux.manifest').write_text('state=committed\n')
            (p/'a.bin.part.foo').write_bytes(b'xyz')
            self.assertEqual(peer.payload_hash(p),runner.payload_hash(p))
            self.assertEqual(peer.payload_hash(p)[0][:2],['a.bin',3])
            self.assertEqual(len(peer.payload_hash(p)),1)
    def test_budget_and_security(self):
        with tempfile.TemporaryDirectory() as d:
            a=runner.Config(Path(d),Path(d),Path(d)/'ca.pem')
            single=runner.grid_command(a,Path(d)/'single','x','single')
            dense=runner.grid_command(a,Path(d)/'dense','x','dense')
            self.assertIn('-dcpriv',single);self.assertNotIn('-nodcau',single)
            self.assertEqual(single[single.index('-p')+1],'4')
            self.assertEqual(single[single.index('-cc')+1],'1')
            self.assertEqual(dense[dense.index('-p')+1],'1')
            self.assertEqual(dense[dense.index('-cc')+1],'4')
            cp=runner.tree_command(a,Path(d),'x',4,'dynamic',Path(d)/'s.json')
            self.assertEqual(cp[cp.index('--file-parallelism')+1],'4')
            self.assertEqual(cp[cp.index('--connections')+1],'1')
            self.assertEqual(cp[cp.index('--data-tls-mode')+1],'required')
            self.assertEqual(cp[cp.index('--tls-mode')+1],'required')
            self.assertEqual(cp[cp.index('--compression')+1],'off')
    def test_resume_criterion_requires_skips_and_corruption_repair(self):
        good={'skipped_chunks':2,'removed_corrupt_chunks':1,'bytes_sent':100}
        runner.validate_resume(good,200)
        for bad in ({**good,'skipped_chunks':0},{**good,'bytes_sent':200}):
            with self.assertRaises(RuntimeError):runner.validate_resume(bad,200)
    def test_sparse_corruption_uses_verified_offset(self):
        with tempfile.TemporaryDirectory() as d:
            r=Path(d);p=r/'cp'/'resume';p.mkdir(parents=True)
            partial=p/'a.part.id';partial.write_bytes(b'0123456789')
            (p/'a.cpnetflux.manifest').write_text('state=transferring\nverified_chunks=1:6:2:crc32c:00000000\n')
            result=peer.operation(r,{'op':'corrupt','case':'resume'})
            self.assertEqual(result['corrupted_offset'],6)
            self.assertEqual(partial.read_bytes()[:6],b'012345')
            self.assertNotEqual(partial.read_bytes()[6],ord('6'))
    def test_identity_and_wire_range_evidence(self):
        runner.validate_tree_summary({'actual_transport':'v3','actual_mode':'persistent_tree',
            'file_count':1,'completed_files':1,'failed_files':0,'file_timings':[{'range_id':1},{'range_id':2}]},1,True)
        with self.assertRaises(RuntimeError):
            runner.validate_tree_summary({'actual_transport':'v3','actual_mode':'persistent_tree',
            'file_count':1,'completed_files':1,'failed_files':0,'file_timings':[{'range_id':0}]},1,True)
if __name__=='__main__':unittest.main()
