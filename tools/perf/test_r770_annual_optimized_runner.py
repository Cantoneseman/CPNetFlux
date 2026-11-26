import socket
import struct
import sys
import unittest
from collections import Counter
from pathlib import Path
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import r770_annual_optimized_node as node
import r770_annual_optimized_runner as runner
import r770_tcp_diag as diag


def argument(argv, name):
    return argv[argv.index(name) + 1]


def without_arguments(argv, names):
    result = []
    skip_value = False
    for item in argv:
        if skip_value:
            skip_value = False
        elif item in names:
            skip_value = True
        else:
            result.append(item)
    return result


def network_snapshot(tcp_retrans):
    counters = (*runner.rx.ERRORS, *runner.rx.MLX, *runner.rx.HCA, "rx_bytes", "tx_bytes")
    return {
        "available_kib": 20 * 1024 * 1024,
        "free_kib": 10 * 1024 * 1024,
        "tmpfs_free": 20 * 2**30,
        "pswpout": 0,
        "swap_free_kib": 1024,
        "psi_full_us": 0,
        "cgroups": {},
        "counters": {name: 0 for name in counters},
        "tcp_retrans": tcp_retrans,
        "host_busy_cpu_s": 0.0,
    }


def flow_row(host, index, state=1, cookie=None, tcp_info_present=True):
    if host == 1:
        local_ip, local_port = "192.168.100.1", 61350 + index
        peer_ip, peer_port = "192.168.100.2", 46000 + index
    else:
        local_ip, local_port = "192.168.100.2", 46000 + index
        peer_ip, peer_port = "192.168.100.1", 61350 + index
    return {
        "source": "snapshot",
        "observed_at_ns": 100,
        "local_ip": local_ip,
        "local_port": local_port,
        "peer_ip": peer_ip,
        "peer_port": peer_port,
        "local_tuple": f"{local_ip}:{local_port}",
        "peer_tuple": f"{peer_ip}:{peer_port}",
        "socket_cookie": cookie or f"{host:04x}{index + 1:012x}",
        "tcp_state": state,
        "tcp_state_name": {1: "ESTABLISHED", 6: "TIME_WAIT", 7: "CLOSE",
                            10: "LISTEN", 12: "NEW_SYN_RECV"}.get(state, "STATE"),
        "tcp_info_present": tcp_info_present,
        "retrans_total": 0 if tcp_info_present else None,
        "bytes_retrans": 0 if tcp_info_present else None,
    }


def watcher_records(host, missing_terminal=None, extra_sockets=()):
    observer_id = f"observer-{host}"
    sockets = [flow_row(host, index) for index in range(4)]
    records = [{
        "kind": "ready", "handshake": "start", "observer": "NETLINK_SOCK_DIAG",
        "host": host, "run_id": node.RUN, "label": "sample", "observer_id": observer_id,
    }, {
        "kind": "sample", "observed_at_ns": 100, "sockets": sockets + list(extra_sockets),
    }]
    for index, row in enumerate(sockets):
        if index == missing_terminal:
            continue
        terminal = dict(row, source="destroy_event", observed_at_ns=200,
                        tcp_state=7, tcp_state_name="CLOSE")
        records.append({"kind": "destroy", "socket": terminal})
    records.append({
        "kind": "stopped", "handshake": "stop", "observer": "NETLINK_SOCK_DIAG",
        "host": host, "run_id": node.RUN, "label": "sample", "observer_id": observer_id,
        "event_loss_count": 0, "drain_grace_seconds": 0.5,
    })
    return records


class AnnualComfortRunnerTests(unittest.TestCase):
    def test_socket_diag_parser_extracts_cookie_and_tcp_info_counters(self):
        message = bytearray(diag.SOCKET_DIAG_HEADER_BYTES)
        struct.pack_into("=BBBB", message, 0, socket.AF_INET, 1, 0, 0)
        struct.pack_into("!HH", message, 4, 61350, 46000)
        message[8:12] = socket.inet_aton("192.168.100.1")
        message[24:28] = socket.inet_aton("192.168.100.2")
        struct.pack_into("=II", message, 44, 0x00001004, 0)
        info = bytearray(280)
        struct.pack_into("=I", info, 100, 7)
        struct.pack_into("=Q", info, 208, 8192)
        attribute = struct.pack("=HH", 4 + len(info), diag.INET_DIAG_INFO) + info

        row = diag.parse_inet_diag_message(message + attribute)

        self.assertEqual(row["socket_cookie"], "0000100400000000")
        self.assertEqual(row["tcp_state_name"], "ESTABLISHED")
        self.assertEqual(row["retrans_total"], 7)
        self.assertEqual(row["bytes_retrans"], 8192)

    def test_diag_filter_excludes_management_and_resident_ports(self):
        data = flow_row(1, 0)
        self.assertTrue(diag.is_experiment_socket(data))
        self.assertTrue(diag.is_data_socket(data))
        for port in (22, 2121):
            management = dict(data, local_port=port)
            self.assertFalse(diag.is_experiment_socket(management))
        management_host = dict(data, peer_ip="192.168.1.109")
        self.assertFalse(diag.is_experiment_socket(management_host))

    def test_socket_gate_ignores_time_wait_and_new_syn_recv_pseudo_sockets(self):
        real = flow_row(1, 0)
        time_wait = dict(real, source="snapshot", socket_cookie="0000100400000000",
                         tcp_state=diag.TCP_TIME_WAIT, tcp_state_name="TIME_WAIT",
                         tcp_info_present=False, retrans_total=None, bytes_retrans=None)
        syn_recv = dict(real, source="snapshot", socket_cookie="0000100500000000",
                        tcp_state=diag.TCP_SYN_RECV, tcp_state_name="SYN_RECV",
                        tcp_info_present=False, retrans_total=None, bytes_retrans=None)
        new_syn_recv = dict(real, source="snapshot", socket_cookie="ffffffffffffffff",
                            tcp_state=diag.TCP_NEW_SYN_RECV, tcp_state_name="NEW_SYN_RECV",
                            tcp_info_present=False,
                            retrans_total=None, bytes_retrans=None)
        records = watcher_records(1, extra_sockets=(time_wait, syn_recv, new_syn_recv))

        gate = runner.socket_gate(records, 1, "sample")

        self.assertEqual(gate["errors"], [])
        self.assertEqual(gate["socket_count"], 4)
        self.assertEqual(len(gate["data_flows"]), 4)

    def test_destroy_event_covers_socket_closed_between_snapshots(self):
        records = watcher_records(1)
        sample = next(item for item in records if item["kind"] == "sample")
        sample["sockets"] = [row for row in sample["sockets"] if row["local_port"] != 61353]

        gate = runner.socket_gate(records, 1, "sample")

        self.assertEqual(gate["errors"], [])
        self.assertEqual(len(gate["data_flows"]), 4)

    def test_socket_gate_rejects_event_loss(self):
        records = watcher_records(1)
        stopped = next(item for item in records if item["kind"] == "stopped")
        stopped["event_loss_count"] = 1

        gate = runner.socket_gate(records, 1, "sample")

        self.assertTrue(any("kernel event loss" in error for error in gate["errors"]))

    def test_socket_gate_rejects_missing_terminal_handshake(self):
        records = [item for item in watcher_records(1) if item["kind"] != "stopped"]

        gate = runner.socket_gate(records, 1, "sample")

        self.assertTrue(any("terminal handshake missing" in error for error in gate["errors"]))

    def test_socket_gate_rejects_no_experiment_sockets(self):
        records = watcher_records(1)
        records = [item for item in records if item["kind"] in ("ready", "stopped")]
        records.append({"kind": "sample", "sockets": []})

        gate = runner.socket_gate(records, 1, "sample")

        self.assertTrue(any("no experiment TCP sockets observed" in error
                            for error in gate["errors"]))

    def test_socket_gate_rejects_unusable_cookie(self):
        records = watcher_records(1)
        sample = next(item for item in records if item["kind"] == "sample")
        sample["sockets"][0]["socket_cookie"] = "ffffffffffffffff"

        gate = runner.socket_gate(records, 1, "sample")

        self.assertTrue(any("no usable socket cookie" in error for error in gate["errors"]))

    def test_socket_gate_requires_real_data_socket_destroy_terminals(self):
        gate = runner.socket_gate(
            watcher_records(1, missing_terminal=3), 1, "sample")

        self.assertTrue(any("data socket destroy terminal(s) missing" in error
                            for error in gate["errors"]))

    def test_socket_gate_rejects_tcp_info_retransmission_delta(self):
        records = watcher_records(1)
        sample = next(item for item in records if item["kind"] == "sample")
        sample["sockets"][0]["retrans_total"] = 1
        sample["sockets"][0]["bytes_retrans"] = 1448

        gate = runner.socket_gate(records, 1, "sample")

        self.assertTrue(any("socket retransmission" in error for error in gate["errors"]))

    def test_socket_gate_rejects_incomplete_four_stream_coverage(self):
        records = watcher_records(1, missing_terminal=3)
        sample = next(item for item in records if item["kind"] == "sample")
        sample["sockets"] = [row for row in sample["sockets"] if row["local_port"] != 61353]

        gate = runner.socket_gate(records, 1, "sample")

        self.assertTrue(any("expected 4 distinct data sockets, observed 3" in error
                            for error in gate["errors"]))

    def test_socket_pair_comparison_requires_matching_reversed_four_tuples(self):
        gates = {
            host: runner.socket_gate(watcher_records(host), host, "sample")
            for host in runner.HOSTS
        }
        self.assertEqual(runner.socket_pair_errors(gates), [])
        gates[2]["data_flows"][0]["peer_port"] += 1
        self.assertTrue(runner.socket_pair_errors(gates))

    def test_loopback_destroy_multicast_has_both_tcp_info_terminals(self):
        result = diag.loopback_destroy_selftest()

        self.assertEqual(result["payload_bytes"], 1)
        self.assertEqual(len(result["destroy_events"]), 2)
        self.assertEqual({row["tcp_state_name"] for row in result["destroy_events"]}, {"CLOSE"})
        self.assertTrue(all(row["tcp_info_present"] for row in result["destroy_events"]))

    def test_run_sizes_keep_legacy_and_add_exact_one_gib(self):
        self.assertEqual(node.RUN_SIZES[node.SECURITY_RUN], 5 * 2**30)
        self.assertEqual(node.RUN_SIZES[node.COMFORT_DIAG_RUN], node.SMOKE)
        self.assertEqual(node.SMOKE, 128 * 2**20)
        self.assertGreater(node.SMOKE, 64 * 2**20)
        for run in node.COMFORT_RUNS:
            self.assertEqual(node.RUN_SIZES[run], 2**30)

    def test_five_gib_replay_has_three_independent_stages(self):
        expected = {
            "security": node.ANNUAL_5G_SECURITY_RUN,
            "forward": node.ANNUAL_5G_FORWARD_RUN,
            "reverse": node.ANNUAL_5G_REVERSE_RUN,
        }
        self.assertEqual(len(set(node.ANNUAL_5G_RUNS)), 3)
        self.assertEqual(node.RUN_SIZES[node.ANNUAL_5G_REJECTED_SECURITY_RUN], 5 * 2**30)
        self.assertEqual(node.RUN_SIZES[node.ANNUAL_5G_REJECTED_SECURITY_RETRY_RUN], 5 * 2**30)
        self.assertEqual(node.RUN_SIZES[node.ANNUAL_5G_REJECTED_SECURITY_FIXED_RUN], 5 * 2**30)
        self.assertEqual(node.RUN_SIZES[node.ANNUAL_5G_REJECTED_FORWARD_RUN], 5 * 2**30)
        self.assertEqual(node.RUN_SIZES[node.ANNUAL_5G_REJECTED_FORWARD_FIXED_RUN], 5 * 2**30)
        self.assertEqual(node.RUN_SIZES[node.ANNUAL_5G_REJECTED_REVERSE_RUN], 5 * 2**30)
        self.assertEqual(node.RUN_SIZES[node.ANNUAL_5G_REJECTED_REVERSE_PARTIAL_RUN], 5 * 2**30)
        for run in node.ANNUAL_5G_RUNS:
            self.assertEqual(node.RUN_SIZES[run], 5 * 2**30)
        for run in node.ANNUAL_5G_RUNS:
            with patch.object(node, "RUN", run), patch.object(node, "IS_DIAG_VALIDATION", False):
                self.assertEqual(runner.stage_run_names(), expected)

    def test_sequence_collects_remaining_samples_after_a_rejection(self):
        expected = {"violations": [], "bytes": 1, "sha256": "source-sha"}
        with patch.object(runner, "idle", return_value={"after": {2: {}}}), \
                patch.object(runner.rx, "require_capacity"), \
                patch.object(runner, "call", side_effect=lambda host, action, *args:
                             expected if action == "seed" else True), \
                patch.object(runner, "record"), \
                patch.object(runner, "sample", side_effect=[False, True, False]) as samples:
            accepted = runner.sequence("unit", 2, ("grid", "mmap", "grid"), 1)

        self.assertFalse(accepted)
        self.assertEqual(samples.call_count, 3)

    def test_security_matrix_completion_counts_rejected_rows(self):
        rows = [
            {"mode": mode, "decision": "accepted" if index == 0 else "rejected",
             "payload_bytes": 5}
            for mode in runner.SECURITY_BLOCKS[0]
            for index in range(3)
        ]

        self.assertTrue(runner.security_matrix_complete(rows, 5))
        self.assertFalse(runner.security_matrix_complete(rows[:-1], 5))

    def test_security_profiles_share_the_comfort_data_path(self):
        profiles = [node.comfort_profile(mode) for mode in ("mmap", "clear", "tls")]
        common_fields = set(profiles[0]) - {"authentication", "control_tls", "data_tls"}
        expected = {key: profiles[0][key] for key in common_fields}
        for profile in profiles[1:]:
            self.assertEqual({key: profile[key] for key in common_fields}, expected)
        self.assertEqual([profile["authentication"] for profile in profiles],
                         ["anonymous", "token", "token"])
        self.assertEqual([profile["control_tls"] for profile in profiles],
                         [False, False, True])
        self.assertEqual([profile["data_tls"] for profile in profiles],
                         [False, False, True])

    def test_token_clear_and_tls_clients_keep_identical_data_arguments(self):
        test_root = Path("/__cpnetflux_unit_test__")
        with patch.object(node, "IS_COMFORT", True), patch.object(
                node, "slot", side_effect=lambda label: test_root / label):
            clear_argv, clear_env = node.cp_client_args("clear", "unit-sample", 2)
            tls_argv, tls_env = node.cp_client_args("tls", "unit-sample", 2)

        tls_names = {"--tls-mode", "--data-tls-mode", "--tls-ca-file", "--tls-server-name"}
        self.assertEqual(without_arguments(clear_argv, tls_names),
                         without_arguments(tls_argv, tls_names))
        self.assertEqual(clear_env, tls_env)
        self.assertEqual(Path(clear_argv[0]).name, "cpnetflux-tree-download-client")
        self.assertEqual(argument(clear_argv, "--auth-mode"), "token")
        self.assertEqual(argument(clear_argv, "--connections"), "4")
        self.assertEqual(argument(clear_argv, "--data-frame-bytes"), "262144")
        self.assertEqual(argument(clear_argv, "--data-transport"), "tcp")
        self.assertEqual(argument(clear_argv, "--phase-timing"), "off")
        self.assertEqual(argument(clear_argv, "--chunk-size"), "1048576")
        self.assertEqual(argument(clear_argv, "--range-checkpoint-batch-chunks"), "16")
        self.assertEqual(clear_env["CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES"], "1048576")
        self.assertEqual(clear_env["CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND"], "1500000000")
        self.assertEqual(clear_env["CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE"], "off")
        self.assertEqual(argument(tls_argv, "--tls-mode"), "required")
        self.assertEqual(argument(tls_argv, "--data-tls-mode"), "required")

    def test_diagnostic_mmap_client_enables_per_range_backend_receipts(self):
        test_root = Path("/__cpnetflux_unit_test__")
        with patch.object(node, "IS_COMFORT", True), patch.object(
                node, "IS_DIAG_VALIDATION", True), patch.object(
                node, "slot", side_effect=lambda label: test_root / label):
            argv, _ = node.cp_client_args("mmap", "unit-sample", 2)

        self.assertEqual(argument(argv, "--phase-timing"), "on")

    def test_token_server_clear_and_tls_keep_identical_data_arguments(self):
        test_root = Path("/__cpnetflux_unit_test__")
        with patch.object(node, "IS_COMFORT", True), patch.object(
                node, "slot", side_effect=lambda label: test_root / label):
            clear_argv, _, _ = node.service_args("clear", "unit-sample")
            tls_argv, _, _ = node.service_args("tls", "unit-sample")

        tls_names = {"--tls-mode", "--data-tls-mode", "--tls-cert-file", "--tls-key-file"}
        self.assertEqual(without_arguments(clear_argv, tls_names),
                         without_arguments(tls_argv, tls_names))
        self.assertEqual(argument(clear_argv, "--auth-mode"), "token")
        self.assertEqual(argument(clear_argv, "--access-policy"), "read-only")
        self.assertEqual(argument(tls_argv, "--tls-mode"), "required")
        self.assertEqual(argument(tls_argv, "--data-tls-mode"), "required")

    def test_security_matrix_and_main_orders_are_balanced(self):
        security_counts = Counter(mode for block in runner.SECURITY_BLOCKS for mode in block)
        self.assertEqual(set(security_counts.values()), {3})
        self.assertEqual(Counter(runner.FORWARD_ORDER), {"grid": 5, "mmap": 5})
        self.assertEqual(Counter(runner.REVERSE_ORDER), {"grid": 5, "mmap": 5})

    def test_sample_host_tcp_retrans_zero_delta_is_diagnostic(self):
        before = {host: network_snapshot(100) for host in runner.HOSTS}
        after = {host: network_snapshot(100) for host in runner.HOSTS}
        watch = {key: value for key, value in network_snapshot(100).items()
                 if key not in ("counters", "tcp_retrans", "host_busy_cpu_s")}

        errors, _, metrics = runner.bounds(
            before, after, {host: [watch] for host in runner.HOSTS})

        self.assertEqual(errors, {1: [], 2: []})
        self.assertEqual(metrics[1]["tcp_retrans_diagnostic_only"], 0)

    def test_sample_host_tcp_retrans_increment_is_diagnostic_only(self):
        before = {host: network_snapshot(100) for host in runner.HOSTS}
        after = {1: network_snapshot(101), 2: network_snapshot(100)}

        errors, _, metrics = runner.bounds(before, after, {})

        self.assertEqual(errors, {1: [], 2: []})
        self.assertEqual(metrics[1]["tcp_retrans_diagnostic_only"], 1)

    def test_idle_ignores_tcp_retrans_increment(self):
        before = {host: network_snapshot(100) for host in runner.HOSTS}
        after = {1: network_snapshot(101), 2: network_snapshot(100)}

        with patch.object(runner, "both", side_effect=[before, after]), patch.object(
                runner.time, "sleep"), patch.object(runner.rx, "require_capacity") as capacity:
            result = runner.idle(0)

        self.assertEqual(result["after"][1]["tcp_retrans"] - result["before"][1]["tcp_retrans"], 1)
        self.assertEqual(capacity.call_count, 2)

    def test_idle_still_rejects_interface_error_increment(self):
        before = {host: network_snapshot(100) for host in runner.HOSTS}
        after = {1: network_snapshot(101), 2: network_snapshot(100)}
        after[1]["counters"]["rx_missed_errors"] += 1

        with patch.object(runner, "both", side_effect=[before, after]), patch.object(
                runner.time, "sleep"), patch.object(runner.rx, "require_capacity") as capacity:
            with self.assertRaisesRegex(RuntimeError, "rx_missed_errors \\+1"):
                runner.idle(0)

        capacity.assert_not_called()

    def test_preflight_creates_missing_output_before_first_record(self):
        commit = "test-commit"
        output = Mock()
        output.exists.return_value = False
        events = []
        output.mkdir.side_effect = lambda **kwargs: events.append(("mkdir", kwargs))

        def both(*args):
            events.append(("both", args))
            if args == ("run-available",):
                return {1: {"available": True}, 2: {"available": True}}
            if args == ("identity",):
                return {1: {"host": 1}, 2: {"host": 2}}
            return {1: {"commit": commit}, 2: {"commit": commit}}

        def idle():
            events.append(("idle",))
            return {"idle": "pass"}

        def record(name, _value):
            events.append(("record", name))

        with patch.object(runner, "OUT", output), patch.object(
                runner, "both", side_effect=both), patch.object(
                runner, "idle", side_effect=idle), patch.object(
                runner, "record", side_effect=record):
            initial = runner.prepare_preflight(commit)

        self.assertEqual(initial, {1: {"host": 1}, 2: {"host": 2}})
        self.assertEqual(events, [
            ("both", ("run-available",)),
            ("both", ("identity",)),
            ("both", ("receipt", commit)),
            ("idle",),
            ("mkdir", {"mode": 0o700, "parents": True}),
            ("record", "matching-builds"),
            ("record", "preflight"),
        ])

    def test_preflight_refuses_existing_output_directory(self):
        output = Mock()
        output.exists.return_value = True

        with patch.object(runner, "OUT", output), patch.object(runner, "both") as both:
            with self.assertRaises(FileExistsError):
                runner.prepare_preflight("test-commit")

        both.assert_not_called()
        output.mkdir.assert_not_called()


if __name__ == "__main__":
    unittest.main()
