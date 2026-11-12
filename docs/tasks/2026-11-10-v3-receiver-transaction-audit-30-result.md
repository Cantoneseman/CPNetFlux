# S2G Receiver Transaction Audit30 Result

Date:2026-11-10. Status: bounded audit complete; per-file performance attribution
paused, no optimization candidate accepted. Input8a781d134b551d36385c4e6c6bd3e506768d2c1f.
No production source, protocol, runtime default or system policy changed.
Final docs commit/GitHub readback and matching builds live in evidence closure.
R1 sole writer; no agents/worktrees. R2 is the exact-commit build peer.

## Decision

The current fresh4KiB x10K contract does not establish a concrete redundant
transaction mechanism with a conservative>=10% complete-task budget. End this
one audit and pause this attribution branch as agreed. Do not add another
profiler, save/window/thread sweep or microbatch implementation automatically.
This does not prove that every future transaction or large-file design is futile.

The audit instead reproduces a specific recovery gap: after the default
non-overwrite hard-link publication, but before the committed manifest save,
both upload and download resume reject a complete, valid transferring checkpoint
even while output and owned temp are the same inode. This is still unfixed.
Next is the separate bounded correctness repair in [Publication31](2026-11-10-v3-publication-reconcile-31.md).
It is not a throughput candidate or authorization to accept arbitrary outputs.

## Source And Evidence Compatibility

Evidence:/home/Su/experiments/CPNetFlux/2026-11-10-v3-receiver-transaction-audit-30.
audit_receiver_transaction.py revalidates the raw Budget28/29 observer gates and
copies their results/analysis/build receipts with hashes. It checks byte equality
of FileTransaction and nine relevant session/checkpoint/storage/profile sources
against Budget28 measured42a2a9d9eca07d8c553691262590fc8b4fd9d26e.
Budget29 sender instrumentation does not change these transaction bodies.

Historical receiver counts and nested service sums are reused only for that
same contract: R2 resident tmpfs -> fresh R1 tmpfs,4KiB x10K, four bound TCP
channels, window2, queued sender/receiver, pwrite/read0/write0/depth2/batch1,
64MiB payload budget, CRC32C/verified_chunks/checkpoint16/commit_sync=none,
original tree serializer, explicit cached summary, phase/detail off.
No new two-node payload or performance samples ran. Historical medians are not
compared across rounds and the local probe is not a throughput benchmark.

## Count And Ownership Ledger

Counts below describe the successful flat-directory fresh4KiB path, not resume,
range work, nested-directory creation, short I/O retries or libc syscall traces.
Line references refer to the input source, whose runtime bodies are unchanged.

| Work | Per member / task | Owner and purpose |
|---|---|---|
| Relative-path validation/root canonicalization/output type check | 1 each /10K each | FileTransaction::begin; safe destination and conflict checks; canonical may issue several syscalls |
| Parent component walk | 0 components for flat files | Same begin; nested paths need their own checks/creation |
| Session create, manifest conflict check, exclusive data temp open, resize | 1 each /10K each | Receiver transaction; exclusive ownership and exact logical size |
| Initial member save | 1 /10K | begin, before payload; transferring with no certified chunk |
| Payload write/chunk record | 1 logical write and1 record /10K each | Writer owns bytes until write completes, then records CRC/coverage; retries can use more syscalls |
| Automatic checkpoint16 save on that record | 0 | One chunk is below16; no hidden per-frame autosave in this fixture |
| Commit coverage/size/mtime checks | 1 commit /10K | Same transaction, after writer join/drain; not a global publication lock |
| Pre-publication member save | 1 /10K | flushManifest persists complete verified coverage while state remains transferring |
| Output publication | 1 link /10K | Non-overwrite branch; overwrite uses rename and has a different crash state |
| Committed member save | 1 /10K | markCommitted after publication; required before successful FILE_RESULT |
| Temp unlink/published metadata snapshot/handle retirement | 1 each /10K each | Owned data alias cleanup and existing summary metadata; failure paths differ |
| Member manifest serialize/open/writeAll/close/rename | 3 each /30K each | Three atomic sidecar saves; writeAll may issue multiple write syscalls |
| Tree state events and background snapshots | 20001 events;203-219 snapshots in profiled Budget28 | Separate shared tree writer; neither one snapshot/file nor the30K member sidecars |

Source anchors: [transaction begin](../../src/core/io/persistent_tree_transfer.cpp#L323),
[record/commit](../../src/core/io/persistent_tree_transfer.cpp#L532),
[session save and record](../../src/core/session/download_session.cpp#L188),
[atomic sidecar save](../../src/checkpoint/download_manifest.cpp#L496).
Budget28 raw profiles independently confirm30000 successful member saves,
zero failures and23490000 serialized member bytes in each profiled task.

The serializer already borrows ordered records via OrderedManifestRecords;
zero/one-record inputs do not allocate a sorted copy. Session save still calls
sortVerifiedChunks, but sorting zero/one items does not create a scalable sort
problem. There are three BufferedFileWriter flush calls per one-frame member
(write entry, chunk boundary, commit); with direct/no buffer they are empty
checks, not three data writes. No redundant payload copy is established here.

## Content Reads And Timing Boundary

For CRC32C/verified_chunks, commit checks certified coverage, size and mtime;
it does not call verifyTempChunks for a normal full reread. verifyPublished
reopens the manifest/output and recomputes content CRC during transport failure
reconciliation. Normal successful DirectoryEnd calls finishTransport(ok), which
returns without that audit. The server's explicit committed-output check is
also on a failed batch-ready recovery path. Do not charge a normal fresh download
for a full verifyPublished pass merely because the function exists.

The39.0625MiB tree is below the128MiB summary CRC-reuse threshold, so summary
uses filesystem_read. Budget28 profiled summary time is165-167ms; Budget29's
six samples are168-211ms. Both are included in their own spawn-to-exit T_ready,
although elapsed_seconds omits summary. There is no proven duplicate normal
audit/summary scan to fuse. The independent experiment SHA/checkpoint checks
occur after process exit and cannot be removed to improve T_ready. Threshold
bypass or reuse of old small-tree CRC-index proposals requires a distinct design.

## Budget Decision

| Historical Budget28 service sum | Measured range | Interpretation |
|---|---:|---|
| All member saves | 449-468ms | Nested sums across four lanes; includes necessary operations |
| All serialization | 122-125ms | Only10.27%-10.50% of T_ready even as an undiscounted sum; cannot all disappear |
| Ordered record preparation | 1.07-1.10ms | 0.090%-0.095% of T_ready; already no sorted copy in this fixture |
| Record formatting plus body CRC | 12.29-12.48ms | 1.03%-1.07%; integrity/format work and overlapping sums |
| Sidecar temp open/write/close | 181-189ms | Atomic replacement work; not removable by changing a serializer |
| Sidecar rename | 142-149ms | Three state transitions; sum is not removable task wall time |

Begin/commit unions22%/24% are whole envelopes containing these costs and other
required work. They do not establish savings from root/path/session caches, and
the prefix/filename/string work lacks a conservative measured budget. Removing
all necessary serialization is already an unrealistic near10% upper diagnostic
envelope before lane overlap. No thread, checkpoint or schema candidate meets
the agreed mechanism plus conservative10% gate. This is budget not established,
not a measured runtime performance rejection or a global performance ceiling.

## Crash Boundary Probe And Known Gap

publish_boundary_probe.cpp links the existing fixed-input core library and stages
valid atomic manifests plus4KiB data, CRC, size and mtime. For both upload and
download it exercises the real V3 receiver/resume response at three boundaries:

| Staged on-disk state | Result |
|---|---|
| Complete verified transferring checkpoint + temp; no output yet | Resume succeeds with no missing payload |
| Same checkpoint, output hard-linked to temp; committed save not done | Resume rejected: committed download identity mismatch; valid aliases remain |
| Committed checkpoint + published output, temp removed | Resume succeeds with no missing payload |

The three-state controls were run once, then repeated under the resource guard;
all six state/direction outcomes reproduce. They are staged crash states, not
actual SIGKILL/power-loss tests. The failure is in FileTransaction::begin's
existing-output requirement for ManifestState::Committed; upload emits the same
download-worded message. Source preflight does not repair this state. No claim
that a full two-node CLI crash test has already been executed follows.

Initial/no-record and complete/transferring checkpoints have different recovery
facts; a crash after link still needs complete CRC/coverage to establish ownership
and data. Moving committed before link would also create a different false-state
window. None of these three saves can simply be deleted under the present
contract. Atomic manifest rename is not a power-loss durability guarantee; the
measured commit_sync=none contract has never promised durable media completion.

## Verification And Delivery

R1 RelWithDebInfo build passes.39 session/checkpoint/persistent-recovery/smoke
CTests pass;13 additional serializer/profile tests pass,52 total. The latter
were run separately after checking actual CTest names: the first regex used
nonmatching typed/profile suite spellings. No failed test is hidden. The probe
intentionally reproduces two rejected recovery states; it does not claim a fix.
Exact regex/logs and source/build hashes are retained outside Git.

Guarded probe: MemAvailable minimum30703357952 bytes, sampled owned VmSwap0,
host pswpout delta0; production PID/start/CPU/RSS/swap snapshots unchanged.
The8.85ms probe yielded only one resource sample; its sampled RSS is not a
reliable instantaneous peak. At most one4KiB data file is staged at a time.
All uniquely owned /tmp probe roots are removed. No isolated service, two-node
payload, RDMA, cache/THP/swap policy or production process was changed.
R1's two unrelated worker-state01b documents remain untouched.

GitHub backup and exact-commit R2 build/configuration/three-binary SHA checks
are recorded in closure.json, along with local mirror hashes. Runtime binaries
must equal the input since this delivery changes documentation only.

## Next Boundary

Publication31 repairs and fault-tests proven publication reconciliation first,
including conflicts, corrupt/missing coverage, symlinks, cancellation and save
failure. Do not broaden this into a journal/microbatch architecture or advertise
speed gains. After correctness, the next architectural question must be selected
explicitly using the existing byte-work/ownership and transaction ledgers; this
audit does not reopen an eliminated candidate or authorize another attribution
loop. Large-file27.3Gbps is unchanged;30-50/70-85/100Gbps, S1B default promotion,
historical NIC+94 and native RDMA remain open.
