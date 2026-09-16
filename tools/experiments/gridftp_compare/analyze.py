from __future__ import annotations

import statistics
from collections import defaultdict
from typing import Any

from .schemas import (
    STATUS_BLOCKED_EXTERNAL_GRIDFTP,
    STATUS_BLOCKED_IO_URING,
    STATUS_BLOCKED_REMOTE_AUTH,
    STATUS_BLOCKED_RESOURCE,
    STATUS_FAIL_CORRECTNESS,
    STATUS_FAIL_RUNTIME,
    STATUS_INCONCLUSIVE_UNSTABLE,
    STATUS_PASS,
    SUMMARY_FIELDS,
)


def classify_evidence(
    *,
    system: str,
    dataset_kind: str,
    checksum: str,
    logical_bytes: int,
    wire_bytes: str,
    manifest_evidence: list[str],
    tree_manifest_evidence: list[str],
    verified_chunks: str,
) -> dict[str, str]:
    """Classify observability evidence without changing transfer correctness.

    A transfer with exit code zero and matching independent hashes remains a
    successful transfer even when optional metrics are absent. Wire bytes may
    legitimately be smaller than logical bytes when compression is active.
    """
    if system != "cpnetflux":
        return {
            "wire_accounting_status": "not_applicable",
            "evidence_status": "not_applicable",
            "evidence_errors": "",
        }

    errors: list[str] = []
    if not wire_bytes or wire_bytes == "0":
        wire_status = "missing"
        errors.append("wire_bytes missing")
    else:
        try:
            wire_value = int(wire_bytes)
        except ValueError:
            wire_status = "invalid"
            errors.append("wire_bytes invalid")
        else:
            wire_status = "equal" if wire_value == logical_bytes else "compressed"

    if not manifest_evidence:
        errors.append("manifest evidence missing")
    if checksum != "none" and not verified_chunks:
        errors.append("verified_chunks evidence missing")
    if dataset_kind == "tree" and not tree_manifest_evidence:
        errors.append("tree manifest evidence missing")

    return {
        "wire_accounting_status": wire_status,
        "evidence_status": "complete" if not errors else "partial",
        "evidence_errors": "; ".join(errors),
    }


def classify_transfer_result(*, exit_code: int, hash_match: bool, timed_out: bool = False) -> str:
    if timed_out or exit_code != 0:
        return STATUS_FAIL_RUNTIME
    if not hash_match:
        return STATUS_FAIL_CORRECTNESS
    return STATUS_PASS


def _float_or_none(value: Any) -> float | None:
    if value in (None, ""):
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def _group_key(row: dict[str, Any]) -> tuple[Any, ...]:
    return (
        row.get("system", ""),
        row.get("stage", ""),
        row.get("dataset", ""),
        row.get("direction", ""),
        row.get("file_parallelism", ""),
        row.get("per_file_connections", ""),
        row.get("scheduler", ""),
        row.get("scheduler_policy", ""),
        row.get("file_io_backend", ""),
        row.get("queue_depth", ""),
        row.get("batch_size", ""),
    )


def summarize_rows(rows: list[dict[str, Any]]) -> list[dict[str, str]]:
    grouped: dict[tuple[Any, ...], list[dict[str, Any]]] = defaultdict(list)
    for row in rows:
        grouped[_group_key(row)].append(row)

    summaries: list[dict[str, str]] = []
    blocked_statuses = {
        STATUS_BLOCKED_EXTERNAL_GRIDFTP,
        STATUS_BLOCKED_REMOTE_AUTH,
        STATUS_BLOCKED_IO_URING,
        STATUS_BLOCKED_RESOURCE,
    }
    for key, group in sorted(grouped.items()):
        goodputs = [
            value
            for value in (_float_or_none(row.get("logical_goodput_mbps")) for row in group)
            if value is not None
        ]
        pass_count = sum(1 for row in group if row.get("result") == STATUS_PASS)
        blocked_count = sum(1 for row in group if row.get("result") in blocked_statuses)
        fail_count = sum(1 for row in group if row.get("result") in {STATUS_FAIL_CORRECTNESS, STATUS_FAIL_RUNTIME})
        if goodputs:
            median = statistics.median(goodputs)
            min_value = min(goodputs)
            max_value = max(goodputs)
            spread = ((max_value - min_value) / median * 100.0) if median > 0 else 0.0
        else:
            median = min_value = max_value = spread = 0.0
        unstable = bool(goodputs and spread > 20.0)
        if fail_count:
            result = STATUS_FAIL_RUNTIME
        elif blocked_count and blocked_count == len(group):
            result = str(group[0].get("result", "blocked"))
        elif unstable:
            result = STATUS_INCONCLUSIVE_UNSTABLE
        elif pass_count == len(group) and group:
            result = STATUS_PASS
        elif blocked_count:
            result = "partial_blocked"
        else:
            result = str(group[0].get("result", "unknown"))

        summary = {
            "system": str(key[0]),
            "stage": str(key[1]),
            "dataset": str(key[2]),
            "direction": str(key[3]),
            "file_parallelism": str(key[4]),
            "per_file_connections": str(key[5]),
            "scheduler": str(key[6]),
            "scheduler_policy": str(key[7]),
            "file_io_backend": str(key[8]),
            "queue_depth": str(key[9]),
            "batch_size": str(key[10]),
            "repeat_count": str(len(group)),
            "pass_count": str(pass_count),
            "blocked_count": str(blocked_count),
            "fail_count": str(fail_count),
            "median_logical_goodput_mbps": f"{median:.6f}" if goodputs else "",
            "min_logical_goodput_mbps": f"{min_value:.6f}" if goodputs else "",
            "max_logical_goodput_mbps": f"{max_value:.6f}" if goodputs else "",
            "spread_percent": f"{spread:.3f}" if goodputs else "",
            "unstable": "true" if unstable else "false",
            "result": result,
        }
        summaries.append({field: summary.get(field, "") for field in SUMMARY_FIELDS})
    return summaries
