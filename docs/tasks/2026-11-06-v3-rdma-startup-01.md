# V3 RDMA Startup 01

Status: in progress, 2026-11-06.
Input: fd59059ab722d48f2c0036d90fe778fede878f15.
Single SSH writer on R770-1; R770-2 fixed-commit build and acceptance only.

## Scope

Attribute cold native RDMA setup to resource creation, send/receive pool
allocation/zeroing, MR registration, receive preposting and CM establishment.
Measure first; select one bounded pool-allocation improvement only if supported
by evidence. Keep setup inside complete task timing. Capture actual page backing
if a process-local huge-page hint is tested; unsupported hints must retain a
correct ordinary-page path. Preserve MR-before-buffer destruction ordering.

## Non-goals

No queue-depth, RNR policy, wire/protocol, checksum, storage, network, system
huge-page configuration, worker/thread or runtime-version changes. No pooling
across connections, hidden prewarming, zero-copy/100G/production claim. Existing
CQ teardown counters and intermittent peer-mapping rejection remain open.
Preserve production PIDs 126766/164834 and two unrelated October untracked docs.
No agents, worktrees or desktop.

## Acceptance

Write meaningful storage/ownership tests before behavioral implementation and
observe the expected negative result. Build with cmake --build build -j4;
serial related CTest on both and full CTest on R770-1, explicit local RDMA bind
and fresh test token. Preserve failure evidence. git diff --check.
Commit/push/read back each completed stage before syncing peer; compare both
commits, CMake configuration/version and eight binary SHA256 before transfers.

At least three baseline and three candidate native RDMA downloads plus one full
verification control: single lane, 1 GiB tmpfs source/target, CRC32C, direct
writes, pipeline depth 2, verified_chunks/every16/commit_sync=none. Report
component times, data connect, file and complete task wall, GNU time and CPU.
Compare the same harness and policies, without moving setup outside timing.
Check source/output residency, sender physical reads, external SHA, terminals,
QP identities, bind/routes, interface/CQ/RNR counters and sampled memory.
Preflight available >=12 GiB, tmpfs free >=2 GiB, disk free >10 GiB; reuse guard
available <6 GiB or owned RSS >4 GiB, no sampled process swap/host swap-out.
One output at a time; clean after verification and remove final owned source
only after inode/size/SHA checks. Logs stay outside Git.

## Affected files

src/core/io/rdma_data_socket.cpp; optional internal pool header and unit tests;
CMakeLists.txt if a new test is needed; RDMA duplex smoke diagnostic assertions;
docs/ROADMAP.md and this task/result. Scope refinement recorded before any
additional implementation.

Evidence: /home/Su/experiments/CPNetFlux/2026-11-06-v3-rdma-startup-01.
Payload: /dev/shm/cpnetflux-v3-rdma-startup-20261106.
Tools checked: shell and dedicated messaging exposed, messaging unused;
remote codex CLI not found. No delegation.
