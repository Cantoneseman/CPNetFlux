# EXP-FEATURE-COVERAGE-01: CPNetFlux Feature Coverage Rerun

Status: completed with findings; see results and limitations below.

Input revision: `8d3b12250eab8b78af2b3b859f97787d81c7e250`.
Authoritative source: Shenzhen CPNetFlux repository, isolated branch
`codex/EXP-FEATURE-COVERAGE-01`.
Peers: R730 `gridflux-lab-small` and R750 `gridflux-lab-main`, direct TCP path
`192.168.100.1/30` <-> `192.168.100.2/30`. This is TCP over Ethernet, not native
libibverbs/RDMA verbs.

## Scope

Rebuild the committed input revision with TLS and, when liburing is available,
io_uring enabled. Run CTest before any network transfer. Then verify CPNetFlux
feature compositions and individual switches against the same seeded payloads:

- Control TLS/token and data TLS, with control-worker reuse and persistent data
  sessions enabled together with dynamic file scheduling, checksum, file-level
  parallelism, and large-file ranges.
- Dense small-file, mixed directory, and single large-file upload/download;
  compare persistent data sessions on/off and control reuse on/off where modes
  are compatible.
- Interrupted transfer and resume, verifying final manifest and SHA-256.
- Compression auto on compressible and incompressible input, recording source,
  wire, and destination bytes/hashes.
- POSIX and optional io_uring storage backends, recording unsupported/failing
  combinations as failures or skips with their exact reason.
- Bounded pipeline-depth sweep and adaptive scheduler profile where supported.
- GridFTP GSI comparison rows only as a separately identified reference; GSI
  protection and CPNetFlux TLS are not equivalent authentication modes.

Payload cap is 1 GiB per case and 10 GiB total generated/test data. Use at least
three repetitions for performance comparisons, alternate profile order, and
retain raw logs/summaries. Correctness is required for every feature row; a
throughput number without hash, manifest, and completed-state checks is invalid.

## Non-goals

- No production source, protocol, desktop, packaging, or authentication changes.
- No native RDMA verbs claims, cross-domain conclusions, or performance claims
  from loopback.
- No changes to either server's persistent network or GridFTP configuration.
- No deletion of unknown files or historical result roots; cleanup only paths
  created by this task.
- No expansion beyond 10 GiB or long-running soak.

## Acceptance

1. Record source SHA, build flags, binary SHA-256, both host/kernel/network/storage
   state, exact commands, and GSI identity/proxy lifetime when GSI is used.
2. Current-revision configure/build and full CTest pass before the first transfer.
3. Every completed transfer has matching source/destination hashes and a valid
   CPNetFlux summary/manifest; resume must finish from a controlled interruption.
4. All requested CPNetFlux feature switches are either demonstrated in a passing
   compatible profile or listed with evidence as unsupported/failed, never
   silently omitted. Separate incompatible profiles instead of implying all
   modes can be enabled simultaneously.
5. Preserve raw evidence under `/tmp/cpnetflux-runs/EXP-FEATURE-COVERAGE-01/`
   and add a concise results/limitations section here.
6. Commit only this task record, push the task branch to the verified GitHub
   remote, and read back the remote SHA. Keep root worktree changes excluded.

## Allowed paths

- `docs/tasks/2026-10-14-exp-feature-coverage-01.md`
- External build, payload, and evidence paths under `/tmp/cpnetflux-runs/`
- Explicitly named per-run `/tmp` paths on R730/R750; never shared user data.

## Rollback

Stop only PIDs started by this task and remove only its exact, empty/verified
temporary payload roots. Revert this task document with a new commit if its plan
is superseded. Do not reset the branch, alter the authoritative root worktree, or
remove pre-existing GridFTP/GSI roots or credentials.

## Results (2026-10-14)

No production source or protocol changes were made.

### Build and topology

- Input revision: 8d3b12250eab8b78af2b3b859f97787d81c7e250.
- Release build with TLS, io_uring, and tests enabled; 126 targets built. Final
  CTest passed 294/294. The first run had two failures because the test-only
  CPNETFLUX_TEST_TOKEN was unset; rerunning with that variable passed all tests.
- Deployed client hashes matched the Shenzhen build: tree upload
  6f9c72a1c09d7b07d33d9dc515c8653bbbc85a66d7b501d4eb42c52de642c685; tree
  download 52106134f942bfaf51ff62cad5bdaa6082273d1afee599bfdba25d99963798cd;
  server f222e8949d941468bc67a3a118c7cd162b2525c7bc0a0f220a8691ffea448524.
- R730 (192.168.100.1/30) and R750 (192.168.100.2/30) were directly
  connected by 100GbE Ethernet, MTU 9000. This was TCP, not native RDMA verbs.
  TCP iperf3 medians were 33.655 Gbps at one stream and 33.672 Gbps at eight
  streams; single-stream runs included one 9.424 Gbps outlier.

### Transfer coverage

All successful CPNetFlux transfer runs completed with matching source and
destination hashes. Explicit negative-path probes, including the data-TLS
hostname failure and missing-manifest resume, are listed separately below.
The main persistent profile combined control TLS and token
authentication, data TLS, CRC32C, control-worker reuse, persistent tree data
sessions, dynamic file scheduling, four-way parallelism, and range transfer.
Observed connection counts were four control and four data connections for
the 128-file tree.

| Workload and direction | Repetitions | Median logical throughput |
| --- | ---: | ---: |
| 128 x 1 MiB dense tree upload | 3 | 3.510 Gbps |
| 128 x 1 MiB dense tree download | 3 | 3.691 Gbps |
| 148-file / 256 MiB mixed tree upload | 3 | 5.684 Gbps |
| 148-file / 256 MiB mixed tree download | 3 | 5.521 Gbps |
| 256 MiB single-file upload | 3 | 2.856 Gbps |
| 256 MiB single-file download | 3 | 2.804 Gbps |

The 256 MiB file used eight 32 MiB ranges over four channels. Range IDs,
offsets, lengths, attempts, and timings were recorded, and final source/server/
download SHA-256 values matched.

Separate compatible profiles covered switches that cannot all be enabled
together:

- Dense 128-file per-file control reuse off used 128 control connections and
  achieved 0.188 Gbps median. Control-worker reuse used four connections and
  achieved 0.243 Gbps. Persistent-tree mode used four control and four observed
  data connections and achieved 3.510 Gbps on the same dense workload. The
  persistent-tree result changes multiple switches and is not an isolated
  control-reuse speedup.
- Dynamic mixed-tree upload reached 5.684 Gbps median versus 4.920 Gbps for
  static scheduling; both hashes matched.
- Pipeline depths 0, 1, 2, and 4 completed a 64 x 1 MiB per-file workload at
  0.239, 0.274, 0.261, and 0.236 Gbps median. Depth 1 was best in this small
  run set, not a general optimum.
- Compression auto was verified with the global scheduler. For compressible
  64 MiB input, wire bytes fell from 67,179,102 to 1,205,639 (about 98.2%)
  while median logical throughput fell from about 1.870 to 1.336 Gbps.
  Incompressible input produced no compressed frames. Auto compression without
  the global scheduler did not activate.
- A controlled interruption followed by persistent-tree resume completed a
  1 GiB file with matching final SHA-256; the successful resumed summary
  reported 671,789,056 wire bytes. Starting with resume before a local tree
  manifest existed correctly failed before sending payload.
- POSIX and io_uring persistent-tree runs each completed dense 64 MiB upload
  and download cases, three repetitions each, with matching hashes. The R750
  server was launched with io_uring and persistent-tree code passed the
  configured FileIoContext into the file reads. This is a functional check,
  not evidence that io_uring is faster.

### Findings and limits

- A per-file data-TLS hostname defect was reproduced. With host
  192.168.100.2 and tls-server-name nodeb.grid, per-file data TLS used the IP
  as its TLS name and failed certificate verification. Setting host to
  nodeb.grid passed. Persistent-tree mode honored the explicit TLS name. No
  fix was made. Track as V3-TLS-DATA-HOSTNAME-01 and add IP-plus-override
  regression coverage.
- Explicit persistent-tree data-session reuse cannot be combined with control
  pipeline depth, and compression auto requires the global scheduler. Results
  therefore use a matrix of compatible profiles, not one all-switches run.
- GridFTP GSI was not rerun. Earlier GSI numbers are reference evidence only:
  GSI protection and CPNetFlux TLS/token are not equivalent authentication
  modes, so this pass supports no new head-to-head conclusion. Cross-node mTLS,
  desktop UI, and admin credential management were outside this transfer-core
  pass.
- Audit of 81 CPNetFlux summaries found 11,408,506,880 transferred logical
  bytes (10.625 GiB), exceeding the planned 10 GiB cumulative cap by 0.625
  GiB. No failed summary transferred non-zero bytes. The matrix was stopped at
  that audit and no more data runs were started.
- The task CPNetFlux service was stopped. Only this task's R730/R750 payload
  directories and R730 temporary credential copy were removed. Raw logs remain
  under /tmp/cpnetflux-runs/EXP-FEATURE-COVERAGE-01/evidence/{R730,R750};
  the final CTest log is under evidence/build/CTest-LastTest.log. These
  external evidence and build files are not part of the Git commit.

Next: fix and test V3-TLS-DATA-HOSTNAME-01 separately, then run only additional
feature rows under an enforced cumulative byte cap. Schedule any GridFTP
comparison separately with equivalent datasets and explicit authentication/
protection differences.
