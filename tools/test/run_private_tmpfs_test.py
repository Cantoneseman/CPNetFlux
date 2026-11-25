#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))

class PrivateTmpfsTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(importlib.util.find_spec('private_tmpfs'),
                             'default-off bounded private tmpfs lifecycle is missing')
        import private_tmpfs
        import run_small_file_baseline
        self.m=private_tmpfs
        self.base=run_small_file_baseline
    def test_default_off(self):
        with self.assertRaisesRegex(ValueError,'explicit'):
            self.m.require_enabled(False)
    def test_cpu_profile_specs_hold_workload_constant(self):
        control=self.m.profile_spec('cpu-profile-control')
        candidate=self.m.profile_spec('cpu-profile-candidate')
        for spec in (control,candidate):
            self.assertEqual(spec['size_bytes'],512*1024**2)
            self.assertEqual(spec['mount_limit_bytes'],1024*1024**2)
            self.assertEqual(spec['sender_pacing'],'1500000000')
            self.assertTrue(spec['receiver_profile'])
            self.assertTrue(spec['mapped_write_profile'])
        self.assertEqual(control['huge_policy'],'never')
        self.assertEqual(candidate['huge_policy'],'within_size')
        self.assertEqual({k:v for k,v in control.items() if k not in ('name','huge_policy')},
                         {k:v for k,v in candidate.items() if k not in ('name','huge_policy')})
    def test_cpu_profile_rejects_incompatible_pacing_and_setup(self):
        from types import SimpleNamespace
        fixed=dict(mode='v3',sender_pacing='1500000000',kernel_tcp_pacing=False,
                   native_setup=True,profile='cpu-profile-control')
        self.assertEqual(self.m.validate_profile(SimpleNamespace(**fixed))['size_bytes'],512*1024**2)
        for changes in ({'sender_pacing':'off'},{'kernel_tcp_pacing':True},{'native_setup':False}):
            values=dict(fixed,**changes)
            with self.subTest(changes=changes):
                with self.assertRaises(ValueError):self.m.validate_profile(SimpleNamespace(**values))
    def test_profile_environment_enables_mapped_write_observation_only_on_request(self):
        ordinary=self.base.environment(receiver_profile=True)
        profiled=self.base.environment(receiver_profile=True,mapped_write_profile=True)
        self.assertEqual(ordinary['CPNETFLUX_MAPPED_WRITE_PROFILE'],'0')
        self.assertEqual(profiled['CPNETFLUX_MAPPED_WRITE_PROFILE'],'1')
    def test_process_profile_can_capture_faults_and_context_switches(self):
        stat='123 (cpnetflux) S 1 2 3 4 5 6 89 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24'
        status='VmRSS: 64 kB\nVmSwap: 0 kB\nvoluntary_ctxt_switches: 31\nnonvoluntary_ctxt_switches: 4\n'
        def read(path):
            return stat if path.name=='stat' else status
        value=self.base.proc_info(123,read_text=read,detailed=True)
        self.assertEqual(value['minor_faults'],89)
        self.assertEqual(value['major_faults'],11)
        self.assertEqual(value['voluntary_context_switches'],31)
        self.assertEqual(value['involuntary_context_switches'],4)
    def test_no_arbitrary_mounts(self):
        for path in ('/dev/shm','/tmp/cpnetflux-s0-private55-x','/dev/shm/other','/dev/shm/../shm/cpnetflux-s0-private55-x'):
            with self.assertRaises(ValueError):
                self.m.validate_path(Path(path))
    def test_evidence_is_confined_to_experiment_root(self):
        for path in ('/tmp/cpnetflux-evidence','/home/Su/experiments/other',
                     'relative/evidence'):
            with self.assertRaises(ValueError):
                self.m.validate_evidence(Path(path))
        self.assertEqual(self.m.validate_evidence(Path('/home/Su/experiments/CPNetFlux/task55')),
                         Path('/home/Su/experiments/CPNetFlux/task55'))
        with tempfile.TemporaryDirectory() as d:
            link=Path(d)/'link';link.symlink_to('/dev/shm')
            with self.assertRaises(ValueError):
                self.m.validate_path(link)
    def test_page_proof_uses_only_target_vma(self):
        text='1000-2000 rw-s 0000 00:00 0 /target/f\nShmemPmdMapped: 2048 kB\n2000-3000 rw-s 0000 00:00 0 /other\nShmemPmdMapped: 4096 kB\n'
        self.assertEqual(self.m.target_pmd_bytes(text,Path('/target')),2048*1024)
    def test_gate_never_promotes_bad_network(self):
        r=self.m.admission(44,network=False,resources=True,content=True,pages=True,cleanup=True)
        self.assertFalse(r)
        self.assertFalse(self.m.admission(39.9,network=True,resources=True,content=True,pages=True,cleanup=True))
        self.assertTrue(self.m.admission(40,network=True,resources=True,content=True,pages=True,cleanup=True))
    def test_timeout_reaps_process(self):
        with tempfile.TemporaryDirectory() as d:
            out=Path(d)/'child.log'
            result=self.m.run_child([sys.executable,'-c','import time;time.sleep(30)'],out,.05)
            self.assertEqual(result['reason'],'timeout')
            self.assertTrue(result['reaped'])
    def test_cancel_reaps_process(self):
        with tempfile.TemporaryDirectory() as d:
            result=self.m.run_child([sys.executable,'-c','import time;time.sleep(30)'],
                                   Path(d)/'log',3,cancel_after=.05)
            self.assertEqual(result['reason'],'cancelled')
            self.assertTrue(result['reaped'])
    def test_broad_counter_gate_checks_driver_and_netdev_names(self):
        from run_private_tmpfs_v3 import validate_receive_deltas,validate_netdev_deltas
        with self.assertRaises(AssertionError):
            validate_receive_deltas({'rx_out_of_buffer_delta':1,'rx_missed_errors_delta':0})
        with self.assertRaises(AssertionError):
            validate_receive_deltas({'rx_out_of_buffer_delta':0,'rx_missed_errors_delta':1})
        validate_receive_deltas({'rx_out_of_buffer_delta':0,'rx_missed_errors_delta':0})
        with self.assertRaises(AssertionError):validate_netdev_deltas({'rx_missed_errors':1})
        validate_netdev_deltas({'rx_missed_errors':0})
    def test_resource_gate_is_independent_of_network_gate(self):
        from run_private_tmpfs_v3 import resource_gate
        client=dict(mem_available_min_bytes=12*1024**3,owned_swap_peak_bytes=0,
                    owned_swap_current_bytes=0,pswpout_delta=0)
        sender=dict(mem_available_min_bytes=12*1024**3,owned_swap_peak_bytes=0,
                    pswpout_delta=0,error=None)
        source=dict(mem_available_bytes=12*1024**3,process=dict(swap_bytes=0),
                    host_pswpout_delta_since_seed=0)
        self.assertTrue(resource_gate(client,sender,source,12*1024**3,0))
        sender['mem_available_min_bytes']=12*1024**3-1
        self.assertFalse(resource_gate(client,sender,source,12*1024**3,0))
        sender['mem_available_min_bytes']=12*1024**3
        client['pswpout_delta']=1
        self.assertFalse(resource_gate(client,sender,source,12*1024**3,0))
        client['pswpout_delta']=0
    def test_kernel_pacing_log_is_preserved_independently_from_application_pacer(self):
        import json
        import run_small_file_baseline as base
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'server.log'
            row=dict(lane=0,requested_bytes_per_second=2000000000,
                     effective_ceiling_bytes_per_second=2000000000,enabled=True,
                     scope='TCP_socket_ceiling_not_achieved_rate',application_pacer_enabled=False)
            path.write_text('CPNETFLUX_TCP_KERNEL_PACING '+json.dumps(row)+'\n')
            parsed=base.compact_log(path)
            self.assertEqual(parsed.get('tcp_kernel_pacing'),[row])
            self.assertNotIn('sender_pacing',parsed)
    def test_kernel_receipt_gate_rejects_missing_duplicate_inactive_and_wrong_scope(self):
        import copy
        from run_private_tmpfs_v3 import validate_kernel_pacing_receipts
        rows=[dict(lane=i,requested_bytes_per_second=2000000000,
              effective_ceiling_bytes_per_second=2000000000,enabled=True,
              application_pacer_enabled=False,scope='TCP_socket_ceiling_not_achieved_rate') for i in range(4)]
        self.assertEqual(validate_kernel_pacing_receipts(rows)['lanes'],4)
        for values in (rows[:3],rows+rows[:1]):
            with self.assertRaises(AssertionError):validate_kernel_pacing_receipts(values)
        for key,value in [('lane',1),('effective_ceiling_bytes_per_second',1500000000),
                          ('enabled',False),('application_pacer_enabled',True),('scope','achieved_rate')]:
            values=copy.deepcopy(rows);values[0][key]=value
            with self.subTest(key=key):
                with self.assertRaises(AssertionError):validate_kernel_pacing_receipts(values)
    def test_kernel_flag_reaches_private_namespace_launcher(self):
        import os
        from types import SimpleNamespace
        from unittest.mock import patch
        import uuid
        captured=[]
        with tempfile.TemporaryDirectory() as folder:
            args=SimpleNamespace(enable_private_tmpfs=True,
                payload_root=Path('/dev/shm/cpnetflux-s0-private55-unit56-'+uuid.uuid4().hex),
                evidence=Path(folder)/'evidence',mode='v3',commit='test',kernel_tcp_pacing=True)
            def child(command,*unused,**kwargs):
                captured.append(command)
                return dict(exit_code=0,reason='exit',reaped=True)
            with patch.object(self.m,'validate_evidence',side_effect=lambda path:path),                  patch.object(self.m,'run_child',side_effect=child):
                self.assertEqual(self.m.launch(args),0)
            self.assertIn('--kernel-tcp-pacing',captured[0])
            self.assertFalse(args.payload_root.exists())
    def test_native_setup_selects_fixed_helper_without_root_python_reexec(self):
        from types import SimpleNamespace
        from unittest.mock import patch
        import uuid
        captured=[]
        with tempfile.TemporaryDirectory() as folder:
            args=SimpleNamespace(enable_private_tmpfs=True,
                payload_root=Path('/dev/shm/cpnetflux-s0-private55-unit57-'+uuid.uuid4().hex),
                evidence=Path(folder)/'evidence',mode='v3',commit='a'*40,
                kernel_tcp_pacing=True,native_setup=True)
            def child(command,*unused,**kwargs):
                captured.append(command)
                return dict(exit_code=0,reason='exit',reaped=True)
            with patch.object(self.m,'validate_evidence',side_effect=lambda path:path), \
                 patch.object(self.m,'run_child',side_effect=child):
                self.assertEqual(self.m.launch(args),0)
            command=captured[0]
            self.assertEqual(command[:2],['sudo','-n'])
            self.assertEqual(Path(command[2]).name,'cpnetflux-private-tmpfs-launcher')
            self.assertNotIn(sys.executable,command)
            self.assertIn('--kernel-tcp-pacing',command)
            self.assertIn('--enable-private-tmpfs',command)
            self.assertFalse(args.payload_root.exists())
    def test_no_target_page_proof_from_anonymous_mapping(self):
        text='1000-2000 rw-p 0000 00:00 0\nShmemPmdMapped: 2048 kB\n'
        self.assertEqual(self.m.target_pmd_bytes(text,Path('/target')),0)
    def test_summary_rejects_unexpected_transport(self):
        from run_private_tmpfs_v3 import validate_summary
        with self.assertRaises(AssertionError):
            validate_summary(dict(connections=4,actual_transport='v2'))
    def test_descendant_does_not_keep_namespace_alive(self):
        import os
        with tempfile.TemporaryDirectory() as d:
            pidfile=Path(d)/'pid'
            code="import subprocess,pathlib,sys; p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)']); pathlib.Path(sys.argv[1]).write_text(str(p.pid))"
            result=self.m.run_child([sys.executable,'-c',code,str(pidfile)],Path(d)/'log',3)
            self.assertEqual(result['exit_code'],0)
            pid=int(pidfile.read_text())
            stat=Path('/proc')/str(pid)/'stat'
            if stat.exists():self.assertEqual(stat.read_text().rsplit(')',1)[1].split()[0],'Z')
    def test_owned_cancel_request_reaps_child(self):
        with tempfile.TemporaryDirectory() as d:
            request=Path(d)/'cancel';request.touch()
            result=self.m.run_child([sys.executable,'-c','import time;time.sleep(30)'],
                                   Path(d)/'log',3,cancel_path=request)
            self.assertEqual(result['reason'],'cancelled')
            self.assertTrue(result['reaped'])
    def test_failure_is_retained(self):
        with tempfile.TemporaryDirectory() as d:
            result=self.m.run_child([sys.executable,'-c','raise SystemExit(7)'],Path(d)/'log',2)
            self.assertEqual(result['exit_code'],7)
            self.assertTrue(result['reaped'])

if __name__=='__main__':unittest.main()
