# V3 Experimental Mapped Download 08

Date: 2026-11-08
Input: ab5136b90379b33869050a6186b728d682c5d401
Status: in progress; single writer on R770-1.

## Scope

Integrate scope 07's four-worker mapped-write candidate into an explicitly
selected single-task V3 download CLI. Each range attempt owns one lazy <=64 MiB
window, protected by its existing writing pin. Mapping, first touch and unmapping
are part of actual task time. Keep file bounds, DATA CRC, checksum certification,
checkpoint, work leases, recovery and atomic publication intact.

## Non-goals

No default changes, automatic selection, server receiver enablement, RDMA,
additional workers/queues, shared per-file/thread-local window cache, kernel/
network/NUMA changes, production-service restart, durable/TLS-on readiness claim,
64 GiB payload, unrelated cleanup, subagents or worktrees.

## Concrete Contract

The download executable alone reads CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE:
unset/pwrite preserves the default; mmap requires explicit inline execution,
four TCP lanes (`connections=4,file_parallelism=4,link_set_links=1`), POSIX direct
IO, no stage read/write workers, one-frame writes,
no compression/preallocation and commit_sync=none. Unsupported combinations
fail, never silently fall back. Core policy defaults false and rejects uploads,
whole-file identities, range counts outside 4..8 and files >8 GiB when selected.
The existing four-lane planner normally makes eight work ranges; this does not
add lanes or concurrent writers. Only the
CLI disables core dumps for this experiment; there is no shared-server signal
handler. A mapped-store SIGBUS terminates that CLI as a failed task. We must
prove peer EOF exits, no success/publish occurs and persisted chunks resume.

Windows are created on demand per Slot attempt. Keep their counters independently
of stack timing. Close on successful range completion, exceptional receive exit,
abort and higher-attempt reclaim before fd closure/publication. Old-attempt exits
must not touch a newly claimed slot. Mapping accounting stays separate from frame
buffers and includes configured per-range/global bounds and actual map/unmap calls.

## Affected Files

New bounded storage window header/source and focused unit tests; CMakeLists.txt;
persistent_tree_transfer header/source; tree_transfer_client header/source;
TreeTransferOptions internal opt-in; download CLI; focused V3 range tests and
fault/resume smoke; this task and docs/ROADMAP.md. Reuse existing read-only
experiment harness under /home/Su/experiments; no raw payload enters Git.

## Acceptance

1. Observe meaningful failing tests before implementation: window bounds and
   bytes, map failure, ownership/retirement, invalid configuration. Exercise
   simultaneous ranges, stale/changed attempts, corrupted data, interrupted
   checkpoint recovery and publication only after writes retire.
2. New CLI fault smoke must observe mmap failure and SIGBUS, bounded peer exit,
   no published partial output/success, then resume and independent SHA. No
   process-wide longjmp/signal recovery in C++ runtime; fatal isolation is explicit.
3. R1 all-target build and relevant CTest (broaden for shared behavior), explicit
   commit/push/readback, then R2 exact commit/configuration build/test and eight
   binary SHA checks before any inter-host data transfer.
4. Five interleaved 8 GiB baseline/candidate pairs, same fixed commit, four lanes
   and actual inline execution. TCP binds 192.168.100.1/2, eno17095np0 route and
   byte counters, resident source, exact bytes, CRC, independent SHA and V3 terminal
   gates. MemAvailable >=12 GiB, owned swap=0, host pswpout delta=0; one source and
   one output/host. Clean all owned payload after verification, keep receipts.
5. Promotion requires >=10% median complete-task gain, CV<=5%, no correctness or
   resource failure and reported both-end CPU/GiB. Retain failures/unstable cells.
   One candidate only; no new concurrency candidate if unsuccessful. Explicit
   experimental support does not imply production readiness or 100 Gbps.
6. Record final source/evidence SHAs, exact tests and commands, resource/cleanup
   acceptance and next step; commit and push every completed stage, sync R2.

## Ownership and Tools

R1/R2 start at input SHA; R1 preserves the two unrelated 2026-10-14 untracked task
files, R2 is clean. Shell/SSH, apply_patch and dedicated chat tools are exposed;
local Codex CLI exists, no dispatch/continuation is invoked. Single writer on R1;
R2 receives fixed commits only. Historical boundaries read from RESEARCH_BASELINE.
Evidence: /home/Su/experiments/CPNetFlux/2026-11-08-v3-mapped-download-08.

## Implementation Gate

Completed before inter-host experiments. Configuration red test observed the old
CLI ignoring the switch and connecting; the range red test compiled with data
fields only, transferred correctly via pwrite and failed the actual-mapping
assertions. New behavior passes window bounds/cross-window bytes/map failure,
four-range publication, blocked mmap/reclaim with independent range progress,
corrupt DATA rejection and upload/wrong-channel rejection.

All-target RelWithDebInfo build succeeds. Broad pre-final-guard/export regression:
635 cases, 631 pass, one native-RDMA environmental skip, three failures from
missing CPNETFLUX_TEST_TOKEN. A final incremental build adds the explicit core
TLS rejection and global mapping-bound export; related CTest 105/105 passes.
Authentication plus mapped fault/config cases are rerun by name with an ephemeral
token: 5/5 pass. Do not use numeric --rerun-failed after GTest discovery changes
test numbering; an intermediate numeric rerun selected different tests and is
not authentication acceptance. Final logs retain that distinction.

The 1 GiB loopback fault test proves ENOMEM -> exit 1 and real SIGBUS -> exit -7,
no published partial output, peer established sockets -> 0, then resume + external
SHA. SIGBUS recovery reused 64 MiB in the final run. mmap failure can be reported
at the top level as sibling control EOF after cancellation; the test shim's
marker proves the injected ENOMEM independently. This is failed-task isolation,
not graceful in-process SIGBUS recovery or an ENOSPC durability guarantee.

The CLI opt-in requires file_parallelism=4 because this existing option controls
the actual V3 lane count. The existing adaptive planner normally creates eight
work ranges for four lanes. No planner/concurrency change is made. Only the
download executable reads the environment switch; the core flag defaults false.
Option parsing can create an empty destination before experimental validation;
invalid configurations create no payload/checkpoint or control connection.

Next: commit/push this implementation, sync R2 and build/test/hash both ends,
then run the bounded five-pair full-task experiment. No performance promotion yet.
