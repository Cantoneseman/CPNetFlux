# Phase 5D Alpha Demo Report

## Scope

Phase 5D adds a demo/operator handoff layer only. It does not change the
CPNetFlux framed STOR/RETR data path, checksum, manifest, resume, final verify,
or default transfer configuration.

## Implemented

- Deterministic demo dataset generator: `tools/demo/make_demo_dataset.py`.
- Local/private alpha demo runner: `tools/demo/run_alpha_demo.py`.
- Demo JSON output with per-case result, elapsed time, bytes, throughput,
  hashes, error, and log paths.
- Alpha release gate now runs a lightweight local demo in quick mode and a
  lightweight private demo in full mode.
- Demo scripts, `docs/DEMO.md`, demo JSON, and demo logs are included in release
  artifact manifest collection.

## Demo Coverage

Local demo:

- single-file STOR;
- single-file RETR;
- STOR resume;
- RETR resume;
- tree upload;
- tree download;
- tree resume;
- changed-file fail-safe.

Private demo:

- private STOR/resume smoke;
- private RETR/resume smoke;
- private tree upload/download/resume smoke.

## Current Status

Phase 5D local and private demo probes passed with the tiny profile:

- Local demo: single STOR, single RETR, STOR resume, RETR resume, tree upload,
  tree download, tree resume, and changed-file fail-safe all passed.
- Private demo: private STOR/resume, private RETR/resume, and private tree
  upload/download/resume all passed.

Final quick/full alpha gate, public hygiene, and artifact sync results are
recorded in `docs/release/ALPHA_RELEASE_GATE.md` and `docs/PROJECT_STATE.md`.

## Boundaries

- Defaults remain POSIX backend, full final verify, every_n_chunks manifest
  flush, preallocate off, and posix_write_strategy auto.
- No raw FTP recursion, TLS/GSI, production auth, permission/owner/xattr/ACL
  preservation, or 100G tuning is introduced in this phase.
