# V3 Receiver Path 01

Status: in progress, 2026-11-06.
Input: 975da5b31f36770706f5e24053a2ba3369a07655.
Single R770-1 SSH writer; R770-2 fixed-commit receiver/build peer only.

## Scope / Order

1. Compare existing direct, 256 KiB and 1 MiB whole-file coalesced writes on the
   unchanged receive algorithm, rotating three repetitions. Inspect actual
   syscall/flush counts, total CPU, file/task wall and memory. Retain an opt-in
   configuration only when measured benefit supports it; do not change defaults.
2. Remove the receiver's logical payload vector copy and checksum-prefix erase.
   Borrow a read-only span from the frame until synchronous validation/write
   returns; compressed frames retain a bounded owned decoded vector. Preserve
   CRC-before-write, file/range state, publication and recovery semantics.
3. Measure that change separately against same-source direct baseline; three
   candidate direct runs and one full control. Keep coalescing and span effects
   separate. Defer readNext reuse and RDMA slot borrowing to later ownership work.

## Non-Goals

No protocol, default integrity/checkpoint/commit policy, pipeline depth, RDMA
pool/queue, network or system changes. No prefilled targets outside task timing,
no disk payloads/64 GiB runs, agents/worktrees/desktop or production restart.
Do not claim 100G or production stability from small successful tests.

## Gates / Memory

One lane, one 1 GiB tmpfs source on R770-2 and one new output on R770-1 at a time.
Start MemAvailable >=12 GiB and free tmpfs >=2 GiB. Use existing memory guard:
sampled MemAvailable <6 GiB or owned combined RSS >4 GiB terminates only this
experiment. Include ~2.04 GiB receive slots, send pools and tmpfs in accounting;
coalescing adds <=1 MiB per lane. No sampled process swap or host swap-out.
Warm/check source before timing, source/output residency and sender physical I/O
after; CRC/external SHA, all protocol terminals, native QPs, explicit bind/routes
and counters. Remove each output after SHA, source after final SHA. Keep original
PIDs 126766/164834, GSI QPs, historical data and two old untracked October docs.

For code changes: independent allocation regression must fail old code; wire
oracle must cover checksum none/CRC, compressed/raw and observation on/off.
Relevant build/CTest, including whole/range writes, corrupt CRC, recovery, TLS
and compression; bounded coalescing correctness. Commit/push explicit files on
R770-1, read back GitHub SHA, sync fixed commit and match eight binaries/config/
version on R770-2 before native transfers. Serial CTest limits pool concurrency.

## Files / Evidence

src/core/io/persistent_tree_transfer.cpp; tests/unit/framed_data_socket_test.cpp;
other directly relevant tests if required; docs/ROADMAP.md and this task/result.
Runtime helper scripts/evidence stay outside Git. No new infrastructure needed.

Evidence: /home/Su/experiments/CPNetFlux/2026-11-06-v3-receiver-path-01.
Payload: /dev/shm/cpnetflux-v3-receiver-path-20261106.
Reuse prior memory guard and validated small-run harness with explicit new roots.
Initial configuration probe failed before transfer: the V3 tree download CLI
does not expose file write coalescing. Add download-only --file-io-buffer-size
(0..67108864) and --posix-write-strategy (auto/direct/coalesced), validate with
the existing storage helper, and pass FileIoConfig to the local receiver policy.
Keep defaults and upload receiver ownership unchanged. A parse regression and
observed actual write counts validate this prerequisite before comparison.
Additional files: include/cpnetflux/config/tree_transfer_options.h,
src/config/tree_transfer_options.cpp, src/core/io/tree_transfer_client.cpp,
tests/unit/tree_transfer_options_test.cpp. The failed startup is retained and
excluded from performance samples; use fresh labels for successful runs.

Tools checked: shell and dedicated chat messaging exposed; chat messaging unused.
No codex CLI found on R770-1. No delegation requested or performed.

## Progress

- a9ecbe9179c3d8154a91e17fe23b4c9d445f5a74: download-only file write
  configuration, failing parse regression then 55/55 related tests on each peer.
  Pushed and GitHub SHA read back before peer synchronization/transfer.
- Nine rotated coalescing samples passed all payload/protocol/memory gates.
  Direct / 256 KiB / 1 MiB file-phase medians: 17.56 / 17.35 / 16.78 Gbps.
  Actual writes: 17408 / 5120 / 1024. No measured net benefit; default stays direct.
- Span regression observed 133 receiver allocations of 65536 bytes before the
  change and 69 after (64 wire frames + 5 chunk readback buffers). Initial test
  allowed only two readback buffers and failed the candidate; corrected after
  checking readChecksumForRecord allocates once per chunk, without removing full
  verification. Byte oracle/profile on-off raw/compressed CRC/none test passes.
  Framed 14/14 and R770-1 full 543/543 passed (257.70 s). Peer regression and
  fixed-commit native candidate measurements pending.
