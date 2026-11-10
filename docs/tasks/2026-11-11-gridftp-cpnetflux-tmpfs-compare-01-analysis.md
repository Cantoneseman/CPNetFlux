# CPNetFlux/GridFTP tmpfs compare 01 analysis

There are no comparable performance samples. The blocker occurred before GridFTP smoke, workload generation and build comparison. This run cannot answer per-workload best throughput, task time, 8 GiB repeatability, small-file/mixed-directory winner, or distance from 100 Gbps.

Current resource observations suggest capacity may be adequate for later bounded runs: each node has 16 GiB free tmpfs and MemAvailable above 12 GiB. Root filesystems have only about 45/42 GiB free; verify every source and destination with findmnt -T and use tmpfs only. Current credentials/service state does not permit entering the large-data phase.

Both .1/.2 reverse routes select eno17095np0 and link reports 100 Gb/s. That nominal rate says nothing about application throughput. No TCP, CPNetFlux, GridFTP or native RDMA benchmark ran; this preflight provides no evidence that CPU, memory, tmpfs or NIC is the performance bottleneck.

Security semantics also need separate reporting: previous CPNetFlux work used TLS/token, while previous GridFTP used GSI/private. Authentication, data protection, transaction/commit and checksum costs differ. Do not claim strict security equivalence; report external SHA separately from CPNetFlux internal validation.

Resume order: refresh proxy only via the approved credential manager; verify at least one hour validity and trust; run an isolated GridFTP service on ports proven free and an 8 MiB private smoke; then pass same-revision build/hash gate and bounded screening. Stop if a controlled service cannot be safely established.
