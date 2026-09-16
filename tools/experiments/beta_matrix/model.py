from __future__ import annotations

import math
from dataclasses import asdict, dataclass, replace
from typing import Any


MIB = 1024 * 1024
SMALL_FILE_THRESHOLD = 64 * 1024


@dataclass(frozen=True)
class TransferParams:
    concurrency: int = 2
    pipeline_depth: int = 4
    block_size: int = 262_144
    session_reuse: bool = True
    read_pipeline: bool = True

    def clamp(self) -> "TransferParams":
        return TransferParams(
            concurrency=max(1, min(8, self.concurrency)),
            pipeline_depth=max(1, min(32, self.pipeline_depth)),
            block_size=max(65_536, min(1_048_576, self.block_size)),
            session_reuse=self.session_reuse,
            read_pipeline=self.read_pipeline,
        )

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


@dataclass(frozen=True)
class LinkProfile:
    rtt_ms: float = 13.0
    bandwidth_mbps: float = 44.0

    @property
    def bandwidth_bytes_per_sec(self) -> float:
        return max(self.bandwidth_mbps * 1_000_000 / 8.0, 1.0)


@dataclass(frozen=True)
class WorkloadProfile:
    dataset: str
    file_count: int
    total_bytes: int
    size_min: int
    size_max: int
    size_p50: float
    size_p90: float
    size_mean: float
    domain: str = ""
    description: str = ""

    @classmethod
    def from_manifest_row(cls, name: str, row: dict[str, Any]) -> "WorkloadProfile":
        file_count = max(1, int(row.get("file_count", 1)))
        if "total_bytes" in row:
            total_bytes = int(row["total_bytes"])
        else:
            total_bytes = int(float(row.get("total_mb", 0.0)) * MIB)
        mean = total_bytes / file_count if file_count else 0.0
        if file_count <= 4:
            size_max = total_bytes
            size_p90 = max(mean, float(size_max))
        else:
            size_max = int(max(mean, mean * min(4, file_count)))
            size_p90 = max(mean, float(size_max))
        size_min = 0 if total_bytes == 0 else max(1, int(min(mean, total_bytes)))
        return cls(
            dataset=name,
            file_count=file_count,
            total_bytes=total_bytes,
            size_min=size_min,
            size_max=size_max,
            size_p50=float(mean),
            size_p90=float(size_p90),
            size_mean=round(mean, 1),
            domain=str(row.get("domain", "")),
            description=str(row.get("description", "")),
        )

    def to_dict(self) -> dict[str, Any]:
        return asdict(self)


B0_BASELINE = TransferParams(1, 1, 262_144, False, False)
B1_SESSION_REUSE = TransferParams(2, 1, 262_144, True, False)
B2_READ_PIPELINE = TransferParams(2, 4, 262_144, False, True)
B3_FULL = TransferParams(4, 8, 262_144, True, True)

PRESETS: dict[str, TransferParams] = {
    "B0_baseline": B0_BASELINE,
    "B1_session_reuse": B1_SESSION_REUSE,
    "B2_read_pipeline": B2_READ_PIPELINE,
    "B3_full": B3_FULL,
}


@dataclass(frozen=True)
class MethodSpec:
    method: str
    experiment_group: str
    transport: str
    compression: str
    compression_scope: str
    description: str

    @property
    def is_cpnetflux(self) -> bool:
        return self.transport == "cpnetflux"


@dataclass(frozen=True)
class MethodPlan:
    dataset: str
    method: str
    experiment_group: str
    transport: str
    compression: str
    compression_scope: str
    status: str
    reason: str
    control_reuse_mode: str
    file_parallelism: int
    connections: int
    planner_preset: str
    transfer_params: TransferParams
    regime: str
    dominant_component: str
    estimated_seconds: float
    command_hint: str

    def to_row(self) -> dict[str, Any]:
        return {
            "dataset": self.dataset,
            "experiment_group": self.experiment_group,
            "method": self.method,
            "transport": self.transport,
            "compression": self.compression,
            "compression_scope": self.compression_scope,
            "status": self.status,
            "reason": self.reason,
            "control_reuse_mode": self.control_reuse_mode,
            "file_parallelism": self.file_parallelism,
            "connections": self.connections,
            "planner_preset": self.planner_preset,
            "regime": self.regime,
            "dominant_component": self.dominant_component,
            "estimated_seconds": f"{self.estimated_seconds:.6f}",
            "command_hint": self.command_hint,
        }


def classify_regime(profile: WorkloadProfile) -> str:
    if (
        profile.file_count > 32
        and profile.size_p50 < SMALL_FILE_THRESHOLD
        and profile.total_bytes < 5 * MIB
    ):
        return "small_files"
    if profile.size_max >= 50 * MIB and profile.file_count <= 4:
        return "large_objects"
    if profile.file_count > 10 and profile.size_p90 >= MIB:
        return "mixed"
    if profile.total_bytes >= 80 * MIB:
        return "bandwidth"
    return "general"


def estimate_job_sec(
    profile: WorkloadProfile,
    link: LinkProfile,
    params: TransferParams,
    *,
    preset_name: str | None = None,
) -> dict[str, float]:
    rtt = link.rtt_ms / 1000.0
    workers = max(1, min(params.concurrency, profile.file_count))
    files_per_worker = math.ceil(profile.file_count / workers)
    sessions = workers if params.session_reuse else profile.file_count
    connect_sec = 0.05
    meta_sec_per_file = 0.003
    rtt_per_file = 2.0 * rtt
    if params.read_pipeline:
        rtt_per_file *= 0.55
    t_connect = connect_sec * sessions
    t_handshake = rtt * sessions + files_per_worker * rtt_per_file
    t_meta = meta_sec_per_file * profile.file_count
    t_data = profile.total_bytes / (link.bandwidth_bytes_per_sec * workers)
    t_job = t_connect + t_handshake + t_meta + t_data
    if preset_name == "B3_full":
        t_job *= 0.98
    return {
        "t_connect": round(t_connect, 6),
        "t_handshake": round(t_handshake, 6),
        "t_meta": round(t_meta, 6),
        "t_data": round(t_data, 6),
        "t_job_est": round(t_job, 6),
    }


def dominant_component(estimate: dict[str, float]) -> str:
    components = {
        "connect": estimate["t_connect"],
        "handshake": estimate["t_handshake"],
        "meta": estimate["t_meta"],
        "data": estimate["t_data"],
    }
    return max(components, key=components.get)


def _best_preset(profile: WorkloadProfile, link: LinkProfile, allowed: set[str]) -> tuple[str, TransferParams, dict[str, float]]:
    best_name = sorted(allowed)[0]
    best_params = PRESETS[best_name].clamp()
    best_estimate = estimate_job_sec(profile, link, best_params, preset_name=best_name)
    for name in sorted(allowed):
        params = PRESETS[name].clamp()
        estimate = estimate_job_sec(profile, link, params, preset_name=name)
        if estimate["t_job_est"] < best_estimate["t_job_est"]:
            best_name = name
            best_params = params
            best_estimate = estimate
    return best_name, best_params, best_estimate


def choose_b5_preset(profile: WorkloadProfile, link: LinkProfile) -> tuple[str, TransferParams, dict[str, float]]:
    regime = classify_regime(profile)
    if regime == "small_files":
        allowed = {"B1_session_reuse", "B3_full"}
    elif regime in {"large_objects", "bandwidth"}:
        allowed = {"B2_read_pipeline", "B3_full"}
    elif regime == "mixed":
        allowed = {"B1_session_reuse", "B2_read_pipeline", "B3_full"}
    else:
        allowed = set(PRESETS)
    return _best_preset(profile, link, allowed)


def choose_b6_preset(profile: WorkloadProfile, link: LinkProfile) -> tuple[str, TransferParams, dict[str, float]]:
    regime = classify_regime(profile)
    if regime == "small_files":
        name = "B1_session_reuse"
    elif regime in {"large_objects", "bandwidth"}:
        name = "B3_full"
    elif regime == "mixed":
        name = "B3_full"
    else:
        name = "B0_baseline"
    params = PRESETS[name].clamp()
    return name, params, estimate_job_sec(profile, link, params, preset_name=name)


def build_method_matrix() -> list[MethodSpec]:
    return [
        MethodSpec("cpnetflux_b0_baseline", "control_reuse", "cpnetflux", "raw", "per_file", "CPNetFlux current default behavior"),
        MethodSpec("cpnetflux_b1_control_reuse", "control_reuse", "cpnetflux", "raw", "per_file", "One control session per file worker"),
        MethodSpec("cpnetflux_b2_read_pipeline_proxy", "control_reuse", "cpnetflux", "raw", "per_file", "FDT read-pipeline concept placeholder"),
        MethodSpec("cpnetflux_b3_reuse_parallel", "control_reuse", "cpnetflux", "raw", "per_file", "Control reuse plus file-level parallelism"),
        MethodSpec("cpnetflux_cpss", "compression", "cpnetflux", "cpss", "per_file_blocks", "CPSS staging then CPNetFlux transfer"),
        MethodSpec("cpnetflux_gzip", "compression", "cpnetflux", "gzip", "per_file_blocks", "gzip staging then CPNetFlux transfer"),
        MethodSpec("cpnetflux_lz4", "compression", "cpnetflux", "lz4", "per_file_blocks", "lz4 staging then CPNetFlux transfer"),
        MethodSpec("gridftp_raw", "control_reuse", "gridftp", "raw", "per_file", "GridFTP raw transfer baseline"),
        MethodSpec("gridftp_cpss", "compression", "gridftp", "cpss", "per_file_blocks", "CPSS staging then GridFTP transfer"),
        MethodSpec("gridftp_gzip", "compression", "gridftp", "gzip", "per_file_blocks", "gzip staging then GridFTP transfer"),
        MethodSpec("gridftp_lz4", "compression", "gridftp", "lz4", "per_file_blocks", "lz4 staging then GridFTP transfer"),
    ]


def _cpnetflux_preset_for_method(method: str) -> tuple[str, TransferParams] | None:
    if method == "cpnetflux_b0_baseline":
        return "B0_baseline", B0_BASELINE
    if method == "cpnetflux_b1_control_reuse":
        return "B1_session_reuse", B1_SESSION_REUSE
    if method == "cpnetflux_b3_reuse_parallel":
        return "B3_full", B3_FULL
    return None


def _command_hint(plan: MethodPlan) -> str:
    if plan.transport == "gridftp":
        return "globus-url-copy or XTransfer-GridFTP row; requires GCT/GSI preflight"
    if plan.status == "unsupported":
        return ""
    return (
        "cpnetflux-tree-upload-client ... "
        f"--file-parallelism {plan.file_parallelism} "
        f"--connections {plan.connections} "
        f"--control-reuse {plan.control_reuse_mode} "
        f"--planner-preset {plan.planner_preset}"
    )


def plan_cpnetflux_method(profile: WorkloadProfile, link: LinkProfile, method: str) -> MethodPlan:
    regime = classify_regime(profile)
    if method == "cpnetflux_b2_read_pipeline_proxy":
        params = B2_READ_PIPELINE
        estimate = estimate_job_sec(profile, link, params, preset_name="B2_read_pipeline")
        plan = MethodPlan(
            dataset=profile.dataset,
            method=method,
            experiment_group="control_reuse",
            transport="cpnetflux",
            compression="raw",
            compression_scope="per_file",
            status="unsupported",
            reason="CPNetFlux Beta v1 has no FDT-style read pipeline proxy in the tree transfer path",
            control_reuse_mode="off",
            file_parallelism=params.concurrency,
            connections=max(1, min(4, params.concurrency)),
            planner_preset="B2_read_pipeline",
            transfer_params=params,
            regime=regime,
            dominant_component=dominant_component(estimate),
            estimated_seconds=estimate["t_job_est"],
            command_hint="",
        )
        return plan

    preset = _cpnetflux_preset_for_method(method)
    if preset is None:
        params = B0_BASELINE
        preset_name = "compression_staging"
    else:
        preset_name, params = preset
    params = params.clamp()
    estimate = estimate_job_sec(profile, link, params, preset_name=preset_name)
    control_reuse = "worker" if params.session_reuse else "off"
    if method in {"cpnetflux_cpss", "cpnetflux_gzip", "cpnetflux_lz4"}:
        control_reuse = "off"
    status = "planned"
    reason = "dry-run plan only; no cloud transfer executed"
    plan = MethodPlan(
        dataset=profile.dataset,
        method=method,
        experiment_group="compression" if method in {"cpnetflux_cpss", "cpnetflux_gzip", "cpnetflux_lz4"} else "control_reuse",
        transport="cpnetflux",
        compression=method.removeprefix("cpnetflux_") if method.startswith("cpnetflux_") and method not in {"cpnetflux_b0_baseline", "cpnetflux_b1_control_reuse", "cpnetflux_b3_reuse_parallel"} else "raw",
        compression_scope="per_file_blocks" if method in {"cpnetflux_cpss", "cpnetflux_gzip", "cpnetflux_lz4"} else "per_file",
        status=status,
        reason=reason,
        control_reuse_mode=control_reuse,
        file_parallelism=params.concurrency,
        connections=max(1, min(4, params.concurrency)),
        planner_preset=preset_name,
        transfer_params=params,
        regime=regime,
        dominant_component=dominant_component(estimate),
        estimated_seconds=estimate["t_job_est"],
        command_hint="",
    )
    return replace(plan, command_hint=_command_hint(plan))


def plan_method(profile: WorkloadProfile, link: LinkProfile, spec: MethodSpec) -> MethodPlan:
    if spec.transport == "cpnetflux":
        return plan_cpnetflux_method(profile, link, spec.method)
    params = B0_BASELINE
    estimate = estimate_job_sec(profile, link, params, preset_name="gridftp_external")
    plan = MethodPlan(
        dataset=profile.dataset,
        method=spec.method,
        experiment_group=spec.experiment_group,
        transport=spec.transport,
        compression=spec.compression,
        compression_scope=spec.compression_scope,
        status="planned",
        reason="dry-run plan only; GridFTP rows require cloud-side XTransfer-GridFTP/GCT preflight",
        control_reuse_mode="external",
        file_parallelism=1,
        connections=1,
        planner_preset="gridftp_external",
        transfer_params=params,
        regime=classify_regime(profile),
        dominant_component=dominant_component(estimate),
        estimated_seconds=estimate["t_job_est"],
        command_hint="globus-url-copy ...; do not replace with scp or rsync",
    )
    return plan
