#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import os
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))

from tools.experiments.gridftp_compare.analyze import (  # noqa: E402
    classify_evidence,
    classify_transfer_result,
    summarize_rows,
)
from tools.experiments.gridftp_compare.dataset import (  # noqa: E402
    DATASET_PROFILES,
    dataset_specs,
    make_dataset,
    materialize_dataset,
    tree_hash,
)
from tools.experiments.gridftp_compare.preflight import (  # noqa: E402
    build_globus_partial_get_command,
    build_globus_transfer_command,
    file_url,
    gridftp_url,
)
from tools.experiments.gridftp_compare.runner import (  # noqa: E402
    build_parser,
    build_cases,
    build_tree_client_command,
    command_audit_path,
    collect_environment,
    configure_ssh_password_file,
    make_case,
    prepare_gridftp_destination_command,
    prepare_gridftp_source_permissions_command,
    record_dry_run_command,
    result_row,
    SocketSampler,
)
from tools.experiments.gridftp_compare.schemas import (  # noqa: E402
    RESULT_FIELDS,
    STATUS_BLOCKED_EXTERNAL_GRIDFTP,
    STATUS_DRY_RUN,
    STATUS_FAIL_CORRECTNESS,
    STATUS_FAIL_RUNTIME,
    STATUS_INCONCLUSIVE_UNSTABLE,
    STATUS_PASS,
)


def fake_args(temp: Path) -> argparse.Namespace:
    return argparse.Namespace(
        chunk_size=1048576,
        buffer_size=65536,
        control_host="47.116.174.181",
        control_port=2811,
        data_port_base=32000,
        environment_json=temp / "environment.json",
        socket_sample_jsonl=temp / "socket_samples.jsonl",
    )


class DatasetTest(unittest.TestCase):
    def test_dataset_manifest_records_required_fields(self) -> None:
        old = DATASET_PROFILES.get("tiny_test")
        DATASET_PROFILES["tiny_test"] = {"kind": "single", "sizes": [8192]}
        try:
            with tempfile.TemporaryDirectory(prefix="gridftp-compare-dataset.") as text:
                root = Path(text) / "dataset"
                manifest = make_dataset(root, profile="tiny_test", seed=7)

                self.assertEqual(manifest["schema_version"], 1)
                self.assertEqual(manifest["profile"], "tiny_test")
                self.assertEqual(manifest["file_count"], 1)
                self.assertEqual(manifest["total_bytes"], 8192)
                self.assertTrue((root / "dataset_manifest.json").is_file())
                self.assertTrue((root / "manifest.json").is_file())
        finally:
            if old is None:
                DATASET_PROFILES.pop("tiny_test", None)
            else:
                DATASET_PROFILES["tiny_test"] = old

    def test_materialized_tree_hash_is_deterministic(self) -> None:
        old = DATASET_PROFILES.get("tiny_tree")
        DATASET_PROFILES["tiny_tree"] = {"kind": "tree", "sizes": [1024, 2048]}
        try:
            with tempfile.TemporaryDirectory(prefix="gridftp-compare-tree.") as text:
                left = Path(text) / "left"
                right = Path(text) / "right"
                materialize_dataset(left, profile="tiny_tree", seed=9)
                materialize_dataset(right, profile="tiny_tree", seed=9)

                self.assertEqual(tree_hash(left), tree_hash(right))
                self.assertEqual(len(dataset_specs("tiny_tree")), 2)
        finally:
            if old is None:
                DATASET_PROFILES.pop("tiny_tree", None)
            else:
                DATASET_PROFILES["tiny_tree"] = old


class MatrixTest(unittest.TestCase):
    def test_core_matrix_contains_raw_cpnetflux_and_gridftp_rows(self) -> None:
        cases = build_cases(
            stage="core",
            systems=["cpnetflux", "gridftp"],
            directions=["local_to_remote"],
            repeat=1,
            scheduler_repeat=1,
            io_repeat=1,
            control_reuse="off",
        )
        keys = {(case.system, case.dataset, case.file_parallelism, case.per_file_connections) for case in cases}

        self.assertIn(("cpnetflux", "single_256MiB", 1, 4), keys)
        self.assertIn(("gridftp", "tree_dense_128MiB", 8, 1), keys)
        self.assertTrue(all(case.scheduler == "off" for case in cases))
        self.assertTrue(all(case.compression == "off" for case in cases))
        self.assertTrue(all(case.checksum == "none" for case in cases))

    def test_async_control_matrix_is_balanced_for_depth_modes(self) -> None:
        baseline = build_cases(
            stage="async-control",
            systems=["cpnetflux", "gridftp"],
            directions=["local_to_remote", "remote_to_local"],
            repeat=3,
            scheduler_repeat=1,
            io_repeat=1,
            control_reuse="worker",
            control_pipeline_depth=0,
        )
        optimized = build_cases(
            stage="async-control",
            systems=["cpnetflux"],
            directions=["local_to_remote", "remote_to_local"],
            repeat=3,
            scheduler_repeat=1,
            io_repeat=1,
            control_reuse="worker",
            control_pipeline_depth=1,
        )
        self.assertEqual(len(baseline), 72)
        self.assertEqual(len(optimized), 36)
        self.assertEqual({case.control_pipeline_depth for case in baseline}, {0})
        self.assertEqual({case.control_pipeline_depth for case in optimized}, {1})
        self.assertEqual({case.dataset for case in optimized}, {"tree_dense_128MiB", "tree_mixed_256MiB"})
        self.assertTrue(all(case.checksum == "none" and case.compression == "off" for case in baseline + optimized))
        self.assertTrue(all(case.control_reuse == "worker" and case.scheduler == "off" for case in baseline + optimized))

    def test_tree_client_command_carries_pipeline_depth(self) -> None:
        case = make_case(
            stage="async-control", system="cpnetflux", direction="local_to_remote",
            dataset="tree_dense_128MiB", file_parallelism=4, connections=1,
            repeat_index=0, control_reuse="worker", control_pipeline_depth=1,
        )
        args = argparse.Namespace(
            local_build_dir="/build", control_host="peer", active_control_port=21210,
            chunk_size=1048576, buffer_size=65536, checksum_backend="auto", auth_mode="anonymous",
        )
        command = build_tree_client_command(
            args=args, case=case, case_dir=Path("/tmp/case"),
            source_dir="/tmp/source", dest_dir="/tmp/dest",
        )
        flag = command.index("--control-pipeline-depth")
        self.assertEqual(command[flag + 1], "1")

    def test_scheduler_matrix_is_cpnetflux_only(self) -> None:
        cases = build_cases(
            stage="scheduler",
            systems=["cpnetflux", "gridftp"],
            directions=["remote_to_local"],
            repeat=1,
            scheduler_repeat=2,
            io_repeat=1,
            control_reuse="worker",
        )

        self.assertEqual(len(cases), 6)
        self.assertEqual({case.system for case in cases}, {"cpnetflux"})
        self.assertIn(("global", "adaptive"), {(case.scheduler, case.scheduler_policy) for case in cases})
        self.assertEqual({case.control_reuse for case in cases}, {"worker"})

    def test_max_cases_limits_matrix(self) -> None:
        cases = build_cases(
            stage="all",
            systems=["cpnetflux", "gridftp"],
            directions=["local_to_remote", "remote_to_local"],
            repeat=3,
            scheduler_repeat=5,
            io_repeat=3,
            control_reuse="off",
            max_cases=3,
        )

        self.assertEqual(len(cases), 3)


class CommandBuilderTest(unittest.TestCase):
    def test_globus_single_command_uses_parallelism_without_recursive_flags(self) -> None:
        command = build_globus_transfer_command(
            globus_url_copy="globus-url-copy",
            source_url="file:///tmp/source.bin",
            dest_url="ftp://anonymous@host:2811/dest.bin",
            parallelism=4,
        )

        self.assertEqual(command[0], "globus-url-copy")
        for flag in ["-fast", "-nodcau", "-cd", "-rp", "-p", "4"]:
            self.assertIn(flag, command)
        self.assertNotIn("-r", command)
        self.assertNotIn("-cc", command)

    def test_globus_tree_command_uses_concurrency_parallelism_and_restart(self) -> None:
        command = build_globus_transfer_command(
            globus_url_copy="globus-url-copy",
            source_url="file:///tmp/source/",
            dest_url="ftp://anonymous@host:2811/dest/",
            parallelism=2,
            concurrency=8,
            recursive=True,
            restart=True,
        )

        self.assertIn("-r", command)
        self.assertIn("-cc", command)
        self.assertIn("8", command)
        self.assertIn("-rst", command)

    def test_gsi_command_uses_private_data_channel(self) -> None:
        command = build_globus_transfer_command(
            globus_url_copy="globus-url-copy",
            source_url="file:///tmp/source.bin",
            dest_url="gsiftp://host:2811/source.bin",
            parallelism=1,
            auth_mode="gsi",
        )
        self.assertIn("-dcpriv", command)
        self.assertNotIn("-nodcau", command)

    def test_globus_source_passive_stream_command_omits_extended_block_flags(self) -> None:
        command = build_globus_transfer_command(
            globus_url_copy="globus-url-copy",
            source_url="gsiftp://host:2811/source.bin",
            dest_url="file:///tmp/dest.bin",
            parallelism=None,
            fast=False,
            auth_mode="gsi",
        )

        self.assertEqual(command, [
            "globus-url-copy",
            "-dcpriv",
            "-cd",
            "-rp",
            "gsiftp://host:2811/source.bin",
            "file:///tmp/dest.bin",
        ])
        self.assertNotIn("-p", command)
        self.assertNotIn("-fast", command)

    def test_globus_partial_get_command_contains_explicit_byte_range(self) -> None:
        command = build_globus_partial_get_command(
            globus_url_copy="globus-url-copy",
            source_url="gsiftp://host:2811/source.bin",
            dest_url="file:///tmp/dest.bin",
            offset=1048576,
            length=4096,
            auth_mode="gsi",
        )

        self.assertEqual(command, [
            "globus-url-copy",
            "-dcpriv",
            "-cd",
            "-rp",
            "-off",
            "1048576",
            "-len",
            "4096",
            "gsiftp://host:2811/source.bin",
            "file:///tmp/dest.bin",
        ])

    def test_globus_source_passive_stream_command_omits_extended_block_flags(self) -> None:
        command = build_globus_transfer_command(
            globus_url_copy="globus-url-copy",
            source_url="gsiftp://host:2811/source.bin",
            dest_url="file:///tmp/dest.bin",
            parallelism=None,
            fast=False,
            auth_mode="gsi",
        )

        self.assertEqual(command, [
            "globus-url-copy",
            "-dcpriv",
            "-cd",
            "-rp",
            "gsiftp://host:2811/source.bin",
            "file:///tmp/dest.bin",
        ])
        self.assertNotIn("-p", command)
        self.assertNotIn("-fast", command)

    def test_globus_partial_get_command_contains_explicit_byte_range(self) -> None:
        command = build_globus_partial_get_command(
            globus_url_copy="globus-url-copy",
            source_url="gsiftp://host:2811/source.bin",
            dest_url="file:///tmp/dest.bin",
            offset=1048576,
            length=4096,
            auth_mode="gsi",
        )

        self.assertEqual(command, [
            "globus-url-copy",
            "-dcpriv",
            "-cd",
            "-rp",
            "-off",
            "1048576",
            "-len",
            "4096",
            "gsiftp://host:2811/source.bin",
            "file:///tmp/dest.bin",
        ])

    def test_url_helpers_preserve_directory_trailing_slash(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gridftp-compare-url.") as text:
            root = Path(text)
            self.assertTrue(file_url(root, directory=True).endswith("/"))
            url = gridftp_url("example.com", 2811, "/tmp/home/data", directory=True, home_dir="/tmp/home")
            self.assertTrue(url.endswith("/"))
            self.assertEqual(url, "ftp://anonymous@example.com:2811/data/")

    def test_gsi_url_encodes_root_relative_path_for_relative_paths(self) -> None:
        url = gridftp_url("example.com", 2811, "/tmp/cpnetflux/smoke.bin", auth_mode="gsi")
        self.assertEqual(url, "gsiftp://example.com:2811/tmp/cpnetflux/smoke.bin")

    def test_gsi_url_does_not_duplicate_home_dir(self) -> None:
        url = gridftp_url(
            "example.com",
            2811,
            "/srv/cpnetflux-gsi/foo.bin",
            auth_mode="gsi",
            home_dir="/srv/cpnetflux-gsi",
        )
        self.assertEqual(url, "gsiftp://example.com:2811/foo.bin")

    def test_gsi_destination_directory_is_writable_by_the_mapped_identity(self) -> None:
        command = prepare_gridftp_destination_command(
            "/tmp/gridftp-compare/run/case",
            "/tmp/gridftp-compare/run/case/dest_payload",
            "gsi",
        )

        self.assertIn("rm -rf /tmp/gridftp-compare/run/case", command)
        self.assertIn("mkdir -p /tmp/gridftp-compare/run/case/dest_payload", command)
        self.assertIn("chmod 0777 /tmp/gridftp-compare/run/case/dest_payload", command)

    def test_anonymous_destination_directory_keeps_default_permissions(self) -> None:
        command = prepare_gridftp_destination_command(
            "/tmp/gridftp-compare/run/case",
            "/tmp/gridftp-compare/run/case/dest_payload",
            "anonymous",
        )

        self.assertIn("chown -R nobody:nogroup", command)
        self.assertIn("chmod -R u+rwX,g+rwX,o-rwx", command)

    def test_gsi_source_permissions_keep_mapped_identity_readable(self) -> None:
        command = prepare_gridftp_source_permissions_command("/tmp/gridftp/source", "gsi")
        self.assertEqual(command, "chmod -R a+rX /tmp/gridftp/source")

    def test_socket_sampler_counts_only_established_data_sockets(self) -> None:
        established = ""
        established += "ESTAB 0 0 10.0.0.1:2811 10.0.0.2:40000\n"
        established += "ESTAB 0 0 10.0.0.1:32000 10.0.0.2:40001\n"
        time_wait = "TIME-WAIT 0 0 10.0.0.1:32001 10.0.0.2:40002\n"
        completed = [
            subprocess.CompletedProcess(args=["ss"], returncode=0, stdout=established, stderr=""),
            subprocess.CompletedProcess(args=["ss"], returncode=0, stdout=time_wait, stderr=""),
        ]
        with tempfile.TemporaryDirectory(prefix="gridftp-compare-sockets.") as text:
            path = Path(text) / "samples.jsonl"
            with patch("tools.experiments.gridftp_compare.runner.subprocess.run", side_effect=completed) as run:
                sampler = SocketSampler(path, case_id="case", control_port=2811, data_port_base=32000, interval=0.5)
                sampler.sample_once()

            event = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual(event["established_data_streams"], 1)
            self.assertEqual(event["peak_established_data_streams"], 1)
            self.assertEqual(event["time_wait_data_sockets"], 1)
            self.assertEqual(event["control_streams"], 1)
            self.assertEqual(sampler.max_data_streams, 1)
            self.assertEqual(run.call_args_list[0].args[0], ["ss", "-Htan", "state", "established"])


class EnvironmentTest(unittest.TestCase):
    def test_experiment_defaults_match_100m_evidence_policy(self) -> None:
        args = build_parser().parse_args(["--stage", "preflight", "--dry-run"])

        self.assertEqual(args.socket_sample_interval, 0.5)
        self.assertEqual(args.scheduler_capacity_gbps, 0.1)
        self.assertEqual(args.seed, 20260831)

    def test_environment_records_gsi_case_directory_policy(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gridftp-compare-environment.") as text:
            output_dir = Path(text)
            args = build_parser().parse_args(
                ["--stage", "preflight", "--dry-run", "--gridftp-auth-mode", "gsi"]
            )

            environment = collect_environment(args, output_dir, "run")

            self.assertEqual(environment["config"]["gridftp_auth_mode"], "gsi")
            self.assertEqual(environment["config"]["gridftp_gsi_case_directory_mode"], "0777")
            written = json.loads((output_dir / "environment.json").read_text(encoding="utf-8"))
            self.assertEqual(written["config"]["gridftp_auth_mode"], "gsi")


class ResultAndSummaryTest(unittest.TestCase):
    def test_transfer_result_classification(self) -> None:
        self.assertEqual(classify_transfer_result(exit_code=0, hash_match=True), STATUS_PASS)
        self.assertEqual(classify_transfer_result(exit_code=0, hash_match=False), STATUS_FAIL_CORRECTNESS)
        self.assertEqual(classify_transfer_result(exit_code=1, hash_match=True), STATUS_FAIL_RUNTIME)
        self.assertEqual(classify_transfer_result(exit_code=0, hash_match=True, timed_out=True), STATUS_FAIL_RUNTIME)

    def test_evidence_does_not_turn_hash_valid_transfer_into_failure(self) -> None:
        evidence = classify_evidence(
            system="cpnetflux",
            dataset_kind="tree",
            checksum="none",
            logical_bytes=1024,
            wire_bytes="",
            manifest_evidence=[],
            tree_manifest_evidence=[],
            verified_chunks="",
        )

        self.assertEqual(evidence["wire_accounting_status"], "missing")
        self.assertEqual(evidence["evidence_status"], "partial")
        self.assertIn("wire_bytes missing", evidence["evidence_errors"])

    def test_compressed_wire_bytes_are_not_an_evidence_error(self) -> None:
        evidence = classify_evidence(
            system="cpnetflux",
            dataset_kind="tree",
            checksum="none",
            logical_bytes=1024,
            wire_bytes="512",
            manifest_evidence=["file.manifest"],
            tree_manifest_evidence=[],
            verified_chunks="",
        )

        self.assertEqual(evidence["wire_accounting_status"], "compressed")
        self.assertNotIn("wire_bytes", evidence["evidence_errors"])

    def test_result_row_has_complete_csv_schema(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gridftp-compare-row.") as text:
            temp = Path(text)
            case = make_case(
                stage="smoke",
                system="cpnetflux",
                direction="local_to_remote",
                dataset="single_64MiB",
                file_parallelism=1,
                connections=1,
                repeat_index=0,
            )
            row = result_row(
                args=fake_args(temp),
                run_id="run",
                case=case,
                case_index=0,
                logical_bytes=1024,
                file_count=1,
                elapsed=1.0,
                max_streams=1,
                source_hash="abc",
                dest_hash="abc",
                source_count=1,
                dest_count=1,
                exit_code=0,
                result=STATUS_PASS,
                case_dir=temp / "case",
            )

            self.assertEqual(set(row), set(RESULT_FIELDS))
            self.assertEqual(row["logical_goodput_mbps"], "0.008192")
            self.assertEqual(row["hash_match"], "true")
            self.assertEqual(row["result"], STATUS_PASS)
            self.assertEqual(row["integrity_status"], "pass")
            self.assertEqual(row["evidence_status"], "partial")

    def test_summary_marks_unstable_spread(self) -> None:
        rows = [
            {
                "system": "cpnetflux",
                "stage": "core",
                "dataset": "single_256MiB",
                "direction": "local_to_remote",
                "file_parallelism": "1",
                "per_file_connections": "1",
                "scheduler": "off",
                "scheduler_policy": "fixed",
                "file_io_backend": "posix",
                "queue_depth": "1",
                "batch_size": "1",
                "logical_goodput_mbps": "100",
                "result": STATUS_PASS,
            },
            {
                "system": "cpnetflux",
                "stage": "core",
                "dataset": "single_256MiB",
                "direction": "local_to_remote",
                "file_parallelism": "1",
                "per_file_connections": "1",
                "scheduler": "off",
                "scheduler_policy": "fixed",
                "file_io_backend": "posix",
                "queue_depth": "1",
                "batch_size": "1",
                "logical_goodput_mbps": "130",
                "result": STATUS_PASS,
            },
        ]

        summary = summarize_rows(rows)[0]

        self.assertEqual(summary["unstable"], "true")
        self.assertEqual(summary["result"], STATUS_INCONCLUSIVE_UNSTABLE)

    def test_blocked_external_gridftp_summary(self) -> None:
        summary = summarize_rows(
            [
                {
                    "system": "gridftp",
                    "stage": "core",
                    "dataset": "single_256MiB",
                    "direction": "local_to_remote",
                    "file_parallelism": "1",
                    "per_file_connections": "1",
                    "scheduler": "off",
                    "scheduler_policy": "fixed",
                    "file_io_backend": "posix",
                    "queue_depth": "1",
                    "batch_size": "1",
                    "result": STATUS_BLOCKED_EXTERNAL_GRIDFTP,
                }
            ]
        )[0]

        self.assertEqual(summary["blocked_count"], "1")
        self.assertEqual(summary["result"], STATUS_BLOCKED_EXTERNAL_GRIDFTP)


class AuditTest(unittest.TestCase):
    def test_ssh_password_file_sets_runtime_env_without_auditing_value(self) -> None:
        old_cpnetflux = os.environ.get("CPNETFLUX_SSH_PASSWORD")
        old_sshpass = os.environ.get("SSHPASS")
        try:
            os.environ.pop("CPNETFLUX_SSH_PASSWORD", None)
            os.environ.pop("SSHPASS", None)
            with tempfile.TemporaryDirectory(prefix="gridftp-compare-secret.") as text:
                password_file = Path(text) / "remote.password"
                password_file.write_text("placeholder-password\n", encoding="utf-8")
                password_file.chmod(0o600)

                configure_ssh_password_file(argparse.Namespace(ssh_password_file=str(password_file)))

                self.assertEqual(os.environ["CPNETFLUX_SSH_PASSWORD"], "placeholder-password")
                self.assertNotIn("SSHPASS", os.environ)
        finally:
            if old_cpnetflux is None:
                os.environ.pop("CPNETFLUX_SSH_PASSWORD", None)
            else:
                os.environ["CPNETFLUX_SSH_PASSWORD"] = old_cpnetflux
            if old_sshpass is None:
                os.environ.pop("SSHPASS", None)
            else:
                os.environ["SSHPASS"] = old_sshpass

    def test_ssh_password_file_rejects_broad_permissions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gridftp-compare-secret-mode.") as text:
            password_file = Path(text) / "remote.password"
            password_file.write_text("placeholder-password\n", encoding="utf-8")
            password_file.chmod(0o644)

            with self.assertRaises(SystemExit):
                configure_ssh_password_file(argparse.Namespace(ssh_password_file=str(password_file)))

    def test_dry_run_command_audit_is_jsonl_parseable(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gridftp-compare-audit.") as text:
            temp = Path(text)
            case = make_case(
                stage="smoke",
                system="gridftp",
                direction="local_to_remote",
                dataset="single_64MiB",
                file_parallelism=1,
                connections=1,
                repeat_index=0,
            )

            record_dry_run_command(temp, "run", case, "planned", ["globus-url-copy", "-p", "1"])

            lines = command_audit_path(temp).read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(lines), 1)
            event = json.loads(lines[0])
            self.assertEqual(event["result"], STATUS_DRY_RUN)
            self.assertTrue(event["dry_run"])

    def test_results_csv_can_be_written_with_declared_fields(self) -> None:
        with tempfile.TemporaryDirectory(prefix="gridftp-compare-csv.") as text:
            temp = Path(text)
            path = temp / "results.csv"
            with path.open("w", newline="", encoding="utf-8") as handle:
                writer = csv.DictWriter(handle, fieldnames=RESULT_FIELDS)
                writer.writeheader()
                writer.writerow({field: "" for field in RESULT_FIELDS})

            self.assertIn("case_id", path.read_text(encoding="utf-8").splitlines()[0])


if __name__ == "__main__":
    unittest.main()
