# V3 Record Index 01 Result

Date: 2026-11-05. Single writer on R770-1; R770-2 only synchronized committed sources and built/tested them.

## Version And Scope

- Input: ec71bb0f527b3a90432e0508d003ccc8a412f453.
- Instrumented linear baseline: ed8da0df2db15a9f105cc390fc13533b17b13ba7.
- Measured candidate: 92f99af71278a6517b8c0b02858b889a3c5e5986.
- Branch: codex/V3-GRIDFTP-OPTIMIZED-RERUN-01.
- Both implementation commits were pushed to the verified CPNetFlux GitHub remote; branch SHA was read back equal after each push. Final documentation backup is recorded in the external acceptance receipt.
- Evidence root on both hosts: /home/Su/experiments/CPNetFlux/2026-11-05-v3-record-index-01.
- Task: [V3 Record Index 01](2026-11-05-v3-record-index-01.md).

This stage removes ordered-record lookup work and measures its practical limits. It does not change checkpoint cadence, manifest schema, final verification, checksum, commit-sync, wire protocol, RDMA resource settings or production defaults. Existing RNR/buffer and CQ findings remain separate stability work.

## Implementation

TransferSession and DownloadSession now maintain two uint64 maxima (16 bytes per session). A record whose offset and checked chunk ID both exceed all prior identities cannot match an existing record, so it bypasses the identity scan. With checksum=None, only the offset must be new, preserving the existing repeated-ID behavior. Range bounds and overlap checks still execute. Duplicate and irregular identities retain the original linear scan; this is not a general constant-time arbitrary-order index.

Resume and corrupt-record pruning rebuild the maxima. Sorting does not invalidate them, and value moves/copies do not depend on vector positions. Regression tests explicitly exercise save/move, resume, conflicts, checksum-none ID reuse, repair after pruning, and uint64 boundaries. No per-record hash allocation or positional index is introduced.

Optional caller-owned lookup/sort counters are attached by the metadata benchmark. Production sessions keep the pointer null and do not read profiling clocks. The existing optional download manifest save metric was missing its whole-save timer: the smoke gate observed save_seconds=0 while serialize_seconds>0, then passed after adding the same timer used by upload manifests. This corrects observation only.

## Controlled Metadata Measurements

Each version ran 24 cases: download/upload; final_only at 4096, 16384 and 65536 records; every_n_chunks/16 at 65536 records; three repeats per group. The benchmark executes actual session record/save paths, then reloads and verifies every persisted identity, checksum field and coverage. Records represent 1 MiB chunks, but the benchmark writes zero payload and performs no network or final file verification. Its fixed checksum values are metadata fixtures, not proof of payload integrity.

At 65536 records and every_n_chunks/16, the medians are:

| Metric | Download Baseline | Download Candidate | Upload Baseline | Upload Candidate |
|---|---:|---:|---:|---:|
| Identity candidates examined | 2147450880 | 0 | 2147450880 | 0 |
| Lookup seconds | 1.429696 | 0.001188 | 1.432952 | 0.001221 |
| Whole metadata wall seconds | 27.123931 | 25.879379 | 27.035220 | 25.727391 |
| Thread CPU seconds | 27.118268 | 25.871302 | 27.030171 | 25.721231 |
| Serialization seconds | 22.656219 | 22.672276 | 22.480161 | 22.578128 |
| Session sort seconds | 1.565959 | 1.634218 | 1.589137 | 1.649392 |
| Manifest flush count | 4098 | 4098 | 4098 | 4098 |

Metadata wall fell 4.588389% (download) and 4.837501% (upload), saving 1.244552 and 1.307829 seconds. The lookup timer includes optional clock/counter overhead; the zero candidate count establishes that ordered scans were bypassed independently of timer precision. Small final_only cases are more sensitive to clock/filesystem noise and are not used for a file-throughput claim.

Each every16 case still serializes approximately 5.56 billion bytes cumulatively. Baseline/candidate temporary paths differ in length, so byte totals differ slightly; this does not represent a schema or checkpoint policy change. Serialization includes its internal copies, sorts, validation/range reconstruction, text formatting and body CRC. The session sort timer is a separate outer operation; do not equate all serialization time with text formatting or add overlapping aggregate timers.

Lookup was only about 5.3% of baseline metadata wall, and 1.43 seconds is about 1% of the previous approximately 145-second verified_chunks file task. The latter comparison is a prioritization estimate, not a measured end-to-end speedup. The observed metadata reduction does not establish a 4.6-4.8% file or network speedup.

Raw profiles: profile/{linear,candidate}/summary.json and each case's metrics.json/stderr.log. Frozen runners: profile.py and summarize.py. Consolidated comparison: acceptance/comparison-summary.json.

## Regression Gates

- Baseline observation smoke: meaningful failure for missing download total-save timer, corrected; focused gates 56/56 PASS.
- Before the append optimization: new typed tests produced two bounded-complexity failures and eight semantic passes under the original scan.
- Candidate focused gates: 66/66 PASS.
- R770-1 complete CTest: 525/525 PASS, 268.96 seconds.
- R770-2 fixed-commit build: PASS; related CTest: 154/154 PASS, 40.34 seconds.
- git diff --check: PASS before implementation commits. Two pre-existing untracked October task documents were preserved and excluded.

Validation commands are retained in run_validation.py. Full gate: ctest --test-dir build --output-on-failure, with ephemeral test token and explicit CPNETFLUX_TEST_RDMA_BIND. Peer filter: SessionRecordLookupTest|TransferSessionTest|DownloadSessionTest|Manifest|Persistent|session_record_bench|tree_receiver_policy|tree_recovery|Rdma. No credentials are stored in Git or the task report.

## Native File Diagnostics

Two serial fresh 64 GiB downloads from R770-2 to R770-1 passed at the fixed candidate. Both hosts have matching version/options and SHA-256 for the server, upload/download clients, RDMA socket bench, unit tests and new session record bench. Full matching hashes are in acceptance/matching-builds.json.

The diagnostics use explicit .1/.2 RDMA NIC binds, V3 native SEND/RECV, CRC32C/auto, one lane, every_n_chunks/16 and receiver commit_sync=none. Control uses TCP with anonymous authentication; control/data TLS are disabled for these fixed diagnostics. Original seed is a hardlink to the previous verified seed; no payload duplication or historical evidence overwrite. External SHA-256 is outside the client task timer. With only one sample per policy, these are integrity and termination checks, not a new repeated throughput comparison.

| Receiver Policy | Task Wall Seconds | Task Gbps | Finalize Seconds | External SHA |
|---|---:|---:|---:|---|
| full | 288.387602 | 1.906309 | 145.081791 | PASS |
| verified_chunks | 144.888411 | 3.794339 | 0.046618 | PASS |

Both samples transferred exactly 68719476736 bytes, one file and one lane, with CRC32C, no recovery/reconnect, successful FILE_RESULT and DirectoryEnd, and native QP initialization on both endpoints. Route/HCA counters corroborate the direct NIC path. Both output hashes equal 054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1. The full and verified_chunks policies still differ in post-write readback coverage. No repeated speedup estimate, CV, upload-file performance or multi-lane claim follows from these two diagnostics.

The real whole-file manifest aggregates are 56.035071 / 55.727385 seconds. Those are different measurement scopes from the metadata-only table and do not expose the substeps needed for the next optimization. Whole-file range_observation counters are zero for this non-range path; they are not evidence of zero manifest work.

RNR and out_of_buffer deltas are zero on both hosts in these two samples. Both samples nevertheless show resp_cqe_error=65 and resp_cqe_flush_error=64 on each host. These errors remain unclassified; isolated zero-RNR samples do not overturn the previous nonzero samples or establish a repair. Production stability is not accepted.

Each fresh 64 GiB receiver payload was removed only after external SHA, with checkpoint files and raw logs retained. The historical seed remains intact. Post-run inspection found no experiment process or temporary listener and only the original kernel GSI QP; production PIDs 126766 / 164834 are alive. Free space recovered to approximately 82.62 / 195.23 GiB. The first cleanup script incorrectly compared dynamic GSI sq-psn as identity and failed; its two receipts are preserved as acceptance/idle-qp-raw-comparison-failure-*.json. The corrected check uses structured rdma JSON and stable QP identity/state, and passed on both hosts. This was an external observer failure, not a leaked application QP.

Diagnostic receipts: metrics/*.verified.json, acceptance/comparison-summary.json, acceptance/final-state-*.json. Frozen experiment.py, baseline_evidence.py and inspection scripts plus their SHA-256 are recorded in acceptance/github-backup.json. Final documentation synchronization changes only docs and preserves the measured binaries.

## Next Step

First connect optional detailed timers to the actual whole-file receiver path: its current summary reports a manifest aggregate, whereas detailed manifest_save observations are attached to the range path and the new benchmark. Do not treat benchmark components as measured whole-file components. Profile serialization substeps under the same checkpoint interval and schema. Source inspection identifies repeated sorting/copying in completedFromVerified and serializeVerifiedChunks, per-record stream formatting, full body construction and CRC. Measure these separately before choosing ordered validation/sort bypass or reserved-buffer/to_chars formatting. Retain deterministic bytes/body CRC, malformed-record rejection, atomic publication, restore/repair behavior and the same persistence policy. Do not obtain apparent speedup by silently reducing checkpoint frequency or verification coverage.

After metadata gates, require a fresh repeated end-to-end control/candidate comparison before claiming file-task gains. Investigate FILE_END-period RNR/out_of_buffer and successful-exit CQ error/flush separately. The existing 100G roadmap targets remain goals; this stage does not establish production stability or 100 Gbps file throughput.
