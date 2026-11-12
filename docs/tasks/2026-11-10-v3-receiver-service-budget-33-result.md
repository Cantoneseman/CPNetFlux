# V3 Receiver Service Budget33 Result

Date:2026-11-10. Status: bounded audit complete; no accepted performance mechanism.
Input:5664854dc268b151ade7adcd0aabb757e7b5c3c1.
Audit implementation:a7e7aa8f5825aefdc76dce8fdddb3b0f723d654d.
Final documentation commit, GitHub readback and peer receipts:closure.json.
R1 sole writer, R2 fixed-commit build/test peer; no agents/worktrees/desktop.

## Outcome And Scope

No reviewed mechanism establishes a conservative >=10% complete-task benefit.
Pause this large-file receiver software candidate branch without another profiler,
parallelism sweep or unchanged candidate rerun. This is an insufficient-budget
decision, not proof that TCP or CPNetFlux cannot exceed the current throughput.

Added tools/perf/audit_receiver_service_budget.py: a fixed-input, read-only audit
of historical structured receipts, decoded profiles and source spans. It creates
only a new audit result, rejects an existing output and never regenerates old
measurements. Its decision records this scope's reviewed disposition; it is not a
general model for predicting gains from arbitrary future implementations.
No runtime, protocol, CMake, default, CRC, lease, checkpoint or ACK policy changed.
No new performance sample, payload preparation, transfer or profiler was run.

Evidence root:
/home/Su/experiments/CPNetFlux/2026-11-10-v3-receiver-service-budget-33.
audit.json records19 input hashes, totaling3948990 bytes; inputs/ preserves exact
snapshots and inputs-verified.json confirms originals and snapshots still match.

## Rechecked Evidence And Source Limits

All six scope17 full-task acceptance records pass their original integrity/resource,
fresh-output, source disk-read and sampling contracts. Three original receiver
perf.data hashes match their capture receipts. Decoded sample/period totals and
physical CRC Self percentages match; inlined matrix frames are not counted twice,
and an interrupted CRC ancestor is not treated as CRC Self.

| Receiver profile | Samples | CRC combine physical Self | Visible caller for CRC Self |
|---|---:|---:|---|
|01|795|7.0440%|other_or_unwound|
|02|781|7.0423%|other_or_unwound|
|03|787|8.0051%|other_or_unwound|

CRC appears anywhere in59/58/67 stacks, but is the physical leaf in56/55/63.
None of those CRC Self stacks exposes the range or session caller needed to
separate lock-held work from other work. Missing callers do not prove an unlocked
call. These are CPU samples, not wall-time savings or measured lock wait.

Against scope17 commit025c38a99994dfe39bf3444c60faf77e44b1f933, checksum CRC/accumulator,
PersistentDataSession, WorkExecutor and the session header are byte-identical.
DynamicFileQueue startIo/finishIo and RangeFileCoordinator::write spans are also
identical, verified with unique literal boundaries and span hashes. Whole queue,
tree runtime and mapped-window files differ: observations/publication recovery and
diagnostic alignment were added. This does not establish whole-binary equivalence
or a fresh throughput result at the current input.

## Logical Work And Mechanism Review

The1MiB checkpoint chunk and65532-byte DATA contract gives16 full DATA frames plus
one64-byte tail per chunk. Per GiB:1024 chunks,17408 DATA frames,34816 receiver
session-plus-chunk CRC combines and460800 byte-power matrix applications. The
frame CRC scans1GiB of payload once; combining finalized CRCs does not rescan it.
The ownership32 diagnostic has one session accumulator and no shared range/file
publication lifecycle, so its approximately0.94 receiver CPU seconds/GiB cannot
be substituted for the complete-file receiver.

| Mechanism | Source/evidence finding | Budget disposition |
|---|---|---|
| New target page work | scope17 fault paths occupy51.33-55.06% of CPU samples; scope19 cuts minor faults but worker CPU .643919 -> .650689s/GiB, including necessary preparation | No newly removable critical-path work demonstrated; closed population/collapse candidates stay closed |
| Mapped copy/map | scope14 copy wrapper includes faults; map CPU only .000710s/8GiB. Existing direct-receive candidate gained about2.8% | CPU wrapper is not pure memcpy time; do not claim a10% copy-removal budget |
| Chunk CRC under shared lock | RangeFileCoordinator::write releases Entry around independent physical IO, reacquires it and combines the chunk CRC under Entry | Real possible serialization point, but lock-held fraction/overlap and conservative task benefit are unknown; moving it needs a separate lifetime/cancel/retry proof |
| Large frames / cached CRC operators | scope18 reduces write calls17408 ->1024/GiB without reducing fresh-page faults or cold-write CPU; representative varied-leaf chunk combine costs21.092ms/GiB | Primitive cost and hypothetical sums across workers/endpoints do not establish task savings; deferred, not globally disproved |
| Buffer initialization | readNext preserves full-frame size/capacity; repeated full DATA frames do not reallocate/value-initialize. Tail-to-full growth is65468 bytes/chunk, about63.93MiB/GiB in steady state | No repeated whole-payload zeroing bug; no measured10% task budget for the remaining growth |
| Payload ownership | incoming payload is moved into inline processing and moved back, not copied by vector move | No extra vector payload copy to remove |
| Lease / checkpoint / publication | per-frame startIo/finishIo maintain inflight identity/deadline/drain safety; normal verified_chunks completion does not do verifyPublished full reread | No demonstrated removable service budget; do not drop ownership checks or invent an extra file scan |

Required map/fault/CRC/copy/checkpoint services overlap across lanes and endpoints.
Their CPU sums and percentages cannot be added into a complete-task wall budget.
The possible CRC lock bottleneck remains unquantified, rather than being declared
small solely because CRC is less than10% of CPU samples.

## Verification And Reproduction

Audit parser:5 positive controls and9 rejections pass, covering inline/physical
leaf accounting, interrupted ancestors, visible/unknown callers, malformed or
empty samples, missing physical frames, nonpositive periods, sample/period/lost
sample mismatches and wrong CRC Self. Actual accepted receipts then pass.
Relevant CMake builds and96 existing CTests cover checksum, WorkExecutor,
DynamicFileQueue, persistent sessions/ranges/leases and mapped windows:
R1 **96/96,34.68s**; R2 **96/96,34.72s**. Audit controls also pass on R2. Peer logs,
configuration and three runtime hashes are retained with closure.json; no payload
is sent for this audit. The runtime hashes must equal Publication31/Ownership32.

```sh
python3 tools/perf/audit_receiver_service_budget.py --self-test
python3 tools/perf/audit_receiver_service_budget.py --output /home/Su/experiments/CPNetFlux/2026-11-10-v3-receiver-service-budget-33/audit.json
cmake --build build -j8
ctest --test-dir build -R '^(ChecksumTest\.|WorkExecutorTest\.|DynamicFileQueueTest\.|PersistentSessionTest\.|PersistentTreeRangeTest\.|PersistentTreeLeaseTest\.|MappedFileWindowTest\.)' --output-on-failure
```

The audit refuses to overwrite the already recorded audit.json. Inspect that
artifact to reproduce this accepted result; a fresh execution needs a new output
filename under this scope's evidence directory and the unchanged historical inputs.
No failed gate or performance sample was discarded. Final production identities,
absence of audit payload roots, source hashes and local mirror are recorded in
closure.json / the local MIRROR.md. Evidence is outside Git; only source/docs are
GitHub-backed. The two unrelated October14 task documents remain untracked.

## Progress And Next Boundary

The most recent comparable8GiB full-task reference remains27.291Gbps /2.518s,
four TCP lanes, resident tmpfs source, fresh experimental mmap target,
CRC32C/verified_chunks, TLS off, non-durable commit. This round adds no throughput
gain. Intermediate30-50Gbps and later70-85/100Gbps targets remain unpassed;
scope17 rx_missed_errors +94 remains unresolved. RDMA stays paused.

The next independent pending item is S1B stable-resource/default-promotion
prerequisite review. Ownership32's zero-swap guarded1GiB interval is useful new
resource evidence, but does not prove8GiB source preparation safe. A dedicated
future scope must first verify a stable resource window, then allow at most one
bounded attempt under the unchanged zero-host-swap/owned-swap and minimum-memory
gates, with exact cleanup and no automatic retries or relaxed thresholds. This
audit does not start that attempt or change the original summary default.
Small-file/default acceptance is separate from the unmet large-file objective.
Reopening large-file runtime work requires a new removable mechanism and budget;
unknown CRC attribution alone does not authorize another measurement campaign.
