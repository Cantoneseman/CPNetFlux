# R770 migration result

Date: 2026-10-30

The authoritative CPNetFlux workspace is now `/home/Su/projects/CPNetFlux` on
R770-1 (`192.168.1.109`). R770-2 (`192.168.1.102`) is the synchronized build,
test and transfer peer. CPSS(DCC) is independently restored at
`/home/Su/projects/CPSS(DCC)` on R770-1.

The migration preserved the CPNetFlux Git branch, history and two unrelated
untracked task documents, and restored the CPSS source and selected experiment
records. Build directories, dependency caches, credentials, and duplicate
payloads were excluded. Cloud sources remain intact. The source did not contain
the historical CPSS `Model/` or `DCCData/full-v1/` inputs, so those remain an
explicit gap.

The local route notes and the 2026-10-29 GridFTP experiment report were also
copied to `/home/Su/experiments/reference` and indexed with SHA-256.

Both R770 nodes built commit `222bcb7b8dc6e7474577239289801406aa5339f7` with
the same RelWithDebInfo/Ninja options. CTest passed `475/475` on each node,
desktop agent tests passed `18/18`, Python evidence tests passed `7/7`, and the
CPSS dedup smoke passed `9/9`. The strict TLS certificate generator fix was
needed for OpenSSL 3.5 and is included in the same GitHub branch.

The bidirectional smoke used CPNetFlux V3 framed TCP with TLS, local bind and
the direct RDMA-capable NIC addresses `192.168.100.1` and `192.168.100.2`.
Both directions transferred 64 MiB plus small/empty files, matched SHA-256,
captured control/data TCP endpoints on `eno17095np0`, and had zero interface
errors. This is a valid direct-link TCP test; native RDMA verbs are not part of
the current CPNetFlux data plane.

GitHub backup: commit `222bcb7b8dc6e7474577239289801406aa5339f7`, remote
`git@github.com:Cantoneseman/CPNetFlux.git`, branch
`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`; remote SHA was read back equal.
