# CPNetFlux Observability Alpha

Phase 6B adds opt-in structured observability for the alpha system. It does not
change transfer defaults, the framed STOR/RETR data path, checksum, manifest,
resume, or final verify semantics.

## JSONL Event Log

Most alpha executables accept:

```bash
--event-log /tmp/cpnetflux/events.jsonl
```

Covered components:

- `cpnetflux-gridftp-server`
- `cpnetflux-file-client`
- `cpnetflux-file-server`
- `cpnetflux-file-download-client`
- `cpnetflux-tree-upload-client`
- `cpnetflux-tree-download-client`

Each line is one JSON object:

```json
{
  "timestamp": "2026-05-18T12:00:00Z",
  "component": "cpnetflux-gridftp-server",
  "event": "stor_complete",
  "transfer_id": "abc123",
  "direction": "upload",
  "path": "dataset/file.bin",
  "result": "pass",
  "error_code": "ok",
  "message": "",
  "elapsed_seconds": 0.123,
  "bytes": 1048576
}
```

The event log is append-only JSONL. Parent directories are created at startup.
If the path cannot be opened, startup or CLI parsing fails instead of silently
dropping the log.

## Error Codes

Stable alpha error strings:

| Code | Meaning |
|------|---------|
| `ok` | Operation succeeded |
| `auth_required` | Protected command was used before login |
| `auth_failed` | USER/PASS or token auth failed |
| `tls_required` | Plain control connection attempted against TLS-required listener |
| `tls_failed` | TLS handshake, certificate, key, or CA validation failed |
| `data_tls_required` | STOR/RETR data TLS was required but not negotiated |
| `data_tls_failed` | Framed file data TLS handshake or IO failed |
| `path_rejected` | Path validation rejected the request |
| `manifest_corrupt` | Manifest load/CRC/metadata validation failed |
| `checksum_mismatch` | Chunk or file checksum validation failed |
| `changed_file` | Tree resume detected size/mtime drift |
| `remote_sync_failed` | Release artifact sync/verify failed |
| `io_error` | Filesystem, socket, read, write, stat, or rename failure |
| `protocol_error` | Frame/control protocol violation or unexpected reply |
| `config_error` | Invalid CLI/configuration |
| `unknown_error` | Error did not match a stable alpha class |

The C++ status model is unchanged. Phase 6B maps known messages to stable
strings for event logs, JSON summaries, demo reports, and release gate output.

## Demo And Gate Summaries

`tools/demo/run_alpha_demo.py` accepts `--event-log` and writes:

- `event_summary.event_count`
- `event_summary.error_code_counts`
- `event_summary.first_error`

`tools/release/run_alpha_release_gate.py` records per-step `error_code`, total
steps, passed/failed counts, first failed step, artifact freshness, and artifact
sync/verify summaries.

## Soak Smoke

Short local soak:

```bash
python3 tools/test/run_alpha_soak_smoke.py \
  --build-dir build \
  --iterations 3 \
  --profile tiny \
  --event-log tools/perf/results/alpha-soak-events.jsonl \
  --json-output tools/perf/results/alpha-soak.json
```

The soak smoke repeatedly runs the tiny local alpha demo and reports
`iterations`, `pass_count`, `fail_count`, `first_failure`, `total_bytes`,
`elapsed_seconds`, `error_code_counts`, and `event_log_path`. Phase 6E adds
operator knobs for longer runs: `--duration-seconds`, `--profile tiny|small|mixed`,
`--token`, `--tls`, and `--data-tls`.

## Credential Red Lines

Event logs, JSON summaries, demo reports, release artifacts, and public exports
must not contain token values, passwords, private keys, cookies, or local
`AGENTS.md` content. Token auth events only record auth mode/result and stable
error codes. TLS events may record mode/result and `tls_required`/`tls_failed`,
but never certificate private-key material or token contents.

Phase 6B/6C/6D observability remains alpha only. Phase 6C TLS events cover the
control connection. Phase 6D data TLS events cover STOR/RETR framed file data
sockets. LIST/NLST listing data remains plaintext and only its control command
path is covered by control-plane TLS. GSI, Prometheus, a metrics server,
production auth, raw FTP STOR/RETR, and 100G performance tuning remain out of
scope.
