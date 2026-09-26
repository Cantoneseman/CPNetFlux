# CLOUD-GITHUB-01-RECONCILE: Complete the per-file migration manifest

- Route: R2026-09-24.16
- Input commit: 2523956be60217df377306f3493fa749397662b4
- Worktree: /tmp/cpnetflux-runs/CLOUD-GITHUB-01/reconcile/src
- Owner: 00 commander; 04 quality independent audit
- Goal: store path/size/SHA-256 for the 548 migrated source files and independently verify parity.
- Non-goals: no source edits, build/test/experiment, deletion, or access to GridFlux-Beta, CPSS(DCC), or science-compressor. No project writes in Windows worktree.
- Allowed files: CLOUD-GITHUB-01-MANIFEST.json, CLOUD-GITHUB-01-FILES.json, this task and its result; 04 may add only its task-authorized QA receipt.
- Acceptance: 548 unique paths; all sizes and SHA-256 readable; source/destination 548/548 with zero differences; explain three cloud-only artifacts; push exact commit and verify remote SHA. 04 independently verifies manifest digest, structure, count, uniqueness, and read-only source/destination hashes.
- Evidence: this task, per-file JSON, summary manifest, stage result, and 04 QA receipt. 00 decides whether to clear migration gate after QA.
