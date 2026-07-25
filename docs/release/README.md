# GridFlux Release Gate

This directory records the alpha release gate for GridFlux. The gate packages
existing build, CTest, smoke, hygiene, and private baseline checks into one
repeatable flow. It does not change transfer defaults or run new protocol code.

## Quick Gate

Prepare both build directories first, then run:

```bash
python3 tools/release/run_alpha_release_gate.py \
  --quick \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/GridFlux \
  --results-dir tools/perf/results
```

Quick mode runs local build, default CTest, io_uring CTest, public export
hygiene, loopback STOR/RETR full and resume smoke, metadata/list smoke,
directory upload/download/resume/parallel/changed-file/corrupt-manifest smoke,
a token-auth loopback smoke, a TLS-required control-plane loopback smoke, a
Phase 6D STOR/RETR framed data TLS loopback smoke, an event-log loopback smoke
through CTest, a tiny local alpha demo with JSONL events, and residual process
checks.

## Full Gate

Full mode adds a lightweight private tree smoke, private token-auth,
TLS-required control-plane metadata, framed data TLS private STOR/RETR smokes,
a tiny private alpha demo, a short local soak smoke, and a private 1GiB repeat=3
STOR/RETR baseline matrix:

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/release/run_alpha_release_gate.py \
  --full \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/GridFlux \
  --server-host <server-host> \
  --results-dir tools/perf/results
```

The full matrix keeps project defaults and only compares checksum
`crc32c|none` plus final verify `full|verified_chunks`. The default baseline
row is reported separately. The demo step stays intentionally small; heavy tree
dataset performance remains under `tools/perf/`.

## Outputs

- Markdown report: `docs/release/ALPHA_RELEASE_GATE.md`
- JSON report: `tools/perf/results/<timestamp>_alpha-release-gate.json`
- Artifact manifest: `tools/perf/results/<timestamp>_alpha-artifacts.json`
- Logs: `tools/perf/results/<timestamp>_alpha-release-gate/`

Full gate writes an artifact manifest and syncs only those required artifacts
to the remote tree without deleting remote files. The manifest includes release
docs, release helper scripts, demo scripts, demo JSON/logs, gate JSON, private
matrix raw/summary CSV, and CSV-referenced sidecar logs. `AGENTS.md`, build
outputs, secrets, keys, tokens, and password-like paths are rejected.
Token files created by token-auth smoke tests are temporary runtime inputs and
are not valid release artifacts.
TLS smoke certificates and private keys are also temporary runtime inputs. They
must not be included in artifact manifests or public exports; strict hygiene
rejects private-key PEM markers. Phase 6D data TLS protects only STOR/RETR
framed file data sockets; LIST/NLST passive listing data remains plaintext
metadata and is not a release-gate failure.

Phase 6B gate JSON/Markdown includes per-step `error_code`, total/passed/failed
step counts, first failed step, event/demo summaries where available, and the
artifact freshness/sync status. Full gate keeps the short soak smoke local and
small; heavy long-run validation remains a separate operator task.

Phase 5C hardens manifest freshness: the gate writes final reports and JSON
first, then writes the final artifact manifest, immediately checks every listed
file against the current local size/SHA256, and only then syncs and verifies the
remote. A full gate is not valid unless local freshness is `pass`.

Artifact sync JSON distinguishes pre-sync and post-sync state:

- `pre_sync_missing` / `pre_sync_mismatch`: what was wrong before sync.
- `post_sync_missing` / `post_sync_mismatch`: what remains after sync.
- `post_sync_status`: final verification status after sync.

Manual verification:

```bash
python3 tools/release/sync_remote_artifacts.py \
  --manifest tools/perf/results/<timestamp>_alpha-artifacts.json \
  --remote <remote> \
  --local-root /root/projects/GridFlux \
  --remote-root <remote-root> \
  --verify-only \
  --json-output tools/perf/results/<timestamp>_artifact-verify.json
```

Dry-run and sync modes:

```bash
python3 tools/release/sync_remote_artifacts.py --manifest <manifest> --remote <remote> --dry-run
python3 tools/release/sync_remote_artifacts.py --manifest <manifest> --remote <remote> --sync
```

`check_remote_artifact_sync.py --manifest <manifest>` remains available as a
verify-only checker for release reports and CI-style gates.

## Alpha Release Candidate

Phase 6E adds a full release-candidate wrapper. It runs the full gate, then adds
a longer local soak with token auth, control TLS, and STOR/RETR data TLS enabled:

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/release/run_alpha_release_candidate.py \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/GridFlux \
  --server-host <server-host> \
  --results-dir tools/perf/results
```

Outputs:

- Markdown report: `docs/release/ALPHA_RELEASE_CANDIDATE.md`
- JSON report: `tools/perf/results/<timestamp>_alpha-release-candidate.json`
- Logs: `tools/perf/results/<timestamp>_alpha-release-candidate/`
- Artifact manifest: `tools/perf/results/<timestamp>_alpha-release-candidate-artifacts.json`

The RC is the recommended final alpha handoff command. It is not a production
certification: see `docs/release/ALPHA_LIMITATIONS.md`.

## Public Hygiene

`AGENTS.md` is private and must not enter public exports. Public publishing must
use:

```bash
rm -rf /tmp/gridflux-public
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public --force
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public --strict
```

Do not place passwords, tokens, private keys, cookies, or real private topology
values in public documents. Use placeholders such as `<remote>` and
`<server-host>`.
