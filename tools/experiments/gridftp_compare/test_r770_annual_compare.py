from __future__ import annotations

import tempfile
import unittest
import zlib
from pathlib import Path

from tools.experiments.gridftp_compare import dataset, preflight, runner


class AnnualComparePlanTest(unittest.TestCase):
    def test_candidate_sizes_are_decimal_gigabytes_from_ten_to_one(self) -> None:
        make_sizes = getattr(runner, "annual_candidate_sizes", None)
        self.assertTrue(callable(make_sizes), "annual_candidate_sizes is not implemented")
        self.assertEqual(
            make_sizes(),
            [10_000_000_000, 9_000_000_000, 8_000_000_000, 7_000_000_000,
             6_000_000_000, 5_000_000_000, 4_000_000_000, 3_000_000_000,
             2_000_000_000, 1_000_000_000],
        )

        with self.assertRaises(ValueError):
            make_sizes(maximum_bytes=10_000_000_000, minimum_bytes=1_500_000_000)

    def test_next_size_only_follows_capacity_rejection(self) -> None:
        next_size = getattr(runner, "annual_next_candidate", None)
        self.assertTrue(callable(next_size), "annual_next_candidate is not implemented")
        self.assertEqual(next_size(10_000_000_000, "reduce_size"), 9_000_000_000)
        self.assertIsNone(next_size(1_000_000_000, "reduce_size"))
        self.assertIsNone(next_size(10_000_000_000, "stop"))

    def test_formal_order_balances_which_software_runs_first(self) -> None:
        make_schedule = getattr(runner, "annual_formal_schedule", None)
        self.assertTrue(callable(make_schedule), "annual_formal_schedule is not implemented")
        rows = make_schedule(pairs_per_direction=5)
        self.assertEqual(len(rows), 10)
        self.assertEqual(sum(row["order"][0] == "cpnetflux" for row in rows), 5)
        self.assertEqual(sum(row["order"][0] == "gridftp" for row in rows), 5)
        for direction in ("upload", "download"):
            direction_rows = [row for row in rows if row["direction"] == direction]
            self.assertEqual(len(direction_rows), 5)
            self.assertEqual(
                sum(row["order"][0] == "cpnetflux" for row in direction_rows),
                3 if direction == "upload" else 2,
            )
            self.assertTrue(all(set(row["order"]) == {"cpnetflux", "gridftp"} for row in direction_rows))

    def test_gridftp_single_file_command_uses_gsi_control_and_clear_data(self) -> None:
        build = getattr(preflight, "build_annual_gridftp_single_command", None)
        self.assertTrue(callable(build), "clear-data GridFTP command builder is not implemented")
        command = build(
            globus_url_copy="globus-url-copy",
            source_url="gsiftp://192.168.100.1:2811/source.bin",
            dest_url="gsiftp://192.168.100.2:2811/dest.bin",
            streams=8,
            tcp_buffer_size=1_048_576,
            block_size=262_144,
        )
        self.assertIn("-fast", command)
        self.assertIn("-nodcau", command)
        self.assertNotIn("-dcpriv", command)
        self.assertEqual(command[command.index("-p") + 1], "8")
        self.assertEqual(command[command.index("-tcp-bs") + 1], "1048576")
        self.assertEqual(command[command.index("-bs") + 1], "262144")
        self.assertNotIn("-cc", command)
        with self.assertRaises(ValueError):
            build(
                globus_url_copy="globus-url-copy",
                source_url="file:///tmp/source.bin",
                dest_url="gsiftp://192.168.100.2:2811/dest.bin",
                streams=4,
                tcp_buffer_size=1_048_576,
                block_size=262_144,
            )

    def test_resource_failure_descends_but_network_or_integrity_failure_stops(self) -> None:
        decide = getattr(runner, "annual_admission_decision", None)
        self.assertTrue(callable(decide), "annual_admission_decision is not implemented")
        common = {
            "authentication_ok": True,
            "terminal_ok": True,
            "integrity_ok": True,
            "min_memavailable_bytes": 20 * 1024**3,
            "pswpout_delta": 0,
            "cgroup_event_delta": {
                "low": 0,
                "high": 0,
                "max": 0,
                "oom": 0,
                "oom_kill": 0,
                "oom_group_kill": 0,
                "sock_throttled": 0,
            },
            "min_tmpfs_free_bytes": 20_000_000_000,
            "required_tmpfs_free_bytes": 10_000_000_000,
            "network_deltas": {
                host: {
                    "rx_missed_errors": 0,
                    "rx_out_of_buffer": 0,
                    "rx_errors": 0,
                    "tx_errors": 0,
                    "rx_dropped": 0,
                    "tx_dropped": 0,
                    "rx_crc_errors_phy": 0,
                    "rx_discards_phy": 0,
                    "tx_discards_phy": 0,
                }
                for host in ("r1", "r2")
            },
        }
        self.assertEqual(decide(**common), "accept")
        self.assertEqual(decide(**{**common, "min_memavailable_bytes": 11 * 1024**3}), "reduce_size")
        self.assertEqual(decide(**{**common, "pswpout_delta": 1}), "reduce_size")
        self.assertEqual(
            decide(**{**common, "min_tmpfs_free_bytes": 9_000_000_000}),
            "reduce_size",
        )
        self.assertEqual(
            decide(
                **{
                    **common,
                    "cgroup_event_delta": {
                        **common["cgroup_event_delta"],
                        "high": 1,
                    },
                }
            ),
            "reduce_size",
        )
        self.assertEqual(
            decide(
                **{
                    **common,
                    "network_deltas": {
                        **common["network_deltas"],
                        "r1": {**common["network_deltas"]["r1"], "rx_missed_errors": 1},
                    },
                }
            ),
            "stop",
        )
        self.assertEqual(decide(**{**common, "integrity_ok": False}), "stop")
        self.assertEqual(decide(**{**common, "authentication_ok": False}), "stop")
        self.assertEqual(decide(**{**common, "network_deltas": {}}), "stop")
        self.assertEqual(
            decide(
                **{
                    **common,
                    "network_deltas": {
                        "r1": common["network_deltas"]["r1"],
                    },
                }
            ),
            "stop",
        )

    def test_random_payload_writer_is_streamed_deterministic_and_exact_sized(self) -> None:
        write_payload = getattr(dataset, "write_deterministic_uncompressible_file", None)
        self.assertTrue(callable(write_payload), "streamed payload writer is not implemented")
        with tempfile.TemporaryDirectory() as text:
            root = Path(text)
            first = root / "first.bin"
            second = root / "second.bin"
            write_payload(first, size_bytes=1_048_579, seed=20261123, buffer_bytes=65_537)
            write_payload(second, size_bytes=1_048_579, seed=20261123, buffer_bytes=65_537)
            self.assertEqual(first.stat().st_size, 1_048_579)
            first_bytes = first.read_bytes()
            self.assertEqual(first_bytes, second.read_bytes())
            self.assertGreater(len(zlib.compress(first_bytes)) / len(first_bytes), 0.99)
            with self.assertRaises(FileExistsError):
                write_payload(first, size_bytes=1, seed=1, buffer_bytes=1)
            self.assertEqual(first.read_bytes(), first_bytes)


if __name__ == "__main__":
    unittest.main()
