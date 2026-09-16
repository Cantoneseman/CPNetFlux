from __future__ import annotations

from dataclasses import asdict, dataclass
from typing import Any


STATUS_PASS = "pass"
STATUS_DRY_RUN = "dry_run"
STATUS_FAIL_CORRECTNESS = "fail_correctness"
STATUS_FAIL_RUNTIME = "fail_runtime"
STATUS_BLOCKED_EXTERNAL_GRIDFTP = "blocked_external_gridftp"
STATUS_BLOCKED_REMOTE_AUTH = "blocked_remote_auth"
STATUS_BLOCKED_IO_URING = "blocked_io_uring"
STATUS_SKIPPED = "skipped"
STATUS_INCONCLUSIVE_UNSTABLE = "inconclusive_unstable"

RESULT_FIELDS = [
    "run_id",
    "case_id",
    "stage",
    "system",
    "tool",
    "tool_version",
    "direction",
    "dataset",
    "file_count",
    "logical_bytes",
    "profile",
    "file_parallelism",
    "per_file_connections",
    "total_stream_budget",
    "max_observed_data_streams",
    "chunk_or_block_size",
    "buffer_size",
    "checksum",
    "compression",
    "file_io_backend",
    "queue_depth",
    "batch_size",
    "control_reuse",
    "scheduler",
    "scheduler_policy",
    "repeat_index",
    "elapsed_seconds",
    "logical_goodput_mbps",
    "wire_goodput_mbps",
    "wire_bytes",
    "wire_bytes_equal_logical",
    "verified_chunks",
    "manifest_evidence",
    "tree_manifest_evidence",
    "source_tree_hash",
    "destination_tree_hash",
    "hash_match",
    "source_file_count",
    "destination_file_count",
    "exit_code",
    "result",
    "spread_percent",
    "unstable",
    "server_log",
    "client_log",
    "environment_json",
    "socket_sample_jsonl",
    "command_id",
    "error",
]

SUMMARY_FIELDS = [
    "system",
    "stage",
    "dataset",
    "direction",
    "file_parallelism",
    "per_file_connections",
    "scheduler",
    "scheduler_policy",
    "file_io_backend",
    "queue_depth",
    "batch_size",
    "repeat_count",
    "pass_count",
    "blocked_count",
    "fail_count",
    "median_logical_goodput_mbps",
    "min_logical_goodput_mbps",
    "max_logical_goodput_mbps",
    "spread_percent",
    "unstable",
    "result",
]

COMMAND_AUDIT_FIELDS = [
    "timestamp",
    "run_id",
    "case_id",
    "stage",
    "system",
    "command_id",
    "command",
    "cwd",
    "started_at",
    "ended_at",
    "duration_seconds",
    "exit_code",
    "stdout_log",
    "stderr_log",
    "result",
    "dry_run",
    "error",
]


@dataclass(frozen=True)
class PreflightResult:
    status: str
    tool: str
    tool_version: str = ""
    server_version: str = ""
    auth_mode: str = ""
    control_host: str = ""
    control_port: int = 0
    data_port_range: str = ""
    smoke_hash_match: bool = False
    server_log: str = ""
    client_log: str = ""
    error: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class CommandAudit:
    timestamp: str
    run_id: str
    case_id: str
    stage: str
    system: str
    command_id: str
    command: str
    cwd: str = ""
    started_at: str = ""
    ended_at: str = ""
    duration_seconds: str = ""
    exit_code: str = ""
    stdout_log: str = ""
    stderr_log: str = ""
    result: str = ""
    dry_run: bool = False
    error: str = ""

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class DatasetFile:
    path: str
    size: int
    sha256: str

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class ExperimentCase:
    case_id: str
    stage: str
    system: str
    direction: str
    dataset: str
    file_parallelism: int
    per_file_connections: int
    repeat_index: int
    scheduler: str = "off"
    scheduler_policy: str = "fixed"
    file_io_backend: str = "posix"
    queue_depth: int = 1
    batch_size: int = 1
    checksum: str = "none"
    compression: str = "off"
    control_reuse: str = "off"
    resume: bool = False

    @property
    def total_stream_budget(self) -> int:
        return self.file_parallelism * self.per_file_connections

    def to_dict(self) -> dict[str, Any]:
        row = asdict(self)
        row["total_stream_budget"] = self.total_stream_budget
        return row
