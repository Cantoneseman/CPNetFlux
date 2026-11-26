import sys
import tempfile
import unittest
from collections import Counter
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))

import r770_annual_optimized_node as node
import r770_annual_optimized_runner as runner


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


class AnnualComfortRunnerTests(unittest.TestCase):
    def test_run_sizes_keep_legacy_and_add_exact_one_gib(self):
        self.assertEqual(node.RUN_SIZES[node.SECURITY_RUN], 5 * 2**30)
        for run in node.COMFORT_RUNS:
            self.assertEqual(node.RUN_SIZES[run], 2**30)

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
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(node, "IS_COMFORT", True), patch.object(
                    node, "slot", side_effect=lambda label: Path(temporary) / label):
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
        self.assertEqual(argument(clear_argv, "--chunk-size"), "1048576")
        self.assertEqual(argument(clear_argv, "--range-checkpoint-batch-chunks"), "16")
        self.assertEqual(clear_env["CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES"], "1048576")
        self.assertEqual(clear_env["CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND"], "1500000000")
        self.assertEqual(clear_env["CPNETFLUX_EXPERIMENTAL_DIRECT_MMAP_RECEIVE"], "off")
        self.assertEqual(argument(tls_argv, "--tls-mode"), "required")
        self.assertEqual(argument(tls_argv, "--data-tls-mode"), "required")

    def test_token_server_clear_and_tls_keep_identical_data_arguments(self):
        with tempfile.TemporaryDirectory() as temporary:
            with patch.object(node, "IS_COMFORT", True), patch.object(
                    node, "slot", side_effect=lambda label: Path(temporary) / label):
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


if __name__ == "__main__":
    unittest.main()
