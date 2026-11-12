# R770 RDMA end-to-end baseline 01

## Scope
- Establish a comparable R770-1/R770-2 baseline for native ib_send_bw, CPNetFlux native RDMA socket bench, and CPNetFlux V3 real-file upload/download.
- Cover one and four connections and total payload sizes of 4 GiB and 64 GiB.
- Run at least three repetitions per matrix cell.
- Record payload steady rate, whole-task rate including setup and teardown, filesystem read/write rate, Ethernet counters, and HCA counters.
- Re-run the post-RNR-fix file matrix and compare against the historical 3.29 Gbps result.
- Verify whether a four-connection V3 transfer of one large file actually uses four file ranges concurrently.

## Non-goals
- Do not modify the production service on port 2121.
- Do not change CPNetFlux source code before the baseline identifies a bottleneck.
- Do not treat the TCP-over-RDMA experiment as a native verbs result.
- Do not delete prior evidence or unrelated experiment directories.

## Acceptance criteria
- Both hosts use the same committed CPNetFlux revision and matching executable SHA-256 values.
- Every matrix cell has at least three completed samples or a recorded, reproducible failure reason.
- Raw command output and counter snapshots are retained with each sample.
- Upload and download checksums pass for every completed V3 file sample.
- Four-connection V3 large-file evidence states the actual range/worker concurrency, not only the configured connection count.
- Results distinguish payload steady throughput from whole-task throughput and filesystem throughput.

## Input revision
- CPNetFlux branch: codex/V3-GRIDFTP-OPTIMIZED-RERUN-01.
- Input commit at scope creation: 7bf243a32962dff1c78e48718ed375f8019ab09e.

## Affected files and artifacts
- This task record: docs/tasks/2026-11-04-r770-rdma-e2e-baseline-01.md.
- Experiment evidence: /home/Su/experiments/CPNetFlux/2026-11-04-r770-rdma-e2e-baseline-01/.
- No CPNetFlux source files are planned for modification during baseline collection.

## Validation and handoff
- Build and relevant CTest/benchmark preflight on both R770 hosts.
- Record commands, host/interface binding, route, counters, executable hashes, and result summaries.
- Commit and push this task record before test execution; synchronize the exact commit to R770-2.
