# CPNetFlux/GridFTP tmpfs compare 01

Date: 2026-11-11
State: RUNNING; user authorized generation of task-scoped local GSI credentials on 2026-11-11.
Input commit: a811f0b54c4075a5c13a3208cf14015e378d3963

## Scope

Compare current CPNetFlux and Globus GridFTP end to end over the same 100 GbE TCP path with tmpfs source and destination on R770-1/R770-2. Bind data explicitly to 192.168.100.1 and 192.168.100.2. Screen CPNetFlux 1/2/4/8 lanes and GridFTP 4/8/16 streams on one 1 GiB file and dense directory, then freeze per-workload winners. Formal matrix: incompressible 1/4/8 GiB both directions; 8 GiB structured data both directions; 2048 x 64 KiB, 10000 x 4 KiB and mixed directory both directions. Use the specified paired repetitions and interleaving. Max one dataset is 8 GiB.

Before generating formal input, require a GSI proxy with at least one hour validity, verified GridFTP trust, and an 8 MiB GSI/private smoke. Record security semantics, configuration, versions, full-task monotonic wall, effective rate, directory files/s, resource usage, interface/HCA counters and independent SHA-256. Do not count auth/integrity/commit/resource failures as throughput samples.

## Non-goals

- No source, system, NIC, routing or firewall changes.
- Do not stop, replace or reconfigure existing services.
- Do not call TCP over the RDMA NIC verbs; no 64 GiB workloads.
- No formal payload before authentication passes; no credentials, payloads or build outputs in Git.

## Acceptance

- Same source commit, CMake options, program versions and binary hashes on both nodes.
- GSI proxy validity/trust and 8 MiB private smoke pass before the matrix.
- Every formal transfer and external checksum succeeds; collect route, interface/HCA, CPU/I/O, memory/swap and tmpfs/inode evidence.
- Report median, range, CV and paired difference per workload/config. Separate configuration screening, TCP comparison and any optional native RDMA ceiling.

## Files in scope

- docs/tasks/2026-11-11-gridftp-cpnetflux-tmpfs-compare-01.md
- docs/tasks/2026-11-11-gridftp-cpnetflux-tmpfs-compare-01-result.md
- docs/tasks/2026-11-11-gridftp-cpnetflux-tmpfs-compare-01-analysis.md
- /home/Su/experiments/CPNetFlux/2026-11-11-gridftp-cpnetflux-tmpfs-compare-01/preflight.md

Credential amendment: generate an isolated SimpleCA, per-node user and host certificates, and 12-hour RFC3820 proxies under the experiment credentials directory. Trust and grid-mapfile are scoped by process environment; no /etc or institutional identities are modified. Both proxies must pass grid-proxy-init -verify and grid-proxy-info -exists -valid 1:00. This is a laboratory GSI/private comparison, not an institutional credential deployment. No credential contents enter Git or reports. Experiment-only orchestration and evidence live under the experiment directory; no production source changes.
