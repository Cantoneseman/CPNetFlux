#!/usr/bin/env python3
"""Validate network attribution without inferring packet loss from missing evidence."""
from pathlib import Path
import sys
import unittest
import tempfile
from types import SimpleNamespace
from unittest.mock import MagicMock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
import receive_network as net
import run_receive_network as driver


class ReceiveNetworkTest(unittest.TestCase):
    def test_thread_exit_during_stat_read_is_expected_but_other_errors_propagate(self):
        root=MagicMock();thread=MagicMock()
        root.__truediv__.return_value=root
        root.glob.return_value=[thread]
        stat=thread.__truediv__.return_value
        for error in (FileNotFoundError(),ProcessLookupError()):
            stat.read_text.side_effect=error
            with patch.object(net,'Path',return_value=root):
                self.assertEqual(net.thread_snapshot(123),{})
        stat.read_text.side_effect=PermissionError('permission')
        with patch.object(net,'Path',return_value=root):
            with self.assertRaises(PermissionError):net.thread_snapshot(123)

    def test_ethtool_parser_and_regression_are_explicit(self):
        values=net.parse_ethtool('NIC statistics:\n     rx3_packets: 123\n     rx_out_of_buffer: 7\n')
        self.assertEqual(values,{'rx3_packets':123,'rx_out_of_buffer':7})
        with self.assertRaisesRegex(ValueError,'regress'):net.delta({'x':10},{'x':9})
        with self.assertRaisesRegex(ValueError,'missing'):net.delta({'x':10},{})

    def test_irq_maps_real_sparse_cpu_columns(self):
        text=(' CPU0 CPU2 CPU19\n'
              '303: 0 7 9 IR-PCI-MSIX-0000:0b:00.0 2-edge mlx5_comp1@pci:0000:0b:00.0\n'
              '309: 5 0 0 IR-PCI-MSIX-other mlx5_comp4@pci:0000:0c:00.0\n')
        self.assertEqual(net.parse_interrupts(text,'0000:0b:00.0'),
                         {'1':{'irq':303,'cpus':{'0':0,'2':7,'19':9}}})

    def test_softnet_uses_explicit_cpu_id_when_present(self):
        rows='00000010 00000002 00000003 0 0 0 0 0 0 0 0 0 00000013 0 0\n'
        result=net.parse_softnet(rows)
        self.assertEqual(result['19'],{'processed':16,'dropped':2,'time_squeeze':3})

    def test_netstat_pairs_matching_protocol_headers(self):
        result=net.parse_netstat('Tcp: InSegs RetransSegs\nTcp: 10 3\n'
                                'TcpExt: TCPBacklogDrop TCPRcvQDrop\nTcpExt: 4 5\n')
        self.assertEqual(result['Tcp.RetransSegs'],3)
        self.assertEqual(result['TcpExt.TCPRcvQDrop'],5)
        with self.assertRaisesRegex(ValueError,'columns'):
            net.parse_netstat('Tcp: A B\nTcp: 1\n')

    def test_scope_analysis_does_not_assign_global_drops_to_queue_or_flow(self):
        def snap(oob,miss,packets=10,alloc=0,drop=0):
            return dict(ethtool={'rx_out_of_buffer':oob,'rx2_packets':packets,
                                'rx2_buff_alloc_err':alloc,'rx_crc_errors_phy':0},
                        netdev={'rx_missed_errors':miss},
                        softnet={'2':dict(processed=10,dropped=drop,time_squeeze=0)},
                        tcp={'TcpExt.TCPRcvQDrop':0})
        result=net.analyze(snap(1,1),snap(8,8,100))
        self.assertEqual(result['rx_out_of_buffer_delta'],7)
        self.assertEqual(result['active_rx_queues'],{'2':90})
        self.assertEqual(result['layer'],'hardware_receive_queue_no_buffer')
        self.assertFalse(result['flow_cause_proven'])
        self.assertFalse(result['allocation_failure_proven'])
        no_error=net.analyze(snap(1,1),snap(1,1,100))
        self.assertEqual(no_error['layer'],'not_reproduced')
        with self.assertRaisesRegex(ValueError,'missing'):
            net.analyze(dict(ethtool={},netdev={},softnet={},tcp={}),snap(1,1))

    def test_queue_page_pool_deltas_remain_separate_from_global_oob(self):
        def snap(oob, q13, q16):
            ethtool={'rx_out_of_buffer':oob}
            for queue,values in (('13',q13),('16',q16)):
                ethtool.update({f'rx{queue}_{key}':value for key,value in values.items()})
            return dict(ethtool=ethtool,netdev={'rx_missed_errors':0},
                        softnet={'0':dict(processed=0,dropped=0,time_squeeze=0)},tcp={})

        before=snap(100,dict(packets=20,pp_alloc_fast=100,pp_alloc_slow=2,
                             pp_alloc_empty=2,pp_alloc_refill=10,buff_alloc_err=0,wqe_err=0),
                    dict(packets=30,pp_alloc_fast=200,pp_alloc_slow=5,
                         pp_alloc_empty=5,pp_alloc_refill=20,buff_alloc_err=0,wqe_err=0))
        after=snap(337,dict(packets=120,pp_alloc_fast=300,pp_alloc_slow=10,
                            pp_alloc_empty=10,pp_alloc_refill=398,buff_alloc_err=0,wqe_err=0),
                   dict(packets=130,pp_alloc_fast=400,pp_alloc_slow=15,
                        pp_alloc_empty=15,pp_alloc_refill=433,buff_alloc_err=0,wqe_err=0))

        result=net.analyze(before,after)
        self.assertEqual(result['rx_out_of_buffer_delta'],237)
        self.assertEqual(result['rx_queue_counter_deltas']['13'],
                         dict(packets=100,pp_alloc_fast=200,pp_alloc_slow=8,
                              pp_alloc_empty=8,pp_alloc_refill=388,buff_alloc_err=0,wqe_err=0))
        self.assertEqual(result['rx_queue_counter_deltas']['16'],
                         dict(packets=100,pp_alloc_fast=200,pp_alloc_slow=10,
                              pp_alloc_empty=10,pp_alloc_refill=413,buff_alloc_err=0,wqe_err=0))
        self.assertEqual(result['rx_out_of_buffer_queue_attribution'],
                         'unavailable_interface_aggregate_only')
        self.assertFalse(result['flow_cause_proven'])
        self.assertFalse(result['allocation_failure_proven'])

    def exercise_driver(self,failed_lane,delayed=False):
        with tempfile.TemporaryDirectory(prefix="cpnetflux-s0-lane-test-",dir="/dev/shm") as directory, \
             tempfile.TemporaryDirectory(prefix="cpnetflux-lane-evidence-") as evidence:
            root=Path(directory)
            root.rmdir()
            args=SimpleNamespace(payload_root=root,evidence=Path(evidence)/"evidence",
                                 commit="test")
            args.evidence.mkdir()
            expected={"sha256":"same","files":1,"bytes":driver.base.GIB}
            residency={"all_resident":True,"host_pswpout_delta_since_seed":0,
                       "mem_available_bytes":16*driver.base.GIB,"process":{"swap_bytes":0}}
            failure={"rx_out_of_buffer_delta":1,"rx_missed_errors_delta":1,"active_rx_queues":{}}
            row={"whole_task_gbps":1,"manifest_checks":{"member_example":"unused"},"summary":{"actual_transport":"v3","data_transport":"tcp",
                 "connections":1,"data_frame_logical_bytes":262144,
                 "lane_endpoints":[{"data_connections":1,"data_local_address":"192.168.100.1",
                                    "data_peer_address":"192.168.100.2","tcp_receive_buffer_bytes":2097152}]},
                 "receiver_phases":{},"receiver_network_window":{
                    "analysis":failure,"settled_analysis":failure},
                 "after":{"net":{"rx_missed_errors":1}},"before":{"net":{"rx_missed_errors":0}}}
            def peer(_args,action,**kwargs):
                if action=="source-check":return expected
                if action=="stop":return {"resource":{"pswpout_delta":0},"before":{"net":{"rx_missed_errors":0}},"after":{"net":{"rx_missed_errors":0}}}
                return {}
            def own_peer(_args,action):
                self.assertEqual(_args.size_gib,1)
                return expected if action=="seed" else residency if action=="residency" else {}
            quiet={"rx_out_of_buffer_delta":0,"rx_missed_errors_delta":0,"active_rx_queues":{}}

            def sample(*_args,**options):
                import copy
                result=copy.deepcopy(row)
                lanes=options['connections']
                result['summary']['connections']=lanes
                result['summary']['lane_endpoints']*=lanes
                if delayed:result['receiver_network_window']['analysis']=quiet
                if failed_lane!=lanes:
                    result['receiver_network_window']={"analysis":quiet,"settled_analysis":quiet}
                    result['after']['net']['rx_missed_errors']=0
                self.assertFalse(options['mapped_download'])
                self.assertFalse(options['receiver_profile'])
                self.assertFalse(options['range_timing'])
                self.assertEqual(options['network_settle_seconds'],.5)
                return result
            with patch.object(driver.base,"node",return_value=1), \
                 patch.object(driver.base,"receipt",return_value={}), \
                 patch.object(driver.base,"peer",side_effect=peer), \
                 patch.object(driver.large,"own_peer",side_effect=own_peer), \
                 patch.object(driver,"ready"),patch.object(driver.time,"sleep"), \
                 patch.object(driver.net,"inventory",return_value={}), \
                 patch.object(driver.net,"snapshot",return_value={}), \
                 patch.object(driver.net,"analyze",return_value=quiet), \
                 patch.object(driver,"verify_chunks",return_value=1024), \
                 patch.object(driver.base,"client_sample",side_effect=sample) as client:
                if failed_lane:
                    with self.assertRaisesRegex(AssertionError,"receive counter"):driver.run(args)
                else:driver.run(args)
            self.assertEqual(client.call_count,1 if failed_lane==1 else 2)
            self.assertEqual([call.kwargs['connections'] for call in client.call_args_list],
                             [1] if failed_lane==1 else [1,4])
            self.assertFalse(root.exists())
            self.assertTrue((args.evidence/"results.json").exists())
            self.assertEqual((args.evidence/"failure.json").exists(),bool(failed_lane))

    def test_first_oob_stops_before_next_lane_and_still_cleans_up(self):
        self.exercise_driver(1)

    def test_delayed_oob_stops_before_next_lane(self):
        self.exercise_driver(1,delayed=True)

    def test_four_lane_event_is_preserved_and_stops(self):
        self.exercise_driver(4)

    def test_two_clean_lanes_complete_without_profiler_or_mapped_write(self):
        self.exercise_driver(None)

if __name__=='__main__':unittest.main()
