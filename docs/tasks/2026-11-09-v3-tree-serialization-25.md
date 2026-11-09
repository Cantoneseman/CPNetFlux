# S2B Tree Serialization25

Date:2026-11-09. Status: local feasibility accepted; runtime integration pending.
See [result](2026-11-09-v3-tree-serialization-25-result.md).
Input: cf73a90ac386e4a0837980ca3b2012a33d133a4e, pushed Fixed Work24 result.

## Hypothesis And Boundary

Fixed Work24 found225 full-tree snapshots/60.33MB in a1.473s small-file reference;
profile identifies serializeTreeManifest work but has17.9% observation overhead.
Hypothesis: tree record copy/sort, stream numeric formatting and temporary hex
allocation can be reduced while keeping identical bytes. This differs from the
closed event-threshold/max-delay candidates. No member batch/thread pool, wire,
manifest schema, snapshot frequency, recovery/atomic publication or default
selector changes. Keep S1B default promotion held and RDMA paused.

R1 single writer, no agents/worktrees; inspect src/core/tree/tree_manifest.cpp,
tests and prior checkpoint serializer pattern. Planned exact files: task/result,
tools/perf/tree_manifest_serialization_bench.cpp, tools/test tree serializer
reference/smoke tests and CMakeLists.txt. Feasibility candidate initially lives
only in the diagnostic target; do not change default runtime serialization before
recording its local result and integration decision. Reuse public manifest types,
validation/parser/CRC APIs rather than invent a new schema or persist fake bytes.

Exact files: this task/result, tools/perf/tree_manifest_serialization_bench.cpp,
tools/perf/run_tree_manifest_serialization.py,
tools/test/run_tree_manifest_serialization_test.py, CMakeLists.txt,
docs/ROADMAP.md and docs/README.md. No runtime source/header edits.
Declare five interleaved baseline/candidate pairs for10K records/225 evolving
snapshots, sorted and reversed order separately (20 process runs total).
Each includes fixture preparation, validation, encoding/CRC and destruction.
Full-series byte/parse equality checks run separately outside measured intervals;
small independent Python byte oracle includes an independent bitwise CRC32C.
Runner checks fixed pushed commit, binary/fixture digest, MemAvailable>=12GiB,
owned VmSwap0, pswpout delta0 and owned RSS<=256MiB; no payload files/network.

Candidate: borrow already ordered input; use a private sorted view for arbitrary
order without mutation; reserve one output buffer, append decimal with to_chars
and hex directly. Preserve all validation and deterministic bytes, including
empty/shuffled/special path/error bytes, status transitions, signed timestamps,
max numeric values, duplicate-record behavior and malformed-input rejection.
No caching that silently assumes mutable metadata is immutable. Header/body CRC
and complete old-format output are included, not just the easy formatting loop.

## Local Gate

First add an independent legacy byte oracle and observe candidate checks fail.
One fixed matched10K-record evolving-state fixture and bounded smaller/shuffled
cases, <=256MiB owned memory/output, no network payload or system changes. Include
baseline/candidate preparation, validation, ordering, encoding/CRC and destruction
in wall/CPU; report stages only if useful, no overlap summation. Snapshot cadence
and data identical, three to five interleaved pairs declared before execution,
CPU and wall CV<=5% per arm, >=30% local CPU reduction and full-byte/parse equality required.
Stop once the declared matrix completes; no parameter/CV chasing.

A local pass proves module feasibility only. Estimate an optimistic *upper bound*
from reference0.422s tree-write/1.473s T_ready, explicitly allowing overlap and
copy/lock/IO work outside the optimized function. If the bound cannot support a
credible whole-task gain, stop before integration. Never claim a>=20% microbatch
budget from local serialization results. Any runtime proposal must preserve the
finish barrier/save-failure/crash-resume tests and pass a separately frozen
full-task A/B; small-file membership batching still requires>=20% wall gain and
its independent correctness/recovery/large-file-regression gates.

## Delivery

Record commands, input/candidate commits, fixture/binary SHA, byte/CPU/variance
results, acceptance/no-go decision and exact cleanup. Build/test R1, commit/push
and read back; R2 sync/build/test only if needed for runtime acceptance, no dirty
tree transfers. Update current route with the actual decision, not predicted gain.

Measured source a173124cb17a37fd62207d0d050243ca8c9870ac, pushed/read back.
All20 samples and450 full snapshot equality/parse checks passed resource/content
gates. CPU medians: sorted1.091433->0.259224s (-76.249%); reversed
1.083250->0.434849s (-59.857%). Max arm CPU/wall CV1.489%; local gate passes.
This is a synthetic10K-record full-vector fixture, not a trace replay of scope24's
growing snapshot sizes. No file transfer, runtime source/default or schema changed.
Integration decision: one opt-in same-binary full-task experiment in new scope26;
the21.86%/17.16% optimistic ceilings do not predict actual task gains.
