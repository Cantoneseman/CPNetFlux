#!/usr/bin/env python3
"""Validate network attribution without inferring packet loss from missing evidence."""
from pathlib import Path
import sys
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'perf'))
import receive_network as net


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


if __name__=='__main__':unittest.main()
