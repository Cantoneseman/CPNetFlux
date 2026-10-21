"""Behavioral gates for the V3 desktop backend contract."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch
from jsonschema import Draft7Validator

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('v3_agent', ROOT / 'agent/cpnetflux_agent.py')
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)
SCHEMA = json.loads((ROOT / 'schemas/desktop-agent.schema.json').read_text())

def policy():
    return dict(requested_mode='v3', scheduler='dynamic_ring', channel_count=2,
                pending_window=4, queue_depth='auto', range_policy='auto',
                checksum_policy='required_crc32c', resume_policy='allow',
                control_tls='verify_ca', data_tls='required', fallback_policy='reject',
                conflict_policy='fail', experimental_raw_ack=False,
                reconnect_attempts=2, transfer_timeout_ms=45000, compression='auto',
                chunk_size=2097152, buffer_size=131072, control_pipeline_depth=2,
                scheduler_mode='off', scheduler_policy='fixed')

class V3ApiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        base = Path(self.tmp.name)
        self.agent = MOD.Agent(types.SimpleNamespace(runtime_dir=str(base/'run'),
            state_dir=str(base/'state'), core_bin_dir=None, demo=True), SCHEMA)
    def tearDown(self):
        self.agent.lock.close(); self.agent.state_lock.close(); self.tmp.cleanup()
    def request(self):
        return dict(operation_id='create-v3', direction='upload',
            source=dict(kind='local',path='/tmp/source',node_id=None,root_id=None),
            destination=dict(kind='remote',path='dataset',node_id='demo',root_id='root'),
            policy=policy())
    def test_v3_contract_has_truthful_availability(self):
        result = self.agent.dispatch('agent.getContract', {}, None)
        self.assertEqual(result['runtime_protocol'], 'v3')
        methods = {x['method']: x for x in result['methods']}
        self.assertEqual(methods['transfer.pause']['support'], 'unsupported')
        self.assertIn('transfer.validate', methods)
        self.assertIn('transfer.evidence', methods)
        self.assertEqual(result['policy_defaults']['requested_mode'], 'v3')
    def test_legacy_modes_rejected_by_wire_schema(self):
        v = Draft7Validator(dict(SCHEMA, **{'$ref':'#/$defs/request'}))
        for mode in ['v1','v2','auto']:
            req = self.request(); req['policy']['requested_mode'] = mode
            self.assertFalse(v.is_valid(dict(jsonrpc='2.0', id='1',
                method='transfer.create', params=req)))
        self.assertTrue(v.is_valid(dict(jsonrpc='2.0', id='1',
            method='transfer.create',params=self.request())))
    def test_preview_is_non_mutating_and_resolves_options(self):
        req = self.request(); req.pop('operation_id')
        before = copy.deepcopy(self.agent.store)
        result = self.agent.dispatch('transfer.validate',req,None)
        self.assertEqual(result['resolved_options']['channel_count'],2)
        self.assertEqual(result['resolved_options']['pending_window'],2)
        self.assertTrue(any('4' in warning and '2' in warning for warning in result['warnings']))
        self.assertEqual(result['resolved_options']['queue_capacity'],8)
        self.assertEqual(self.agent.store,before)
    def test_invalid_combinations_are_rejected_before_launch(self):
        req = self.request(); req.pop('operation_id')
        req['policy']['scheduler'] = 'static'
        with self.assertRaises(MOD.RpcError): self.agent.dispatch('transfer.validate',req,None)
        req = self.request(); req.pop('operation_id')
        req['policy']['resume_policy'] = 'never'
        with self.assertRaises(MOD.RpcError): self.agent.dispatch('transfer.validate',req,None)
    def test_network_manual_selection_get_clear_and_stale_clear(self):
        a = self.agent
        inv = a._network_inventory('local')
        p = dict(node_id='local',inventory_revision=inv['inventory_revision'],
                 requested_links=1,peer_addresses=[],selections=[dict(
                     interface_id='demo:eth0',local_address='192.0.2.10')])
        plan = a.dispatch('node.network.plan',p,None)
        a.dispatch('node.network.apply',dict(operation_id='apply',node_id='local',
            plan_id=plan['plan_id'],inventory_revision=inv['inventory_revision'],confirm=True),None)
        current = a.dispatch('node.network.get',{'node_id':'local'},None)
        self.assertEqual(current['binding']['local_bind_addresses'],['192.0.2.10'])
        with self.assertRaises(MOD.RpcError):
            a.dispatch('node.network.clear',dict(operation_id='stale',node_id='local',
                expected_plan_id='obsolete',confirm=True),None)
        result = a.dispatch('node.network.clear',dict(operation_id='clear',node_id='local',
            expected_plan_id=plan['plan_id'],confirm=True),None)
        self.assertIsNone(result['binding'])
        self.assertIsNone(a.dispatch('node.network.get',{'node_id':'local'},None)['binding'])
    def test_peer_mapping_plan_apply_and_frozen_command(self):
        a=self.agent
        inv=a._network_inventory('demo')
        plan=a._network_plan('demo',inv['inventory_revision'],1,['198.51.100.20'])
        a.dispatch('node.network.apply',dict(operation_id='mapped',node_id='demo',
            plan_id=plan['plan_id'],inventory_revision=inv['inventory_revision'],confirm=True),None)
        binding=a.dispatch('node.network.get',dict(node_id='demo'),None)['binding']
        self.assertEqual(binding['bindings'][0]['peer_address'],'198.51.100.20')
        a.args.demo=False;a.core_binary=lambda name:'/bin/'+name
        profile=dict(host='identity.example',control_port=2121,server_name='identity.example',
            ca_path='/ca',credential_ref='file:/token')
        a.profile_for=lambda endpoint,profile=None:profile
        a.store['task_inputs']['t']=dict(profile=profile,binding=copy.deepcopy(binding))
        a.store['network_bindings'].clear()
        task=self.request();task.update(transfer_id='t',attempt=dict(attempt_number=1),
            backend_evidence=dict(event_log='/events',summary='/summary'))
        cmd=a.build_real_command(task)
        self.assertEqual(cmd[cmd.index('--peer-addresses')+1],'198.51.100.20')
        self.assertEqual(cmd[cmd.index('--tls-server-name')+1],'identity.example')

    def test_peer_mapping_rejects_partial_invalid_and_family_mismatch(self):
        inv=self.agent._network_inventory('local')
        for links,peers in [(2,['198.51.100.20']), (2,['198.51.100.20',None]),
                (1,['peer.example']), (1,['0.0.0.0']), (1,['224.0.0.1']),
                (1,['fe80::1']), (1,['::1'])]:
            with self.assertRaises(MOD.RpcError):
                self.agent._network_plan('local',inv['inventory_revision'],links,peers,
                    [dict(interface_id='demo:eth0',local_address='192.0.2.10')]*links)
        with self.assertRaises(MOD.RpcError):
            self.agent.resolve_options(policy(),dict(local_bind_addresses=['192.0.2.10'],
                bindings=[dict(local_address='192.0.2.10',peer_address='198.51.100.1'),
                          dict(local_address='192.0.2.10',peer_address=None)]))

    def test_manual_selection_rejects_unassigned_or_duplicate_interface(self):
        inv = self.agent._network_inventory('local')
        for picks in [[dict(interface_id='demo:eth0',local_address='203.0.113.9')],
                      [dict(interface_id='demo:eth0',local_address='192.0.2.10')]*2]:
            with self.assertRaises(MOD.RpcError):
                self.agent.dispatch('node.network.plan',dict(node_id='local',
                    inventory_revision=inv['inventory_revision'],requested_links=len(picks),
                    peer_addresses=[],selections=picks),None)
    def test_real_command_passes_v3_options_and_not_credentials(self):
        a = self.agent; a.args.demo=False
        a.core_binary = lambda name: '/opt/bin/'+name
        p = dict(node_id='demo', host='peer.example', control_port=2121,
            server_name='peer.example',ca_path='/private/ca.pem',auth_mode='token',
            credential_ref='file:/private/token')
        a.profile_for = lambda endpoint, profile=None: profile or p
        a.store['profiles']['demo'] = p
        t = self.request(); t['transfer_id']='test'; t['attempt']={'attempt_number':1}
        t['backend_evidence']={'event_log':'/tmp/events','summary':'/tmp/summary'}
        cmd = a.build_real_command(t)
        for flag,value in [('--link-set-links','2'),('--file-parallelism','2'),
                           ('--data-pending-window','2'),('--compression','auto'),
                           ('--reconnect-attempts','2'),('--transfer-timeout-ms','45000'),
                           ('--control-pipeline-depth','2'),('--chunk-size','2097152')]:
            self.assertEqual(cmd[cmd.index(flag)+1],value)
        self.assertIn('--scheduler',cmd)
        self.assertNotIn('--scheduler-mode',cmd)
        self.assertEqual(cmd[cmd.index('--scheduler')+1],'off')
        self.assertNotIn('--resume',cmd)
        t['attempt']['attempt_number']=2
        self.assertIn('--resume',a.build_real_command(t))
    def test_summary_readback_uses_accepted_v3_lanes(self):
        a=self.agent; t={'effective_policy':a.pending_policy()}
        a.apply_real_policy(t,dict(result='pass',actual_transport='v3',
            actual_file_scheduling='dynamic',checksum_algorithm='crc32c',resume=False,
            link_set_accepted_links=2,connections=8,control_tls_verified=True,
            data_tls_verified=True))
        self.assertEqual(t['actual_mode'],'v3')
        self.assertEqual(t['effective_policy']['channel_count'],2)
        self.assertEqual(t['effective_policy']['control_tls'],'verified')
    def test_resume_committed_bytes_include_previously_skipped_bytes(self):
        a=self.agent; t={'source':{'path':'/nonexistent/source'},'direction':'upload',
            'metrics':{},'backend_evidence':MOD.empty_evidence()}
        a.read_real_evidence(t,dict(result='pass',file_count=2,completed_files=1,
            skipped_files=1,failed_files=0,bytes_total=100,bytes_transferred=40,
            checksum_algorithm='crc32c'))
        self.assertEqual(t['metrics']['bytes_committed'],100)
        self.assertTrue(t['scan_complete'])

    def test_contract_and_preview_responses_match_schema(self):
        validator=Draft7Validator(SCHEMA)
        for method,params in [('agent.getInfo',{}),('agent.getContract',{}),
                ('transfer.validate',{k:v for k,v in self.request().items() if k!='operation_id'}),
                ('node.network.get',dict(node_id='local'))]:
            validator.validate(dict(jsonrpc='2.0',id='test',result=self.agent.dispatch(method,params,None)))
        self.agent.args.demo=False
        contract=self.agent.contract()
        methods={x['method']:x for x in contract['methods']}
        self.assertEqual(methods['transfer.start']['support'],'unsupported')
        self.assertTrue(methods['transfer.start']['reason'])

    def test_default_and_static_queue_and_scheduler_bounds(self):
        a=self.agent
        result=a.resolve_options(a.policy_defaults())
        self.assertEqual(result['queue_capacity'],4)
        p=a.policy_defaults();p['scheduler']='static'
        self.assertEqual(a.resolve_options(p)['queue_capacity'],0)
        p.update(scheduler_work_item_min_bytes=10,scheduler_work_item_max_bytes=2)
        with self.assertRaises(MOD.RpcError):a.resolve_options(p)

    def test_frozen_task_inputs_and_explicit_raw_policy_reach_core(self):
        a=self.agent;a.args.demo=False
        a.core_binary=lambda name:'/bin/'+name
        profile=dict(host='frozen.example',control_port=2121,server_name='frozen.example',
            ca_path='/ca',credential_ref='file:/token')
        a.profile_for=lambda endpoint,profile=None:profile or a.store['profiles']['demo']
        a.store['profiles']['demo']=dict(profile,host='changed.example')
        a.store['network_bindings']['demo']=dict(local_bind_addresses=['203.0.113.9'])
        a.store['task_inputs']['task']=dict(profile=profile,binding=dict(local_bind_addresses=['192.0.2.10']))
        task=self.request();task.update(transfer_id='task',attempt=dict(attempt_number=1),
            backend_evidence=dict(event_log='/events',summary='/summary'))
        task['policy'].update(data_tls='off',checksum_policy='none',experimental_raw_ack=True,reconnect_attempts=0)
        cmd=a.build_real_command(task)
        self.assertEqual(cmd[cmd.index('--host')+1],'frozen.example')
        self.assertEqual(cmd[cmd.index('--local-bind-addresses')+1],'192.0.2.10')
        self.assertEqual(cmd[cmd.index('--data-tls-mode')+1],'off')
        self.assertEqual(cmd[cmd.index('--checksum')+1],'none')
        for flag in ['--scheduler-workitem-min-bytes','--scheduler-workitem-max-bytes','--scheduler-default-rtt-ms','--scheduler-capacity-gbps']:
            self.assertIn(flag,cmd)

    def test_final_file_and_range_evidence_match_schema(self):
        a=self.agent;a.args.demo=False
        task=dict(transfer_id='t',attempt=dict(attempt_id='a',attempt_number=1))
        a.store['tasks']['t']=task;a.store['attempt_history']['t']=[task['attempt']]
        path=Path(self.tmp.name)/'summary.json'
        path.write_text(json.dumps(dict(result='pass',checksum_algorithm='crc32c',file_timings=[dict(
            file_id=7,generation=3,relative_path='nested/data',size=1024,range_id=9,offset=0,length=1024,attempt=2)])))
        a.store['summaries']['a']=str(path)
        files=a.real_files(task,'a')
        ranges=a.real_ranges(task,dict(attempt_id='a',file_id='7',generation=3))
        for name,rows in [('file',files),('range',ranges)]:
            validator=Draft7Validator(dict(SCHEMA,**{'$ref':'#/$defs/'+name}))
            for row in rows:validator.validate(row)
        self.assertIsNone(files[0]['mtime_ns'])
        self.assertEqual(ranges[0]['checksum']['status'],'pass')
        self.assertEqual(ranges[0]['attempt_number'],1)
        self.assertEqual(ranges[0]['core_attempt'],2)
        with self.assertRaises(MOD.RpcError):a.real_files(task,'another-attempt')

    def test_completion_requires_actual_socket_endpoints_to_match_peer_map(self):
        a=self.agent
        binding=dict(local_bind_addresses=['192.0.2.10'],bindings=[dict(
            lane_id=0,local_address='192.0.2.10',peer_address='198.51.100.20')])
        a.store['task_inputs']['mapped']=dict(profile={},binding=binding)
        t=self.request();t.update(transfer_id='mapped',attempt=dict(attempt_number=1),
            metrics=dict(files_total=1,files_committed=1,files_skipped=0,files_failed=0,
                bytes_total=7,bytes_committed=7),effective_policy=a.pending_policy())
        summary=dict(result='pass',actual_transport='v3',actual_file_scheduling='dynamic',
            checksum_algorithm='crc32c',resume=False,link_set_accepted_links=2,
            control_tls_verified=True,data_tls_verified=True,data_pending_window=2,
            compression_policy='auto',scheduler_mode='off',scheduler_policy='fixed',
            control_pipeline_depth=2,queue_capacity=8,reconnect_attempt_budget=2,
            recovery_attempts_used=1,peer_addresses=['198.51.100.20'],lane_endpoints=[
                dict(channel_index=0,lane_index=0,control_peer_address='198.51.100.20',
                    data_peer_address='198.51.100.20',control_local_address='192.0.2.10',
                    data_local_address='192.0.2.10'),
                dict(channel_index=1,lane_index=1,control_peer_address='198.51.100.20',
                    data_peer_address='198.51.100.20',control_local_address='192.0.2.10',
                    data_local_address='192.0.2.10')])
        a.apply_real_policy(t,summary)
        self.assertTrue(a.completion_matches(t,summary))
        changed=copy.deepcopy(summary)
        changed['lane_endpoints'][1]['data_peer_address']='203.0.113.10'
        self.assertFalse(a.completion_matches(t,changed))
        changed=copy.deepcopy(summary);changed['peer_addresses']=['203.0.113.10']
        self.assertFalse(a.completion_matches(t,changed))

    def test_completion_rejects_unapplied_options_or_excess_recovery(self):
        a=self.agent;t=self.request()
        t.update(transfer_id='task',attempt=dict(attempt_number=1),metrics=dict(
            files_total=2,files_committed=1,files_skipped=1,files_failed=0,
            bytes_total=100,bytes_committed=100))
        summary=dict(result='pass',actual_transport='v3',actual_file_scheduling='dynamic',
            checksum_algorithm='crc32c',resume=False,link_set_accepted_links=2,
            control_tls_verified=True,data_tls_verified=True,data_pending_window=2,
            compression_policy='auto',scheduler_mode='off',scheduler_policy='fixed',
            control_pipeline_depth=2,queue_capacity=8,reconnect_attempt_budget=2,recovery_attempts_used=1)
        a.apply_real_policy(t,summary)
        self.assertTrue(a.completion_matches(t,summary))
        for key,value in [('queue_capacity',12),('recovery_attempts_used',3),
                ('reconnect_attempt_budget',0),('resume',True),('data_pending_window',4)]:
            with self.subTest(field=key):
                changed=dict(summary,**{key:value})
                self.assertFalse(a.completion_matches(t,changed))

    def test_large_internal_summary_is_independent_of_rpc_frame_budget(self):
        path=Path(self.tmp.name)/'large-summary.json'
        path.write_text('{"result":"pass"}'+' '*(4*MOD.MAX_FRAME+1))
        self.assertEqual(self.agent.load_summary(str(path)),dict(result='pass'))

if __name__ == '__main__': unittest.main(verbosity=2)
