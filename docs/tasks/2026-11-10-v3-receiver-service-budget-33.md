# V3 Receiver Service Budget33

Date:2026-11-10. Status: audit implemented; R1 gates passed; peer/backup closure pending.
Input:5664854dc268b151ade7adcd0aabb757e7b5c3c1.
R1 sole writer, R2 fixed-commit build/test peer. Preserve unrelated October14 docs.

## Scope, Non-Goals And Files

Execute the next boundary from TCP Ownership32: audit existing full-task17 and
diagnostic32 evidence plus source. Separate CPU demand from wall/critical-path
savings, verify source compatibility and review receiver page/copy/CRC/lease
ownership. Identify at most one new removable mechanism and conservative>=10%
complete-task budget. Only a mechanism passing that gate can justify a separate
implementation scope. An unestablished budget ends this branch for this round.

Edit tools/perf/audit_receiver_service_budget.py (existing evidence only), this
task/result, docs/ROADMAP.md and docs/README.md. Evidence:
/home/Su/experiments/CPNetFlux/2026-11-10-v3-receiver-service-budget-33.
No runtime/CMake/default/protocol/checkpoint/ACK/lease change, new instrumentation,
performance samples, frame/thread/queue sweep, RDMA, system policy or reopening
unchanged eliminated candidates. Do not substitute4KiB page count or on-CPU
percentages for task-time savings. No payload generation or inter-node transfers.

## Acceptance

1. Parse existing structured receipts/metrics; verify raw receiver profile hashes,
   decoded-script sample/period totals and flat CRC Self against the accepted
   profile reports. Preserve missing/truncated caller attribution explicitly.
   Record input evidence digests; never regenerate or edit historical evidence.
2. Compare actual checksum/session/executor/queue/range-write spans with scope17,
   record changed files and limits. Count logical frame work for the current
   1MiB chunk/65532-byte maximum data contract. Distinguish session/range CRC
   combine from content scans and moved vectors from copied payload bytes.
3. Review page preparation, direct receive/copy, CRC/lock placement and lease
   operations using established results. A larger upper envelope does not prove
   a removable critical path. No new candidate if conservative budget is unknown.
4. Validate the audit parser with independent synthetic controls/rejections and
   actual accepted receipts. Run relevant existing CMake/CTest gates, retain
   command/logs/errors. No broad performance matrix is justified for this audit.
5. Commit/push/readback on R1, fixed-commit R2 build/test/configuration/three
   runtime hashes, verify source/evidence mirror and own temporary cleanup.

## Handoff Boundary

If no mechanism passes, pause the current large-file receiver software candidate
branch, keep the approximately27.3Gbps reference and unmet targets explicit.
Do not schedule another receiver profiler or parallelism trial automatically.
S1B remains an independent existing opt-in-to-default acceptance item: recent
scope32 guarded1GiB preparation/transfers show a stable counter interval but do
not prove8GiB preparation safe. A future dedicated gate must first recheck stable
resources and permit only one bounded attempt under unchanged zero-swap gates.
No small-file gain or default promotion substitutes for the large-file target.
