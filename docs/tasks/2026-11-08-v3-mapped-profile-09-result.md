# V3 Mapped Path Attribution 09 Result

Date: 2026-11-08. Scope: [task](2026-11-08-v3-mapped-profile-09.md).
Input: `4c7d813c078978c37bc4a0e3ee368922bb30216e`.
Observer-control source: `4b2976e094cf936272da281faab57a48a1fe00d8`.
Diagnostic source: `5a9f06a7b14ad10ca1d343fc90b4be53082480cd`.
Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`; version 0.1.0.
Both stages were committed, pushed to Cantoneseman/CPNetFlux and SHA-read back.

## Decision

Attribution and diagnostic-tool work complete. No new runtime performance candidate
is promoted. The explicitly selected four-lane mapped receiver from scope 08 remains
experimental; defaults, CRC, leases, checkpoint and publication are unchanged.
Complete-task performance is still about 28-29 Gbps for this memory workload, below
the 30 Gbps lower midterm threshold. The new 76 Gbps local rewrite observation is
not network or V3 performance. Three-stage expansion and RDMA tuning remain paused.

The next hypothesis should target bounded mapping/page-population cost, with all
preparation included in measured task time. Serial file prefill is not a selected
optimization: its cost exceeds its subsequent local write savings in this control.
Current evidence does not justify another queue, more workers, removing CRC, or a
claim about a particular kernel lock.

## Observer Control

R770-2 -> R770-1, 8 GiB fully resident tmpfs source, one fresh output at a time.
TCP on 192.168.100.2/1, explicit local bind, eno17095np0 counters/routes retained.
Four lanes, eight 1 GiB ranges, inline, mmap, 64 MiB window per range, four active
windows <=256 MiB; read0/write0, depth2, POSIX direct, no compression/preallocation,
CRC32C, verified_chunks, checkpoint/manifest batching16, commit_sync=none, TLS off.
Task process launch through client completion/summary is timed; external SHA follows.
Order: lean/detail, detail/lean, lean/detail. Both ends have the same fixed commit,
configuration, version and eight binary hashes before each transfer.

| Observation | Samples | Median Gbps | CV | Median task seconds | Receiver CPU sec/GiB | Sender CPU sec/GiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Lean | 3 | 28.222448 | 1.150489% | 2.434923 | 0.995000 | 1.223750 |
| Detailed | 3 | 28.047271 | 2.088662% | 2.450131 | 0.997500 | 1.215000 |

Ratio-of-median task overhead for detailed observation is +0.624580%. Three pairs
quantify this batch's observer effect; they are not a universal overhead guarantee
or a five-pair candidate acceptance. Scope 08's five-pair result remains separate.
No V3 runtime source/binary changed between those batches.

All six samples pass exact bytes, CRC, external SHA, V3 terminals, source residency,
zero source physical read_bytes, no recovery, explicit route/interface gates and
zero native QPs. Each mapped task records 128 map/unmap calls, zero map failures,
zero active mapped bytes at exit and exactly 8 GiB mapped writes. Detailed samples
have 139264 DATA frames, frame CRC covers 8 GiB, no logical copy/prefix shift and
no range pwrite. Buffer ownership budgets and output removal are checked.

## Attribution and Its Limits

Values below are medians of per-task sums over eight ranges. Wall intervals nest
or execute on different threads; they must not be added into CPU/task-wall shares.

| Detailed observation | Seconds | Interpretation |
| --- | ---: | --- |
| Receiver lane thread CPU | 7.676012 | Actual CPU, all receiver range work |
| Receiver coordinator write wall sum | 6.285310 | Includes Entry wait, mapped writer, CRC/checkpoint |
| Mapped writer wall sum | 4.723944 | Copy, first touch, mapping lifecycle; no finer kernel attribution |
| Entry lock wait wall sum | 0.965036 | Includes waiting/scheduling, not pure lock CPU |
| Entry lock hold wall sum | 0.534621 | Includes chunk checksum/checkpoint |
| Chunk checksum wall sum | 0.198557 | Nested inside Entry hold |
| Checkpoint manifest flush wall sum | 0.212259 | Nested in coordinator write |
| Receiver frame CRC wall sum | 0.162250 | Nested in receiver checksum work |
| Receiver frame read wall sum | 1.795773 | Socket/header/payload read aggregate |
| Sender lane / producer thread CPU | 5.076626 / 4.633355 | Different threads; process CPU about 9.72 s |
| Sender socket-write wall sum | 7.140541 | Can include receiver backpressure |
| Sender producer / consumer wait wall sums | 4.087738 / 0.193998 | Condition wait through mutex reacquisition |

Across all six tasks, receiver process user/system CPU medians are 1.515/6.420 s;
minor faults range 2111479..2111498, major faults zero. Minor faults are not disk
reads. Producer waits cannot be interpreted entirely as avoidable wakeup overhead.
The dominant measured receiver interval motivates page-population isolation, but
does not prove all writer time is faults or rule out sender/socket CPU limitations.

perf task-clock is actually denied (perf_event_paranoid=4). Installed gprofng was
calibrated against OpenSSL and the project checksum benchmark; both runs warn that
the collection timer changed and sample CPU is far below measured process CPU.
Those samples are rejected as hotspot evidence. No policy/package/system changes.

## Diagnostic Development and Storage Control

`cpnetflux-offset-write-bench` now exports worker-write-interval minor/major faults,
plus separately measured serial rewrite prefill wall/CPU/system CPU/fault counters.
Existing getrusage snapshots delimit worker writes, including map/first touch/unmap,
excluding verification and prefill. Schema3 gains additive fields; existing fields
retain their scope. The bounded runner validates the added fields. The backend
smoke adds rewrite/content coverage, requires real mapped-write faults on nontrivial
files and verifies zero prefill measurements in sparse mode.

The new behavioral assertion fails against the old executable with
`missing write/prefill attribution`, then passes after implementation. Both hosts
complete all-target builds and related CTest 4/4: offset-write smoke, backend smoke,
range observation export and raw-stage analyzer. Existing content, boundary,
descriptor, map/unmap, mmap failure, SIGBUS and parent cleanup checks remain.
No V3 runtime code changes, so previous runtime recovery acceptance is not claimed
as a new test run. Initial ad hoc analysis required fixes for nullable timing fields
and the structured native-QP receipt; final analysis passes without excluding tasks.

At fixed diagnostic commit, R2 receives the commit and builds/tests; eight existing
binary hashes match, plus the changed offset benchmark matches on both hosts:
`d8b7226ed001868c723f9c2b690409b639a908ea7d348a54ef03b1a838bdc21e`.
R2's owned source is removed before the local control. R1 alone runs three alternating
sparse/rewrite pairs, shared inode, four workers, 8 GiB, 65532-byte writes, 1 MiB
chunks, mmap, identical 64 MiB windows; content/resource/cleanup gates retained.

| Local write diagnostic, median | Sparse | Rewrite |
| --- | ---: | ---: |
| Write-only Gbps | 54.719794 | 76.299459 |
| Write-only CV | 0.345779% | 0.262382% |
| Write seconds | 1.255843 | 0.900655 |
| Write worker CPU / system CPU seconds | 4.979898 / 4.699641 | 3.466293 / 2.932553 |
| Write worker minor / major faults | 2097152 / 0 | 2097152 / 0 |
| Separate prefill wall / CPU seconds | 0 / 0 | 1.656798 / 1.656558 |
| Median per-sample prefill + write seconds | 1.255843 | 2.558043 |

Page backing already exists in rewrite, but new mappings still incur per-page minor
faults. This separates file-page population from mapping/PTE establishment. It does
not measure a particular kernel subsystem. Serial prefill reduces subsequent write
time but adds more wall time than it saves. The two-interval total is a diagnostic
comparison, not full V3 readiness or an implementation of pipeline overlap.

## Resources, Cleanup and Evidence

Full-task minimum MemAvailable R1/R2: 20.518879/20.009567 GiB, above 12 GiB guard.
Maximum polled owned RSS: 233.230469/19.542969 MiB; polled HWM max 262.042969 MiB
on R1. Local-control minimum available 20.562347 GiB and max polled RSS 244.281250
MiB. tmpfs backing pages are budgeted separately; mapped RSS references those pages.
Owned swap and host pswpout deltas are zero in all 12 accepted samples. All source,
output, sidecar and local-control payload roots removed; both /dev/shm use zero.
Production PIDs 126766/164834 and October30 start times remain. R1's two unrelated
October14 untracked docs and CPSS(DCC) are preserved. No payload enters Git.

Evidence on R1 (~6 MiB):
`/home/Su/experiments/CPNetFlux/2026-11-08-v3-mapped-profile-09`.
Important receipts: `profile-analysis.json`, `storage-control/analysis.json`,
`storage-control/identity.json`, `cleanup-acceptance-source.json`,
`cleanup-acceptance-final.json`, `script-hashes.json`, per-task verified JSON and
`acceptance/` build/test/matching-build receipts. Profiler calibration is retained
but excluded as CPU attribution evidence. Script SHA-256 values:

| Script | SHA-256 |
| --- | --- |
| profile_harness_09.py | ea46118e28c4af6c2803a32be01a7ec760749af7ccf076776e62673b087e11c8 |
| profile_matrix_09.py | ac993b03d9b3781565112feada7f2cbe4093bca1a40305a3c5ec1a6724edf70a |
| analyze_profile_09.py | cd47fde35ec76230ce5fbe4aea9736b9e229526a5f7e64e2059bfc7f63452c27 |
| storage_control_09.py | adebd0f9ae8bd4e11d7ce54a41385f114e950c9c5b4a57521b521bebdc1696f1 |
| final_audit_09.py | 9a5177b380f12d547ce01b8cf50fcd257cfaec65b885a76593a4051d244de159 |

```sh
cmake --build build -j8
ctest --test-dir build --output-on-failure -R 'offset_write|range_observation_export|raw_stage_analyzer'
# R2 seed then fetch source receipt, before the six-task matrix:
python3 "$EVIDENCE/profile_matrix_09.py" matrix 4b2976e094cf936272da281faab57a48a1fe00d8
python3 "$EVIDENCE/analyze_profile_09.py"
# R2 cleanup-input after matrix; local control at the diagnostic source:
python3 "$EVIDENCE/storage_control_09.py" 5a9f06a7b14ad10ca1d343fc90b4be53082480cd
python3 "$EVIDENCE/final_audit_09.py" source
```

## Next Scope

1. Evaluate one bounded mapped-write candidate that batches writable page-table
   establishment, subject to actual kernel/API support, without changing host
   THP policy. First separate its syscall/page-population/copy CPU and wall cost
   in the local diagnostic; include all preparation and retirement in the interval.
2. Reject it if it merely moves cost outside the timer or has no stable >=10% local
   gain. Preserve 64 MiB windows, memory guard, exact content, ENOMEM/SIGBUS behavior
   and cleanup. No additional workers, global mapping or unbounded preallocation.
3. Only after that gate, define a separate opt-in CLI integration, test cancellation,
   leases, CRC, recovery and publication, then five full-task A/B pairs with >=10%
   median gain, CV<=5%, matching commits/builds and all existing resource gates.
4. If the single candidate fails, close that hypothesis and profile sender read/
   socket CPU independently. TLS-on, durable storage, reverse direction, same-commit
   single-lane, mixed/long workloads, 70-85 and 100 Gbps remain unaccepted.
