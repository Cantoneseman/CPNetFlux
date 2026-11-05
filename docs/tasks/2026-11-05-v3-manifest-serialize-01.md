# V3 Manifest Serialize 01

Date: 2026-11-05. Input 4b4bf3a71c861e25503331e140ffec1c90e183eb.
Single writer on R770-1 /home/Su/projects/CPNetFlux, existing branch.

## Scope

Execute the accepted next step after ordered lookup: expose actual whole-file session save/lookup/sort details, split serialization into record copy/sort, range validation, record formatting and body CRC, then optimize the measured dominant work. Keep counters caller-owned and opt-in through existing phase timing. Do not reuse range counters as whole-file observations. Whole-file JSON must identify scope and unobserved cases; a retained transaction must not hold a lane's stack observation pointer.

Freeze and push an instrumented baseline before selecting the candidate. Candidate choices are a shared ordered view avoiding duplicate copy/sort and reserved-buffer numeric formatting, only as profiling warrants. Preserve byte-for-byte manifest output for equivalent valid input, deterministic ordering and body CRC, malformed-record rejection, atomic publication and resume/repair behavior. Arbitrary input vectors must remain supported without mutation.

## Non-goals

No schema, checksum/final verification, checkpoint cadence, commit-sync, runtime default, wire/lease, storage durability, buffering/pipeline, network configuration or RDMA resource/teardown changes. CQ error/flush and previous RNR findings remain unaccepted stability work. No agents/worktrees, payloads in Git, historical cleanup or modifications to the two old untracked task documents.

## Affected Files

include/cpnetflux/checkpoint/{manifest_save_stats.h,transfer_manifest.h,download_manifest.h}; src/checkpoint/{transfer_manifest.cpp,download_manifest.cpp,manifest_store.cpp}; a small checkpoint helper if it removes duplicated sorting/formatting; include/cpnetflux/core/io/persistent_tree_transfer.h; src/core/io/persistent_tree_transfer.cpp; tools/perf/session_record_bench.cpp; focused serializer/whole-file observation tests; CMakeLists.txt; this task/result and docs/ROADMAP.md. Runners/evidence remain outside Git.

## Acceptance

1. Observe a failing whole-file JSON smoke under the input code; test actual upload/download, phase timing on/off, persisted bytes and independently counted download manifest publications. Expose total save, serialize/write/publish and lookup/sort details with meaningful scopes. Existing range on/off/recovery gates must pass.
2. Add optional serialization subphase counters/timers, collect metadata baseline download/upload final_only at 4096/16384/65536 records and every16 at 65536, three repeats each. These are zero-payload measurements, not network throughput. Preserve frozen baseline binary and commit provenance.
3. Before candidate implementation, retain independent legacy-format byte oracles covering shuffled/empty/sparse/extreme records and special path bytes, plus a meaningful failing bounded copy/sort regression when optimizing that work. Existing malformed manifest, CRC, restore/pruning, move and publication tests must pass. Do not weaken rejection to improve speed.
4. Full R770-1 CTest after final source changes, peer focused gates; explicit commits/push and GitHub SHA readback for completed phases. Before native transfers require same fixed commit/configuration/version and six binary SHA-256 values on both hosts.
5. For any file-task speedup claim, fresh single-lane 64 GiB verified_chunks/CRC32C/every16/commit_sync=none controls and candidates, three per version; independent SHA outside task timer, exact bytes/native QPs/terminals, direct binds/routes and HCA counters. One candidate full-policy integrity gate. Collect controls before implementation and candidates after, report ordering/cache limitations. If measured gains do not justify the candidate, document the evidence and keep the accepted baseline rather than claim a speedup.
6. Only one fresh 64 GiB output at a time, preserve failure outputs and frozen seed; remove successful outputs after SHA, retain checkpoints/logs. Preserve production PIDs 126766/164834, verify no temporary process/listener/application QP, and retain final HANDOFF/backup receipts and next input commit.

Evidence root: /home/Su/experiments/CPNetFlux/2026-11-05-v3-manifest-serialize-01 on both hosts. Stage gates are build/CTest, metadata profiles and fixed-build native transfers in that order. No transfer from a dirty source tree. Tool audit: dedicated chat messaging and shell are exposed but no messaging was requested; R770-1 codex CLI was not found. No delegation is used.
