# CLOUD-GITHUB-01-MATERIALIZE-01: Commit the verified cloud project snapshot

- Status: ready
- Route version: R2026-09-28.1
- Task version: 1
- Owner: 05 Operations; independent acceptance: 04 Quality; coordinator: 00
- Input commit: 47b0ca2050288f4a0f2efec846535c5d130b3188
- Migration source HEAD: a076c532640ba06de016ed7ed20f7d2a6d48a0a7
- Source checkout (read-only): /root/projects/CPNetFlux
- Worktree: /tmp/cpnetflux-runs/CLOUD-GITHUB-01/materialize/src
- Branch: codex/CLOUD-GITHUB-01-MATERIALIZE-01
- Source allowlist: the 548 paths in docs/coordination/CLOUD-GITHUB-01-FILES.json, whose input SHA-256 is f7f962862a7676147f36518600c21ba7edf0d39aa2b4365d4b5680d3f2bc699d
- Recorded source bytes: 4,448,132; largest file: 215,124 bytes

## Goal

Make the complete, already hash-verified CPNetFlux snapshot available from a clean GitHub-backed Shenzhen task worktree. The migration manifest currently lists 548 files, but 203 are absent from commit 47b0ca2 and 75 tracked files in the cloud root are modified. A clean worktree at the current commit therefore lacks in-scope project material. Preserve the root checkout and materialize only the manifest allowlist in this task branch.

The user's next development priority after migration acceptance is directory-transfer performance. This task does not authorize performance implementation or experiments.

## Non-goals

- No protocol, source behavior, test, build-system, task-board, or role-roster changes.
- No build, CTest, benchmark, transfer, SSH to Shanghai, or data cleanup.
- No edits in /root/projects/CPNetFlux. Treat its current 270-item dirty worktree as read-only source.
- Never enter or alter /root/projects/GridFlux-Beta, /root/projects/CPSS(DCC), or science-compressor.
- No secrets, credentials, payloads, build outputs, or large raw evidence in GitHub.
- Do not amend, reset, clean, or switch branches in any existing worktree.

## Allowed changes

1. Copy exactly the 548 regular-file paths in the verified source manifest from the read-only root checkout into this task worktree, preserving file mode where practical.
2. Correct the two migration control manifests:
   - In CLOUD-GITHUB-01-FILES.json, remove the prose exclusion "cloud-only migration marker and validation artifacts"; add an explicit cloud_only_artifacts array with exactly these paths: .cpnetflux-migration-marker, docs/coordination/CLOUD-GITHUB-01-FILES.json, docs/coordination/CLOUD-GITHUB-01-MANIFEST.json, docs/tasks/2026-09-25-cross-domain-smoke-result.md. Identify their roles; do not add these paths to the 548 source entries.
   - In CLOUD-GITHUB-01-MANIFEST.json, update per_file_manifest.sha256 to the newly computed SHA-256 of CLOUD-GITHUB-01-FILES.json and list the same four exact paths under verification.known_remote_only_artifacts. Keep entry_count at 548 and preserve the five machine-readable exclusions.
3. Add this task and docs/tasks/2026-09-28-cloud-github-01-materialize-01-result.md.

No other path may be staged or committed.

## Preconditions and stop conditions

- Confirm the task worktree starts clean at the exact input commit and the root source still has the expected manifest SHA.
- Confirm every source allowlist path is relative, unique, traversal-free, a regular file (not a symlink), and matches its recorded byte count and SHA-256 before copying.
- The 548 files total 4,448,132 bytes according to the input manifest; fail closed if the live total or any file hash differs.
- Run a high-confidence secret scan over the allowlist. Do not print matched values. If a private key, live credential, token, or unresolved suspicious value appears, stop before staging and report only the affected path and redacted finding category.
- Preserve the root dirty state byte-for-byte. Do not include files absent from the source manifest.

## Acceptance commands and evidence

- Use a path-safe copy routine; then independently recompute all 548 destination sizes and SHA-256 values and require exact equality with the manifest.
- Validate both JSON documents; require 548 unique ordered entries, the five exact exclusions, four exact cloud-only artifact paths, and matching embedded manifest SHA-256.
- Stage only paths from the source manifest, the two control manifests, this task, and its result. Verify the staged path set against that allowlist before commit.
- Run git diff --cached --check; verify no secrets or files over the recorded 215,124-byte maximum were added.
- Commit on the named branch, push origin codex/CLOUD-GITHUB-01-MATERIALIZE-01, and read back the remote ref; require the remote SHA to equal the local commit SHA.
- Leave the task worktree clean after commit. Do not declare the migration gate accepted; 04 must independently audit this exact commit and the root source parity, then 00 records the final gate decision.
- Retain the task worktree and all evidence. Do not delete the old root checkout or any prior migration worktree.

## Expected artifacts

- This versioned task file.
- docs/tasks/2026-09-28-cloud-github-01-materialize-01-result.md with commands, exit codes, source/destination hashes, secret-scan summary without values, commit SHA, push result, remote SHA, final status, and remaining limitations.
- A clean committed worktree for independent 04 review.
