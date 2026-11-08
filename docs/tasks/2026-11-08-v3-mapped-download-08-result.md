# V3 Mapped Download 08 Result

Date: 2026-11-08. Scope: [task](2026-11-08-v3-mapped-download-08.md).
Input: `ab5136b90379b33869050a6186b728d682c5d401`.
Source: `535d33db5d38fe7e380d022e100195a94ec5e32e`.
Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`; version 0.1.0.
The source commit was pushed to `Cantoneseman/CPNetFlux` and read back at origin.

## Decision

Retain the explicitly selected, four-lane experimental mapped receiver in the V3
download executable. It passes the >=10% median gain, CV<=5%, correctness and
resource gates. Default receiver execution/backend is unchanged. No server-side
receiver enablement, automatic selection, RDMA or additional stage concurrency.

This advances full-task throughput from about 24 to about 29 Gbps for the tested
memory workload. The 30 Gbps lower midterm threshold remains open. Neither the
local diagnostic's 55 Gbps nor raw TCP bandwidth is a full-file result.

## Implementation

- Only `cpnetflux-tree-download-client` reads
  `CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE=pwrite|mmap`; unset means default.
- mmap requires inline, four actual TCP lanes (`connections=4`,
  `file_parallelism=4`, `link_set_links=1`), POSIX direct, zero stage workers and
  file-IO buffer, batch=1, CRC32C, no compression/preallocation, commit_sync=none.
  Core checks reject uploads, TLS data sockets, whole files, range counts outside
  4..8 and individual files >8 GiB. Unsupported choices fail without fallback.
- Each range attempt owns a lazy <=64 MiB MAP_SHARED window. Four inline lanes
  bound active windows to 256 MiB. Existing writing pins protect the fd and slot;
  retry waits for old IO, and a stale exit cannot retire a replacement's mapping.
  Range completion closes its window before publication. All receive exits close
  their own attempt window after any writer has drained. Map/unmap/bytes/bounds
  appear separately from frame-buffer metrics in per-range JSON timings.
- DATA CRC, verified chunks, leases, checkpoint recovery and atomic publication
  use the existing path. Mapping/first touch/unmapping are included in task time.
  No prefaulting and no independent output files are used to inflate throughput.
- mmap errors return Status. SIGBUS terminates the isolated opt-in CLI; its core
  dumps are disabled. No process-wide recovery handler or longjmp is installed.

## Acceptance

Meaningful configuration and actual-mapping tests failed on the pre-implementation
behavior, then passed. Window tests cover boundary crossing, exact readback,
overflow/out-of-bounds, mmap failure and idempotent retirement. Range tests cover
parallel progress during blocked mmap, waiting retry after ENOMEM, publication,
corrupt frame rejection and upload/wrong-lane rejection. Existing reclaim,
changed partition, lease, resume, checksum, manifest and writer regressions remain.

R1 all-target build succeeded. Broad earlier build: 635 tests, 631 pass, one native
RDMA environmental skip, three missing-test-token failures. Final related build:
105/105 passes. Actual failed authentication tests and mmap fault/config tests are
rerun by name with a temporary token: 5/5 passes. An intermediate numeric
`--rerun-failed` selected different tests after GTest discovery changed numbering;
it is retained as evidence but is not authentication acceptance. R2 all-target
build and related 107/107 pass at the exact source commit.

The isolated 1 GiB loopback fault test verifies:

| Injected fault | CLI exit | Published partial output | Peer established sockets after EOF | Resume |
| --- | ---: | --- | ---: | --- |
| mmap ENOMEM | 1 | none | 0 | external SHA passes |
| mmap then truncate, real SIGBUS | -7 | none | 0 | external SHA passes; 64 MiB reused in final run |

The first error may surface at task level as sibling control EOF following
cancellation; the fault shim's marker independently proves ENOMEM injection.
This test does not establish graceful SIGBUS recovery, actual tmpfs ENOSPC
recovery, durability, or correctness against an external concurrent writer.

## Full-task Experiment

R770-2 sends to R770-1. Both ends have the same source/configuration/version and
eight binary SHA-256 values. RelWithDebInfo; ISA-L/io_uring/TLS/RDMA compiled ON,
but this experiment explicitly selects TCP, TLS off, POSIX direct and no sync.
TCP uses 192.168.100.2 -> 192.168.100.1, explicit local bind and eno17095np0.
Routes/interface byte counters are retained; no native verbs QPs are initialized.

One 8 GiB fully resident tmpfs source, one fresh receiver output at a time.
Five pairs in alternating order; same source commit, CRC32C,
verified_chunks, manifest/checkpoint batching 16, depth 2, read0/write0, inline,
compression off, preallocate off, commit_sync=none. Timed from task process launch
through client completion/summary; independent external SHA is checked afterward.
Server physical source read_bytes delta is zero in all ten samples.

| Metric | pwrite | mmap |
| --- | ---: | ---: |
| Samples | 5 | 5 |
| Median complete-task Gbps | 24.039570 | 28.864204 |
| Min / max Gbps | 23.168454 / 24.684108 | 27.720301 / 29.234460 |
| CV, population stddev / mean | 2.083093% | 1.874964% |
| Median receiver CPU seconds/GiB | 1.233750 | 0.983750 |
| Median sender CPU seconds/GiB | 1.212500 | 1.237500 |
| Median both-end CPU seconds/GiB | 2.437500 | 2.213750 |

Median task gain: **20.069554%**. Paired gains: 17.970516%, 19.824827%,
20.354323%, 19.646746%, 20.069554%. Both-end CPU/GiB decreases 9.179487%;
receiver CPU/GiB decreases 20.263425%. Medians of the CPU components need not sum
to the median of the sample-wise totals.

All ten tasks pass exact bytes, CRC, independent SHA, no recovery, V3 terminals,
route/interface counts, source residency, zero native QPs and output cleanup.
Each mapped task reports exactly 128 maps/unmaps, zero map failures/active bytes
at completion, 8 GiB written and <=64 MiB peak window per range. Data and window
accounting are checked from actual receiver records, not just requested options.

Minimum MemAvailable: R1 20.552109 GiB, R2 20.002949 GiB (gate >=12 GiB).
Polled maximum owned RSS: R1 226.265625 MiB, R2 19.804688 MiB. tmpfs backing
pages are budgeted separately; mapped RSS references those pages, not another
8 GiB buffer. Owned swap and host pswpout delta remain zero. All owned source,
output and sidecar payload roots are removed, both /dev/shm report zero used
bytes. Production PIDs 126766/164834 retain October 30 start times.

## Evidence and Commands

Evidence root on R1:
`/home/Su/experiments/CPNetFlux/2026-11-08-v3-mapped-download-08` (~9.5 MiB).
No payload enters Git. The two unrelated October untracked task files are kept.

```sh
cmake --build build -j 8
ctest --test-dir build --output-on-failure -R 'Mapped|PersistentRange|PersistentSession|PersistentTree|TransferStages|receiver_policy|range_observation_export|offset_write|cpnetflux_mapped_download'
# Evidence runner supplies only an ephemeral test token, never saves its value:
python3 "$EVIDENCE/run_acceptance_08.py" -R 'control_token_smoke|event_log_smoke|peer_mapping_smoke|cpnetflux_mapped_download'
python3 "$EVIDENCE/mapped_matrix_08.py" check 535d33db5d38fe7e380d022e100195a94ec5e32e
# Seed action ran on R2; source receipt was fetched to R1 before matrix:
python3 "$EVIDENCE/mapped_matrix_08.py" matrix 535d33db5d38fe7e380d022e100195a94ec5e32e
python3 "$EVIDENCE/analyze_mapped_08.py"
# cleanup-input ran on R2; R1 empty payload root was removed with rmdir:
python3 "$EVIDENCE/final_audit_08.py"
```

Set EVIDENCE to the evidence root above. The matrix refuses to overwrite receipts;
use a new, explicitly scoped root for a new experiment. Related logs are
`build-acceptance.log`, `ctest-broad.log`, `ctest-final.log`,
`ctest-auth-and-fault-by-name.log`, `ctest-R770-2.log`, `matrix.log`.
Build equality is in `acceptance/matching-builds-mapped08-before-transfer.json`;
ten `metrics/*.verified.json` receipts retain full task/resource/counter evidence.

| Artifact | SHA-256 |
| --- | --- |
| Download executable | e1d39dbc1ee89e4d116e31b64c3d993d33ccc4a988447471375b85bd4cfc3c6e |
| Server executable | d164e94c6587e69af35208e6694c94ee48938dffbd3c9e89d1abb6d8cb14e249 |
| mapped-analysis.json | f60eaf28421a287e400f023b31db134bca80ceee5472661817057cb4705bb41c |
| mapped_matrix_08.py | a0ea8333b4f2e42c6518aa730c02a7fa8b2c5f3da0c2118b2bd40e20aab79790 |
| analyze_mapped_08.py | 2a25b97fa4d75197a5d42148a80efa73d6ebc6a3ddae9c9b3fe3c228dcb20fac |
| fault-acceptance.json | 37d2316f80f03860d04e56d66910a9b1766d04a28eb7ee105e07bc878c176e2a |
| cleanup-acceptance.json | 2dd1dc13ad44edf9b35fb234b56154a8b1661325088b4aed39ed008788abb463 |

## Next Direction

Three-stage architecture expansion remains closed. This one write-backend
candidate succeeded, so retain its explicit experimental capability. The next
independent task should profile the new approximately 29 Gbps full-task path:
attribute sender preparation/CRC, receiver frame/CRC, Entry lock, checkpoint and
idle/wait CPU before selecting one hypothesis. Do not extrapolate the old pwrite
profile or add concurrency to compensate for an unidentified cost. Keep bounded
memory and a fixed-commit five-pair acceptance gate.

Reverse direction/current single-lane comparison, TLS-on, durable writes,
mixed loads and longer stability must be accepted before default selection or
production enablement. Small-file microbatch remains an independent research
scope and cannot replace the still-open large-file 30-50, 70-85 and 100 Gbps goals.
