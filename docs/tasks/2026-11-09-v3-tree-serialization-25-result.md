# S2B Tree Serialization25 Result

Date:2026-11-09. Local feasibility PASS. Runtime/file-throughput acceptance NOT
performed. Candidate remains only in cpnetflux-tree-manifest-serialization-bench;
CPNetFlux runtime, manifest schema/frequency, ACK, CRC, resume and publish behavior
are unchanged. Summary default original/cached opt-in and S1B hold remain.

## Source And Changes

Input cf73a90ac386e4a0837980ca3b2012a33d133a4e. Measured source
a173124cb17a37fd62207d0d050243ca8c9870ac, pushed/read back before matrix.
R1 sole writer. Added diagnostic target, bounded experiment driver, independent
Python byte oracle/CMake test. No runtime source/header changed. The benchmark
links the real public baseline serializer, validation/parser/status/CRC APIs.

Candidate borrows strictly ordered records; arbitrary order and equal keys use
one private full-vector copy with the same std::sort/comparator as legacy. Equal
keys cannot use the borrow shortcut because std::sort is not stable. Fields are
appended into a reserved string using to_chars and direct hex encoding. It keeps
validation, scalar header/body CRC and complete output, without persistent caches
or assumed immutable metadata. All input objects remain unchanged.

The independent oracle encodes boundary fixtures in Python and computes bitwise
CRC32C separately. Empty/shuffled records, path/error special bytes, UTF8 paths,
all file statuses, signed timestamp/unsigned size extremes, upload/download,
none/CRC32C headers, duplicate paths, short valid transfer IDs and invalid
version/root/path/control/ID inputs are covered. Duplicate serialization remains
accepted as in legacy; duplicate parsing remains rejected by the shared parser.

## Declared Matrix And Results

Command: python3 tools/perf/run_tree_manifest_serialization.py
--commit a173124cb17a37fd62207d0d050243ca8c9870ac
--evidence /home/Su/experiments/CPNetFlux/2026-11-09-v3-tree-serialization-25/formal.

Five interleaved baseline/candidate pairs for sorted and reversed input each,
10K records/225 evolving-state snapshots,20 measured process runs. Includes
fixture creation, status evolution, validation, ordering, encoding/body CRC,
common result digest and destruction. One thread; no filesystem/network writes
of manifest payload, production service or host policy changes. Owned address
space capped256MiB; independent process/host resource guard enabled.

| Input | Baseline CPU median s | Candidate CPU median s | CPU reduction | Baseline/candidate CPU CV |
|---|---:|---:|---:|---:|
| Sorted | 1.091432660 | 0.259224086 | 76.249% | 0.293% / 1.489% |
| Reversed | 1.083249873 | 0.434848838 | 59.857% | 0.364% / 1.009% |

Wall medians: sorted1.091702823->0.259290163s (-76.249%);
reversed1.083530224->0.434927288s (-59.860%). Wall CV is also below1.5% in
all arms. Both groups pass the declared>=30% local CPU and<=5% CPU/wall CV gate.
All samples retained; no matrix retries, parameter sweeps or outlier removals.

Each measured arm emits177,807,370 logical serialized bytes over225 snapshots;
series CRC32C1883809848 is identical across all20 samples and both input orders.
Two separate full-series verifications compare complete baseline/candidate bytes
at every snapshot, parse all fields, and confirm input immutability:450/450
snapshots pass. Digest is an additional check, not a substitute for byte equality.
This synthetic fixture always contains10K records; it is not an exact replay of
scope24's growing snapshot sizes/60.33MB total and cannot be a task-time denominator.

Max measured RSS25,649,152 bytes (24.462MiB); minimum observed MemAvailable
30,714,667,008 bytes (28.605GiB). Verification RSS peaks below20MiB. All measured
and verification owned VmSwap0, host pswpout delta0, below256MiB cap. No payload
tree was created and no new network-performance evidence was collected.

## Tests And Integration Decision

Independent oracle initially observed five expected failures with the candidate
stub. Test setup also revealed two invalid fixture assumptions: b: was a forbidden
drive prefix, while the short ID bad was actually valid. Fixtures were corrected
to bb: and bad/id; short valid IDs have explicit compatibility coverage. An early
benchmark build used the wrong CRC update signature; corrected to the existing
crc32cInitialState/Update/Finalize API before tests/measurement.

R1 related CTest14/14 passed:12 TreeManifest/Writer tests, S0 runner ownership
test and new serializer test (8 checks). It covers existing finish barrier,
concurrent writer generations, save failure, timer and atomic save/load behavior.
R2 sync/build/test checks the final fixed commit; it does not add performance runs.
No transfer-program changes or experimental default promotion occurred.

Scope24 reference tree write0.422147s / task1.472798411s is a28.66% optimistic
all-tree-write ceiling. Multiplying by local reductions yields21.86% sorted and
17.16% reversed ceilings. These are generous upper bounds, not forecasts: writing,
snapshot copying/lock acquisition and IO are not all optimized serialization;
writer work overlaps other stages; source/state sizes differ. No>=20% member
batching budget is established. A one-time opt-in runtime A/B is reasonable for
testing whether any of this measured module work lies on the critical path.

Next: [Runtime26](2026-11-09-v3-tree-serialization-runtime-26.md). Move the tested
mechanism into an explicit candidate selector, keep original default, preserve
all bytes and failures, and run a same-binary full-task matrix. No coalescing
threshold/delay retries, thread-pool expansion, microbatch, RDMA or system tuning.
No new Gbps result; large-file30-50/70-85/100G and S1B remain open.

Evidence on R1: /home/Su/experiments/CPNetFlux/2026-11-09-v3-tree-serialization-25.
formal/ contains the only performance matrix. Raw JSON samples, verification,
binary/source hashes, build/tests and resource snapshots are outside Git. Final
commit/GitHub readback and peer build receipt are recorded in closure.json; local
source/evidence mirror is checked against SHA256SUMS.json. Unrelated October
untracked docs are preserved.
