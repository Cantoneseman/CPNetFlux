# V3 Source Supply 01 Result

Date: 2026-11-06 Asia/Shanghai. Status: twelve source reads, independent SHA and related
gates complete; retain diagnostic/input-safety changes, defer unproven runtime buffer changes.
Input `5732a7cf587333a93d910812805a5e42893a6889`; measured tool source
`5b8be5af0c034451c117f372e346c049228299cc`, branch
`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`. Development was a single writer over passwordless
SSH on R770-1; R770-2 received a fixed commit. No desktop control or agents were used.

## Delivered Tooling

The existing `cpnetflux-storage-bench` now preserves its input for `--mode read` on
success and failure, even without `--keep-file`. The baseline deleted both normal and
truncated temporary test fixtures; red logs are retained. Write/rewrite cleanup is
unchanged. Added explicit `--read-checksum none|crc32c` for read mode, with CRC32C/auto
per read block plus checksum combine, completed read bytes, resolved backend, digest
and checksum timing. Non-read CRC mode is rejected before opening files. The default
read workload remains without checksum; no transfer behavior/default changed.

The smoke test uses an independent software CRC32C oracle, several nonaligned buffer
sizes, repeated reads, source inode/content preservation and invalid mode/option
rejection. The full checksum test failed on the old missing option before implementation.
`tools/perf/run_source_supply.py` runs 12 source-only cases, preserves per-case logs,
CPU/resource/block snapshots and provenance, and checks SHA-256 outside the timer.
Its complete 12-case small-fixture smoke also passed.

Related CTest gates passed 51/51 on R770-1 and 51/51 on R770-2. Build configuration,
version and seven binary hashes match. The six previous transfer/unit/session-bench
binary hashes remain identical to the prior native measured source; this stage only
changes the storage benchmark. No native throughput rerun or new file-speed claim is
made from this tool-only change. The previous full-suite 534/534 result is historical;
this stage ran the directly related gates instead of relabelling that old full run.

## Method

Evidence root: `/home/Su/experiments/CPNetFlux/2026-11-06-v3-source-supply-01` on both hosts;
raw source matrix originally runs on R770-2 under `source-64g/`. Input remains
`/home/Su/experiments/CPNetFlux/2026-11-05-v3-manifest-serialize-01/R770-2/input/seed-64g/payload/payload.bin`.
It is the previous hardlinked 68719476736-byte seed, expected SHA-256
`054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1`.

Read-only 65536/1048576/8388608-byte buffers are measured in three rotated rounds;
then three 65532-byte CRC32C/auto reads use the current sender's frame-sized read block.
All cases use POSIX, advice off, queue/batch one, one iteration per process. The
application reads the complete file; source inode/device/size/mtime/ctime is checked
before/after each case. No system cache drop, payload write, network transfer or
parallel read workload is performed. `--keep-file` is supplied defensively as well.

Buffered reads are not cold/direct I/O or a hardware maximum. The first file may have
some cached pages; read-size order rotates but checksum cases come after all pure reads.
Process physical read bytes use GNU time filesystem-input blocks x512, including
read-ahead; diskstats are whole-device evidence and can include other activity. CPU is
child user/system time, while benchmark elapsed includes the read loop and optional
checksum. Checksum seconds are a nested subset, not extra wall time. Initialization
of the reusable read buffer is outside benchmark elapsed, but inside process wall/CPU.
This sequential diagnostic does not model allocation, frame encoding, pipeline queue
or network costs; those are compared using existing actual sender timing separately.
The CRC group uses 65532 bytes rather than the pure-read group's aligned 65536 bytes,
and follows it in time. Its directly timed checksum subset is reported, but the wall
difference between these groups must not be attributed entirely to CRC processing.
The diagnostic reads continuously across the file; it does not clamp each read at the
production 1 MiB logical chunk boundary. Consequently its read/combine call count is
not an exact replay of the native producer, which can emit a short DATA frame there.

## Measurements

All twelve timed reads and independent source SHA passed. Every case reads
68719476736 logical bytes, with approximately 67.6 GB physical input accounting.

| Source workload, n=3 per row | Median Seconds | Median Gbps | Median User CPU s | Median System CPU s |
|---|---:|---:|---:|---:|
| POSIX read, 64 KiB | 135.831 | 4.04734 | 0.17 | 13.28 |
| POSIX read, 1 MiB | 138.885 | 3.95836 | 0.06 | 26.97 |
| POSIX read, 8 MiB | 138.654 | 3.96494 | 0.02 | 27.73 |
| POSIX read + CRC32C, 65532 bytes | 135.886 | 4.04570 | 3.44 | 12.98 |

Population throughput CV is respectively 0.020983%, 0.082726%, 0.035487% and 0.005968%.
Physical-read medians are 67652419584 / 67652550656 / 67652550656 / 67652419584 bytes.
Each process also accounts for 4096 bytes of physical output for its result log; the
storage benchmark reports zero payload write calls/bytes, and the source identity and
expected SHA are unchanged. Total unique benchmark evidence is under 1 MiB, no payload copy.

The CRC group uses hardware backend, produces `56cb220d` on all three reads and spends
median 2.68731 seconds in per-block CRC/combine. Its readAtAll subset is approximately
132.99 seconds. Larger pure-read buffers reduce call count from 1048576 at 64 KiB to
65536 at 1 MiB and 8192 at 8 MiB, but show no useful throughput gain in these runs.
The runtime sender read size and pipeline budget are therefore unchanged.
An additional 30-second iostat sample spanning the first two read cases is retained as
`acceptance/read-storage-iostat.json`; it is not a full-matrix aggregate.

A separate R770-1 checksum control reused the same 65532-byte RAM buffer for 64 GiB
of logical CRC work, without touching the source host's storage experiment. Hardware
CRC32C/auto results were 0.633313/0.637702/0.624800 seconds (median 868.064 Gbps of
cache-hot logical checksum bytes). This is a primitive microbenchmark: the unique
working set is only about 64 KiB, with no combine, frame, allocation, file or network
cost. It does not establish 868 Gbps transfer ability or source-file checksum speed.
Evidence: `acceptance/crc-memory-control.json` and three raw logs.

## Interpretation And Next Gate

The prior actual sender observations on source `791c2eee` are readAtAll median
127.136050 seconds and consumer wait 114.453123 seconds, with a roughly 139-second file
phase. Prior complete native task rate is 3.856495 Gbps, under CRC32C/verified_chunks,
every16 and commit_sync=none. Those overlapping intervals cannot be summed. They are
from a different day's workload/cache history, not a matched A/B test of this tool.
The prior task rate is 95.284678% of the measured 64 KiB read median. That ratio
supports prioritizing source supply under this workload, but is not proof that the
network/software path has no further cost or that storage hardware cannot go faster.

The repeated read-size/CRC results support source-supply investigation before another
receiver-only throughput change. No runtime prefetch, read-size,
buffer, pipeline, checkpoint or verification setting is changed merely because the
tool supports a larger buffer. Current ~30 GiB RAM cannot hold the complete 64 GiB seed.
The source resides on `/dev/sda2` ext4, with a RAID-labelled block device; its label is
not evidence for 100G storage service rate. 100 Gbps application payload alone needs
12.5 GB/s source supply before other work. Separate fast-storage or verified cache
controls are needed to investigate network/software headroom beyond this source path.

The build includes io_uring, but this matrix uses POSIX. Code inspection shows the
existing `ioUringReadAtAll` initializes and exits a ring for each requested read and
splits that request into depth-sized SQEs (`src/storage/file_io_uring.cpp`). Selecting
that backend or increasing its queue depth is not equivalent to adding persistent
cross-frame prefetch. A future queue-depth/control experiment must measure that cost
and retain the same source-preservation and physical-I/O evidence before deciding on
a persistent reader implementation. No io_uring performance conclusion is made here.

RNR/out_of_buffer and CQ error/flush from previous native experiments remain open.
This stage opens no application QP and cannot establish their resolution or production
readiness. Source service rate and lifecycle stability remain separate acceptance gates.

Next serial task:

1. Compare existing io_uring read-only queue/batch depths 1/4/16 at a fixed large bounded
   request size, with physical-I/O and ring setup/completion counts. Inspect initialization
   lifetime and cancellation/partial-read behavior before choosing persistent prefetch.
   Keep this distinct from the completed POSIX block-size matrix.
2. Establish a separately labelled cache-resident native file control with verified near-zero
   source physical reads, or a demonstrably faster mounted storage source. Preserve the
   64 GiB disk workload and document differences in file size, manifest work and durability;
   a small cached-file result cannot replace its task-throughput result.
3. In a separate stability experiment, split RNR and CQ counters around live transfer,
   FILE_RESULT/DirectoryEnd and QP close/destroy, retaining ownership/recovery tests.
   No default queue/pipeline/checkpoint changes until their own evidence gates pass.

## Reproduction And Handoff

At the fixed measured commit after build, on R770-2:

```sh
cd /home/Su/projects/CPNetFlux
ctest --test-dir build --output-on-failure -R 'FileIo|Checksum|storage|checksum_bench'
python3 tools/perf/run_source_supply.py \
  --project /home/Su/projects/CPNetFlux \
  --source /home/Su/experiments/CPNetFlux/2026-11-05-v3-manifest-serialize-01/R770-2/input/seed-64g/payload/payload.bin \
  --evidence /home/Su/experiments/CPNetFlux/NEW-EMPTY-EVIDENCE-DIRECTORY \
  --commit 5b8be5af0c034451c117f372e346c049228299cc \
  --sha256 054d6505471beef2084a6106822cfbc6542d60f4b8e52413f4c38def5ee36cd1 \
  --production-pid 164834
```

Use a new evidence directory; the runner intentionally refuses to overwrite it. Raw
data stays outside Git. Both post-run final-state inspections passed: original service
PIDs 126766/164834, only kernel GSI QP identity, no benchmark/runner process, clean tracked
source and seven fixed hashes. The seed inode/device/size/mtime/ctime and SHA are intact.
The small peer evidence is copied to R770-1. Final documentation-only commit/sync and
GitHub readback are recorded in `acceptance/github-backup.json` and `HANDOFF.md` rather
than confusing the document revision with the measured tool revision. The two old
untracked October tasks and Windows project remain untouched.
