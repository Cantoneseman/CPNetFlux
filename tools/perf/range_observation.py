"""Safe extraction and export of CPNetFlux server-side range observations."""

from __future__ import annotations

import csv
import hashlib
import json
import tarfile
from pathlib import Path
from typing import Iterable

MARKER = b"CPNETFLUX_PERSISTENT_TIMING "
METRIC_FIELDS = (
    "data_frame_count", "effective_data_bytes", "range_write_batch_bytes",
    "entry_lock_calls", "entry_lock_wait_seconds", "entry_lock_hold_seconds",
    "offset_write_calls", "offset_write_seconds",
    "writer_call_count", "writer_call_seconds",
    "file_write_at_all_calls", "file_write_at_all_bytes",
    "file_write_at_all_failures", "file_write_at_all_seconds",
    "data_flush_calls", "data_flush_seconds",
    "chunk_flush_calls", "chunk_flush_seconds", "chunk_checksum_seconds",
    "checkpoint_seconds", "checkpoint_manifest_flush_calls",
    "checkpoint_manifest_flush_seconds", "background_checkpoint_thread_cpu_seconds",
    "manifest_save_attempts", "manifest_serialize_calls", "manifest_serialized_bytes",
    "manifest_save_seconds", "manifest_temp_write_calls", "manifest_rename_calls",
    "manifest_publish_calls", "manifest_publish_successes", "manifest_publish_seconds", "manifest_failures",
    "manifest_serialize_seconds", "manifest_temp_write_seconds", "manifest_rename_seconds",
)

MANIFEST_FIELDS = {
    "attempts": "manifest_save_attempts",
    "serialize_calls": "manifest_serialize_calls",
    "serialized_bytes": "manifest_serialized_bytes",
    "save_seconds": "manifest_save_seconds",
    "temp_write_calls": "manifest_temp_write_calls",
    "rename_calls": "manifest_rename_calls",
    "publish_calls": "manifest_publish_calls",
    "publish_successes": "manifest_publish_successes",
    "publish_seconds": "manifest_publish_seconds",
    "failures": "manifest_failures",
    "serialize_seconds": "manifest_serialize_seconds",
    "temp_write_seconds": "manifest_temp_write_seconds",
    "rename_seconds": "manifest_rename_seconds",
}

IDENTITY_FIELDS = (
    "source_log", "line_number", "batch_id", "channel_index",
    "file_id", "range_id", "attempt", "observation_status",
)


def _numeric(value: object) -> int | float | None:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return value


def _safe_id(value: object) -> int | str | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return value
    if isinstance(value, str) and len(value) <= 128:
        return value
    return None


def _metrics(file_record: dict) -> dict[str, int | float | None]:
    observation = file_record.get("range_observation")
    result = {name: None for name in METRIC_FIELDS}
    if file_record.get("range_observation_enabled") is False or not isinstance(observation, dict):
        return result

    for name in METRIC_FIELDS:
        if name not in MANIFEST_FIELDS.values():
            result[name] = _numeric(observation.get(name))

    manifest = observation.get("manifest_save")
    if isinstance(manifest, dict):
        for source, target in MANIFEST_FIELDS.items():
            result[target] = _numeric(manifest.get(source))
    return result


def _row(source_log: str, line_number: int, payload: dict, file_record: dict) -> dict:
    observation = file_record.get("range_observation")
    if file_record.get("range_observation_enabled") is False:
        status = "disabled"
    elif not isinstance(observation, dict):
        status = "missing"
    elif observation.get("schema_version") == 2:
        status = "enabled"
    else:
        status = "legacy_partial"

    return {
        "source_log": source_log,
        "line_number": line_number,
        "batch_id": _safe_id(payload.get("batch_id")),
        "channel_index": _safe_id(payload.get("channel_index")),
        "file_id": _safe_id(file_record.get("file_id")),
        "range_id": _safe_id(file_record.get("range_id")),
        "attempt": _safe_id(file_record.get("attempt")),
        "observation_status": status,
        "metrics": _metrics(file_record),
    }


def parse_server_logs(log_paths: Iterable[str | Path]) -> dict:
    """Parse timing records while retaining only allow-listed metadata and metrics."""
    observations = []
    sources = []
    contract = None

    for path in sorted((Path(item) for item in log_paths), key=lambda item: (item.parent.name, item.name)):
        source_name = f"{path.parent.name}/{path.name}"
        digest = hashlib.sha256()
        byte_count = parsed = malformed = 0
        with path.open("rb") as stream:
            for line_number, raw in enumerate(stream, 1):
                digest.update(raw)
                byte_count += len(raw)
                marker_index = raw.find(MARKER)
                if marker_index < 0:
                    continue
                try:
                    payload = json.loads(raw[marker_index + len(MARKER):].decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    malformed += 1
                    continue
                parsed += 1
                if contract is None and isinstance(payload.get("range_observation_contract"), dict):
                    contract = payload["range_observation_contract"]
                files = payload.get("files")
                if not isinstance(files, list):
                    malformed += 1
                    continue
                for file_record in files:
                    if isinstance(file_record, dict):
                        observations.append(_row(source_name, line_number, payload, file_record))

        sources.append({
            "name": source_name,
            "sha256": digest.hexdigest(),
            "bytes": byte_count,
            "timing_records": parsed,
            "malformed_records": malformed,
            "status": "present" if parsed else "missing",
        })

    counts = {}
    for row in observations:
        name = row["observation_status"]
        counts[name] = counts.get(name, 0) + 1
    if not observations:
        status = "missing"
    elif (counts.get("enabled") == len(observations) and
          all(source["status"] == "present" and source["malformed_records"] == 0
              for source in sources)):
        status = "complete"
    else:
        status = "partial"
    return {
        "schema": "cpnetflux_range_observations_export_v1",
        "status": status,
        "source_logs": sources,
        "counts": counts,
        "contract": contract,
        "observations": observations,
    }


def export_range_observations(log_paths: Iterable[str | Path], output_dir: str | Path) -> dict:
    result = parse_server_logs(log_paths)
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=True)
    (output / "range-observations.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    columns = list(IDENTITY_FIELDS) + list(METRIC_FIELDS)
    with (output / "range-observations.csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns, lineterminator="\n")
        writer.writeheader()
        for row in result["observations"]:
            writer.writerow({**{key: row.get(key) for key in IDENTITY_FIELDS}, **row["metrics"]})
    return result

def write_evidence_bundle(output_dir: str | Path, evidence_names: Iterable[str]) -> Path:
    """Build a hash-indexed archive from an explicit allow-list of evidence files."""
    output = Path(output_dir).resolve()
    names = sorted(set(evidence_names))
    entries = []
    for name in names:
        relative = Path(name)
        if relative.is_absolute() or len(relative.parts) != 1 or relative.name in {
            "evidence-manifest.json", "range-observation-evidence.tar.gz",
            "range-observation-evidence.tar.gz.sha256",
        }:
            raise ValueError(f"unsafe evidence entry: {name}")
        path = output / relative
        if path.is_symlink():
            raise ValueError(f"evidence entry must not be a symlink: {name}")
        if not path.is_file():
            raise FileNotFoundError(path)
        data = path.read_bytes()
        entries.append({
            "name": relative.name,
            "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        })

    manifest = {
        "schema": "cpnetflux_range_observation_evidence_v1",
        "included_files": entries,
        "excludes": ["raw server logs", "credentials", "private keys", "payload files"],
    }
    manifest_path = output / "evidence-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    archive = output / "range-observation-evidence.tar.gz"
    included = [output / entry["name"] for entry in entries] + [manifest_path]
    with tarfile.open(archive, "w:gz") as bundle:
        for path in included:
            bundle.add(path, arcname=path.name)
    archive_digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (output / (archive.name + ".sha256")).write_text(
        f"{archive_digest}  {archive.name}\n", encoding="ascii"
    )
    return archive
