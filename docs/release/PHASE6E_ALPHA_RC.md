# Phase 6E Alpha Release Candidate

Phase 6E turns the feature-complete alpha into a repeatable release-candidate
package. It does not add new data-plane behavior and does not change defaults.

## RC Command

```bash
CPNETFLUX_SSH_PASSWORD='***' python3 tools/release/run_alpha_release_candidate.py \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/CPNetFlux \
  --server-host <server-host> \
  --results-dir tools/perf/results
```

The RC command runs the full alpha release gate, then runs a longer local soak
with token auth, control TLS, and STOR/RETR data TLS enabled. It writes a final
RC Markdown report, RC JSON, artifact manifest, and remote artifact sync/verify
summary.

## Outputs

- Markdown: `docs/release/ALPHA_RELEASE_CANDIDATE.md`
- JSON: `tools/perf/results/<timestamp>_alpha-release-candidate.json`
- Logs: `tools/perf/results/<timestamp>_alpha-release-candidate/`
- Manifest: `tools/perf/results/<timestamp>_alpha-release-candidate-artifacts.json`

## 2026-05-19 Result

Phase 6E RC passed.

- RC Markdown: `docs/release/ALPHA_RELEASE_CANDIDATE.md`
- RC JSON: `tools/perf/results/20260519T030518Z_alpha-release-candidate.json`
- RC logs: `tools/perf/results/20260519T030518Z_alpha-release-candidate/`
- RC manifest: `tools/perf/results/20260519T030518Z_alpha-release-candidate-artifacts.json`
- Nested full gate JSON: `tools/perf/results/20260519T030518Z_alpha-release-gate.json`
- Nested full gate manifest: `tools/perf/results/20260519T030518Z_alpha-artifacts.json`
- Long soak: `iterations=5`, `pass_count=5`, `fail_count=0`, `total_bytes=96584080`
- Event log: `tools/perf/results/20260519T030518Z_alpha-release-candidate/alpha_long_soak_events.jsonl`
- Artifact freshness: `checked=1080`, `stale=0`, `status=pass`
- Artifact sync: `checked=1081`, `synced=123`, `missing=0`, `mismatch=0`, `status=pass`
- Artifact verify: `checked=1081`, `missing=0`, `mismatch=0`, `status=pass`

During RC hardening, the release artifact allowlist was updated to accept
JSONL event logs as text artifacts, and the RC finalization order was adjusted
so a successful final manifest is not made stale by a later report rewrite.

## Acceptance

The RC passes only when the nested full gate passes, long soak has
`fail_count=0`, public hygiene passes, manifest freshness is `pass`, remote
artifact sync/verify has `missing=0` and `mismatch=0`, and no CPNetFlux business
processes remain.

## Defaults

Defaults remain:

- `auth-mode=anonymous`
- `tls-mode=off`
- `data-tls-mode=off`
- `file_io_backend=posix`
- `final_verify_policy=full`
- `manifest_flush_policy=every_n_chunks`
- `preallocate=off`
- `posix_write_strategy=auto`

## Remaining Risks

The alpha package is demonstrable and reproducible, but not beta/production.
See `docs/release/ALPHA_LIMITATIONS.md` for final alpha limitations.
