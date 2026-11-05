# V3 Record Index 01

Date: 2026-11-05. Input: ec71bb0f527b3a90432e0508d003ccc8a412f453.
Writer: R770-1 /home/Su/projects/CPNetFlux, existing branch, single writer.

## Scope

Execute the next accepted receiver-hot-path step. First profile actual TransferSession and DownloadSession record lookup separately from manifest sorting, serialization, temporary write and publication. Use optional caller-owned counters/timers and a reproducible metadata-only benchmark at increasing record counts, including 65536 effective 1 MiB records. Establish an instrumented linear baseline before selecting a bounded lookup optimization. Preserve conflict rejection, out-of-order records, resume revalidation, corruption pruning, move/copy correctness and on-disk ordering/schema.

Prefer the smallest measured-beneficial append/index solution. Do not infer that the previous 52-62 second manifest aggregate is all lookup or all serialization. A fast append path may retain the existing linear fallback for irregular records if profiling supports that boundary; report it explicitly. Avoid adding a production algorithm switch just to benchmark old code.

## Non-goals

No final-verify/checksum/commit-sync/default changes; no checkpoint cadence change, wire/manifest schema change, durability weakening, shared range lock rewrite, new pipeline, system network/service modification, RDMA pool/RQ/CQ tuning or microbatch. Keep RNR/buffer and CQ errors as unaccepted stability findings, not proof of a lookup cause. Preserve the two unrelated untracked 2026-10-14 task documents and all old data.

## Affected Files

include/cpnetflux/core/session/{download_session.h,transfer_session.h,verified_chunk_record_stats.h}; src/core/session/{download_session.cpp,transfer_session.cpp}; tests/unit/{download_session_test.cpp,transfer_session_test.cpp}; tools/perf/session_record_bench.cpp; CMakeLists.txt; this task/result and docs/ROADMAP.md. Add a small shared lookup helper only if it removes real duplicated state/invariants. Experimental runners, logs and payloads remain outside Git.

The actual benchmark smoke exposed download_manifest.cpp's missing total saveSeconds timer (zero while serializeSeconds is positive). Add the same optional whole-save timer used by ManifestStore; the failing benchmark smoke must become green. Include src/checkpoint/download_manifest.cpp and tools/test/run_cpnetflux_session_record_bench_smoke.py. No publication/checkpoint behavior changes.

## Acceptance

1. Record baseline metadata benchmark wall/thread CPU, actual lookup comparisons/time, sort time, serialization/save metrics and persisted record coverage. At 4096/16384/65536 records separate final_only lookup diagnostics from every_n_chunks/16 controls. These runs write metadata only and must not be reported as file/network throughput.
2. Before optimization, observe a meaningful failing bounded-lookup regression using the instrumentation. Add semantic cases for duplicate offsets/IDs, checksum-none ID reuse, out-of-order save/resume, corrupt record removal/retransmission and session moves. Keep existing behavior as an independent oracle where appropriate.
3. Run focused session/manifest/persistent/recovery gates and full R770-1 CTest after the final code change. Commit/push explicit stage files and read back the verified CPNetFlux remote SHA. Sync only fixed commits to R770-2; matching configuration/version and binary SHA-256 are mandatory before transfers.
4. Choose the end-to-end comparison size after metadata profiling: primary fresh comparisons use the same verified_chunks/CRC32C/every_n_chunks16/commit_syncNone single-lane 64 GiB policy so full reread does not mask lookup effects. Require at least three controls and three candidates if claiming a new file-task speedup; run a full-policy integrity gate as well. Historical six-case policy results are context, not a substitute for fresh controls or proof of causality. If lookup is insignificant, bound further experiments and report that limitation instead of promising a speedup.
5. Keep native binds .1/.2, exact bytes/terminals/QPs, route/HCA evidence, independent external SHA after the timer, one fresh output at a time and cleanup only after SHA. Retain failed outputs. Verify production PIDs and temporary resources. Record source/backup/readback, scripts, all outcomes and next step in the result and external HANDOFF.md.

Evidence root on both hosts: /home/Su/experiments/CPNetFlux/2026-11-05-v3-record-index-01.
Initial tracked trees are clean; R770-2 is at the same input commit. No subagents/worktrees.
