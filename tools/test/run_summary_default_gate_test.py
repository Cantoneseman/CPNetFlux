#!/usr/bin/env python3
"""Large-file default promotion must preserve pairs, work and integrity evidence."""
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
import run_summary_default_gate as gate
from run_summary_default_gate import evaluate, residency, verify_chunks, verify_ranges


def rows(candidate=1):
    return [{'rep':rep,'summary_path_request':mode,'t_ready_seconds':1 if mode=='original' else candidate,
             'result':'pass','output_slot_removed':True,
             'summary':{'tree_hash':'crc32c:12345678','summary_tree_hash_source':'verified_chunks',
                        'summary_path_requested':mode,'summary_path_effective':'not_used'}}
            for rep in range(1,6) for mode in ('original','cached')]


class DefaultGateTest(unittest.TestCase):
    def test_neutral_reuse_passes_without_speedup_requirement(self):
        self.assertTrue(evaluate(rows())['gate_pass'])
        self.assertFalse(evaluate(rows(1.06))['gate_pass'])

    def test_complete_exact_pairs_required(self):
        for rs in (rows()[:-1],rows()+rows()[:1]):
            with self.assertRaisesRegex(AssertionError,'five complete pairs'):evaluate(rs)

    def test_unstable_arm_fails_without_dropping_outlier(self):
        rs=rows();rs[0]['t_ready_seconds']=2
        self.assertFalse(evaluate(rs)['gate_pass'])

    def test_reuse_and_hash_must_match(self):
        rs=rows();rs[0]['summary']['summary_tree_hash_source']='filesystem_read'
        with self.assertRaises(AssertionError):evaluate(rs)
        rs=rows();rs[0]['summary']['tree_hash']='bad'
        with self.assertRaises(AssertionError):evaluate(rs)

    def test_checkpoint_requires_exact_nonoverlapping_coverage(self):
        text='state=committed\nchunk_size=1048576\ncompleted_ranges=0-2097152\nverified_chunks=0:0:1048576:crc32c:12345678,2:1048576:1048576:crc32c:abcdef01\n'
        self.assertEqual(verify_chunks(text,2*1024**2),2)
        with self.assertRaises(AssertionError):verify_chunks(text.replace('2:1048576:','2:0:'),2*1024**2)
        with self.assertRaises(AssertionError):verify_chunks(text.replace('crc32c:abcdef01','crc32c:bad'),2*1024**2)

    def test_wrong_backend_or_missing_range_cannot_pass(self):
        with self.assertRaises(AssertionError):verify_ranges([])
        rs=[{'offset':i*1024**3,'length':1024**3,'receiver_payload_execution':'inline',
             'sender_payload_execution':'not_sender','receiver_write_backend':'mmap_experimental',
             'range_observation_enabled':False,'lane_thread_cpu_seconds':0,
             'sender_cpu_observation':None,'receiver_cpu_observation':None,
             'mapped_write':{'window_bound_bytes':64*1024**2,'concurrent_window_bound_bytes':256*1024**2,
                 'map_calls':16,'unmap_calls':16,'map_failures':0,'active_mapped_bytes':0,
                 'written_bytes':1024**3,'write_calls':17408,'peak_mapped_bytes':64*1024**2,'profile':None}}
            for i in range(8)]
        self.assertEqual(verify_ranges(rs)['ranges'],8)
        rs[0]['receiver_write_backend']='pwrite'
        with self.assertRaises(AssertionError):verify_ranges(rs)
        rs[0]['receiver_write_backend']='mmap_experimental'
        rs[0]['mapped_write']['peak_mapped_bytes']+=1
        with self.assertRaises(AssertionError):verify_ranges(rs)

    def test_residency_observation_does_not_change_file(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'resident';p.write_bytes(b'x'*8192)
            r=residency(p)
            self.assertEqual(r['pages'],2);self.assertEqual(r['resident_pages'],2)
            self.assertEqual(p.read_bytes(),b'x'*8192)

    def test_seed_swap_abort_retains_diagnostics_before_cleanup(self):
        import json
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-gate-test-',dir='/dev/shm') as d:
            root=Path(d);(root/gate.base.MARKER).write_text(gate.base.TOKEN)
            args=SimpleNamespace(commit='test',payload_root=root,evidence=root/'evidence')
            with patch.object(gate.base,'receipt'),patch.object(gate.base,'node',return_value=2), \
                 patch.object(gate.base,'mem',return_value=32*1024**3), \
                 patch.object(gate.base,'pswpout',side_effect=[10,11,11]), \
                 patch.object(gate.shutil,'disk_usage',return_value=SimpleNamespace(free=16*1024**3)):
                with self.assertRaisesRegex(AssertionError,'seed host swap-out changed'):gate.seed(args)
            r=json.loads((args.evidence/'peer/seed-resource.json').read_text())
            self.assertEqual(r['pswpout_delta'],1)
            self.assertEqual(r['materialized_bytes'],8*1024**2)

    def test_seed_hash_phase_swap_cannot_escape_preparation_guard(self):
        import json
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-gate-test-',dir='/dev/shm') as d:
            root=Path(d);(root/gate.base.MARKER).write_text(gate.base.TOKEN)
            args=SimpleNamespace(commit='test',payload_root=root,evidence=root/'evidence')
            swap=[10]
            def hash_source(source):
                self.assertEqual((source/'f000000.bin').stat().st_size,1024)
                swap[0]=11
                return {'files':1,'bytes':1024,'sha256':'fixture'}
            with patch.object(gate.base,'receipt'),patch.object(gate.base,'node',return_value=2), \
                 patch.object(gate.base,'mem',return_value=32*1024**3), \
                 patch.object(gate.base,'pswpout',side_effect=lambda:swap[0]), \
                 patch.object(gate.base,'tree',side_effect=hash_source), \
                 patch.object(gate,'residency',return_value={'all_resident':True}), \
                 patch.object(gate.os,'urandom',return_value=b'x'),patch.object(gate,'SIZE',1024), \
                 patch.object(gate.shutil,'disk_usage',return_value=SimpleNamespace(free=16*1024**3)):
                with self.assertRaisesRegex(AssertionError,'seed host swap-out changed'):gate.seed(args)
            r=json.loads((args.evidence/'peer/seed-resource.json').read_text())
            self.assertEqual(r['pswpout_delta'],1)
            self.assertEqual(r['phase'],'source_sha')
            self.assertFalse((args.evidence/'peer/source.json').exists())

    def test_pre_sample_residency_abort_is_saved_and_not_masked_by_phase_count(self):
        import json
        with tempfile.TemporaryDirectory() as d:
            args=SimpleNamespace(evidence=Path(d));rows=[]
            resident={'all_resident':False,'pages':100,'resident_pages':90}
            def remote_action(args,action):
                return resident if action=='residency' else {'sha256':'fixture','files':1,'bytes':gate.SIZE}
            def base_action(args,action,**kwargs):
                if action=='stop':return {'resource':{'pswpout_delta':0},'sender_phases':{'count':0,'examples':[]}}
                return {}
            with patch.object(gate,'own_peer',side_effect=remote_action), \
                 patch.object(gate.base,'peer',side_effect=base_action),patch.object(gate,'ready'), \
                 patch.object(gate.base,'client_sample') as client:
                with self.assertRaisesRegex(AssertionError,'source residency gate'):gate.run_case(args,rows)
            client.assert_not_called()
            self.assertEqual(rows,[])
            self.assertEqual(json.loads((args.evidence/'samples/8g-original-r1.source-residency.json').read_text()),resident)
            failure=json.loads((args.evidence/'case-failure.json').read_text())
            self.assertEqual(failure['phase'],'source_residency')
            self.assertEqual(failure['completed_samples'],0)

    def test_fully_resident_source_still_rejects_swap_since_seed(self):
        import json
        with tempfile.TemporaryDirectory() as d:
            args=SimpleNamespace(evidence=Path(d));rows=[]
            resident={'all_resident':True,'pages':100,'resident_pages':100,
                      'host_pswpout_delta_since_seed':1,'mem_available_bytes':32*gate.base.GIB,
                      'owned_probe_swap_bytes':0}
            def remote_action(args,action):
                return resident if action=='residency' else {'sha256':'fixture','files':1,'bytes':gate.SIZE}
            def base_action(args,action,**kwargs):
                if action=='stop':return {'resource':{'pswpout_delta':0},'sender_phases':{'count':0,'examples':[]}}
                return {}
            with patch.object(gate,'own_peer',side_effect=remote_action), \
                 patch.object(gate.base,'peer',side_effect=base_action),patch.object(gate,'ready'), \
                 patch.object(gate.base,'client_sample',side_effect=AssertionError('client incorrectly launched')) as client:
                with self.assertRaisesRegex(AssertionError,'source host swap-out changed'):gate.run_case(args,rows)
            client.assert_not_called()
            saved=json.loads((args.evidence/'samples/8g-original-r1.source-residency.json').read_text())
            self.assertEqual(saved,resident)

    def test_residency_probe_carries_original_seed_swap_baseline(self):
        import json
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-gate-test-',dir='/dev/shm') as d:
            root=Path(d);(root/gate.base.MARKER).write_text(gate.base.TOKEN)
            (root/'source').mkdir();(root/'source/f000000.bin').write_bytes(b'x'*8192)
            evidence=root/'evidence';(evidence/'peer').mkdir(parents=True)
            owner={'process':{'pid':123,'start_ticks':456},'membership':'0::/user.slice/session-test.scope',
                   'leaf':'/user.slice/session-test.scope','leaf_present':True,'ancestors':[]}
            (evidence/'peer/seed-resource.json').write_text(json.dumps({'pswpout_before':10,'owner_final':owner}))
            args=SimpleNamespace(payload_root=root,evidence=evidence)
            probe=getattr(gate,'source_residency',None)
            self.assertTrue(callable(probe),'source probe must carry the original seed baseline')
            with patch.object(gate.base,'pswpout',return_value=11), \
                 patch.object(gate.base,'proc_info',return_value={'swap_bytes':0}), \
                 patch.object(gate,'cgroup_owner',return_value=owner), \
                 patch.object(gate,'cgroup_snapshot',return_value={'leaf':'/user.slice/session-test.scope','leaf_present':True}):
                r=probe(args)
            self.assertTrue(r['all_resident'])
            self.assertEqual(r['host_pswpout'],11)
            self.assertEqual(r['host_pswpout_delta_since_seed'],1)
            self.assertTrue(r['same_preparation_process'])
            self.assertTrue(r['same_preparation_cgroup'])


class SourceOwnershipDiagnosticTest(unittest.TestCase):
    def test_cgroup_snapshot_parses_leaf_and_ancestor_state(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);leaf=root/'user.slice/session-7.scope';leaf.mkdir(parents=True)
            parent=root/'user.slice'
            for path,current,swap,events,stat in (
                (parent,'4096','0','low 0\nhigh 0\n','shmem 2048\nfile 4096\n'),
                (leaf,'2048','1024','low 0\nhigh 2\n','shmem 1024\nfile 2048\n')):
                (path/'memory.current').write_text(current+'\n')
                (path/'memory.swap.current').write_text(swap+'\n')
                (path/'memory.swap.max').write_text('max\n')
                (path/'memory.events').write_text(events)
                (path/'memory.stat').write_text(stat)
                (path/'memory.pressure').write_text('some avg10=0.00 avg60=0.00 total=7\n')
            snapshot_fn=getattr(gate,'cgroup_snapshot',None)
            self.assertTrue(callable(snapshot_fn),'source cgroup snapshots must be supported')
            result=snapshot_fn('0::/user.slice/session-7.scope\n',cgroup_root=root)
            self.assertEqual(result['leaf'],'/user.slice/session-7.scope')
            self.assertTrue(result['leaf_present'])
            groups={item['path']:item['fields'] for item in result['ancestors']}
            self.assertEqual(groups[str(leaf)]['memory.swap.current'],'1024')
            self.assertEqual(groups[str(leaf)]['memory.events']['high'],2)
            self.assertEqual(groups[str(leaf)]['memory.stat']['shmem'],1024)
            self.assertIn('total=7',groups[str(leaf)]['memory.pressure'])
            self.assertEqual(groups[str(parent)]['memory.swap.current'],'0')
            missing=snapshot_fn('0::/user.slice/expired.scope\n',cgroup_root=root)
            self.assertFalse(missing['leaf_present'])
            missing_groups={item['path']:item['fields'] for item in missing['ancestors']}
            self.assertEqual(missing_groups[str(parent)]['memory.swap.current'],'0')

    def test_seed_records_same_owner_across_write_hash_and_mincore(self):
        import json
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-gate-test-',dir='/dev/shm') as d:
            root=Path(d);(root/gate.base.MARKER).write_text(gate.base.TOKEN)
            args=SimpleNamespace(commit='test',payload_root=root,evidence=root/'evidence')
            owner={'process':{'pid':123,'start_ticks':456,'swap_bytes':0},'membership':'0::/user.slice/session-test.scope',
                   'leaf':'/user.slice/session-test.scope','ancestors':[]}
            with patch.object(gate.base,'receipt'),patch.object(gate.base,'node',return_value=2), \
                 patch.object(gate.base,'mem',return_value=32*1024**3), \
                 patch.object(gate.base,'pswpout',return_value=10), \
                 patch.object(gate.base,'proc_info',return_value={'swap_bytes':0,'rss_bytes':1024,'cpu_seconds':0}), \
                 patch.object(gate.base,'tree',return_value={'sha256':'fixture','files':1,'bytes':1024}), \
                 patch.object(gate,'residency',return_value={'bytes':1024,'pages':1,'resident_pages':1,'all_resident':True}), \
                 patch.object(gate,'cgroup_owner',return_value=owner), \
                 patch.object(gate.os,'urandom',return_value=b'x'), \
                 patch.object(gate.shutil,'disk_usage',return_value=SimpleNamespace(free=16*1024**3)), \
                 patch.object(gate,'SIZE',1024):
                gate.seed(args)
            state=json.loads((args.evidence/'peer/seed-resource.json').read_text())
            phases=[item['phase'] for item in state['cgroup_snapshots']]
            self.assertEqual(phases,['before_write','after_write','after_sha','after_mincore'])
            identities={(item['owner']['process']['pid'],item['owner']['process']['start_ticks'],
                         item['owner']['membership']) for item in state['cgroup_snapshots']}
            self.assertEqual(identities,{(123,456,'0::/user.slice/session-test.scope')})

    def test_seed_persists_phase_and_resource_evidence_when_guard_fails(self):
        import json
        with tempfile.TemporaryDirectory(prefix='cpnetflux-s0-gate-test-',dir='/dev/shm') as d:
            root=Path(d);(root/gate.base.MARKER).write_text(gate.base.TOKEN)
            args=SimpleNamespace(commit='test',payload_root=root,evidence=root/'evidence')
            owner={'process':{'pid':123,'start_ticks':456,'swap_bytes':0},'membership':'0::/user.slice/session-test.scope',
                   'leaf':'/user.slice/session-test.scope','leaf_present':True,'ancestors':[]}
            with patch.object(gate.base,'receipt'),patch.object(gate.base,'node',return_value=2), \
                 patch.object(gate.base,'mem',side_effect=[32*1024**3,11*1024**3]), \
                 patch.object(gate.base,'pswpout',return_value=10), \
                 patch.object(gate.base,'proc_info',return_value={'swap_bytes':0,'rss_bytes':1024,'cpu_seconds':0}), \
                 patch.object(gate,'cgroup_owner',return_value=owner), \
                 patch.object(gate.os,'urandom',return_value=b'x'), \
                 patch.object(gate.shutil,'disk_usage',return_value=SimpleNamespace(free=16*1024**3)), \
                 patch.object(gate,'SIZE',1024):
                with self.assertRaisesRegex(AssertionError,'seed memory threshold'):
                    gate.seed(args)
            state=json.loads((args.evidence/'peer/seed-resource.json').read_text())
            self.assertEqual(state['phase'],'source_write')
            self.assertEqual(state['error'],"AssertionError('seed memory threshold')")
            self.assertEqual(state['materialized_bytes'],1)
            self.assertEqual(state['pswpout_delta'],0)
            self.assertEqual([x['phase'] for x in state['cgroup_snapshots']],['before_write'])

    def test_resource_diagnostic_blocks_preexisting_process_swap_without_preparing(self):
        import os
        root=Path('/dev/shm')/f'cpnetflux-s0-preflight-test-{os.getpid()}'
        self.assertFalse(root.exists())
        with tempfile.TemporaryDirectory() as d:
            args=SimpleNamespace(commit='test',payload_root=root,evidence=Path(d))
            owner={'process':{'pid':123,'start_ticks':456,'swap_bytes':4096},'membership':'0::/user.slice/test.scope'}
            with patch.object(gate.base,'receipt'),patch.object(gate.base,'node',return_value=2), \
                 patch.object(gate.base,'mem',return_value=32*1024**3), \
                 patch.object(gate.base,'pswpout',return_value=10), \
                 patch.object(gate,'cgroup_owner',return_value=owner), \
                 patch.object(gate.shutil,'disk_usage',return_value=SimpleNamespace(free=16*1024**3)):
                report=gate.resource_diagnostic(args)
            self.assertEqual(report['source_preparations'],0)
            self.assertIn('diagnostic process already has swap',report['preflight_block_reasons'])
            self.assertFalse(report['payload_server_started'])
            self.assertFalse(report['data_transfer_started'])
            self.assertFalse(root.exists())

    def test_resource_diagnostic_retains_failed_residency_and_cleans_root(self):
        import json, os
        root=Path('/dev/shm')/f'cpnetflux-s0-diagnostic-test-{os.getpid()}'
        self.assertFalse(root.exists())
        with tempfile.TemporaryDirectory() as d:
            evidence=Path(d);args=SimpleNamespace(commit='test',payload_root=root,evidence=evidence)
            failure={'phase':'source_residency','source_residency':
                     {'pages':2048,'resident_pages':1900,'all_resident':False}}
            def failed_seed(args):
                (args.evidence/'peer').mkdir(parents=True,exist_ok=True)
                (args.evidence/'peer/seed-resource.json').write_text(json.dumps(failure))
                raise AssertionError('source residency')
            with patch.object(gate.base,'receipt'),patch.object(gate.base,'node',return_value=2), \
                 patch.object(gate.base,'mem',return_value=32*1024**3), \
                 patch.object(gate.base,'pswpout',return_value=10), \
                 patch.object(gate,'cgroup_owner',return_value={'process':{'swap_bytes':0}}), \
                 patch.object(gate.shutil,'disk_usage',return_value=SimpleNamespace(free=16*1024**3)), \
                 patch.object(gate,'seed',side_effect=failed_seed):
                with self.assertRaisesRegex(AssertionError,'source residency'):
                    gate.resource_diagnostic(args)
            saved=json.loads((evidence/'resource-diagnostic.json').read_text())
            self.assertEqual(saved['seed_resource'],failure)
            self.assertEqual(saved['source_preparations'],1)
            self.assertEqual(saved['outcome'],'preparation_failed')
            self.assertTrue(saved['cleanup_pass'])
            self.assertFalse(root.exists())


if __name__=='__main__':unittest.main()
