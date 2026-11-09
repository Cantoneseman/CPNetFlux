# V3 Publication Reconciliation31 Result

Date:2026-11-10. Status: bounded correctness repair accepted; no throughput result.
Input:df239afdb542f165f972865ba443541aa28fd4c7.
Implementation and tested source:a0ae45728cb4223aec45c9a2f970d9001d8f8c01.
GitHub push/readback verified on codex/V3-GRIDFTP-OPTIMIZED-RERUN-01.
Final documentation commit/readback and both build receipts are in closure.json.
R1 remains the sole writer; R2 receives the fixed commit and only builds/tests it.

## Result And Scope

[Audit30](2026-11-10-v3-receiver-transaction-audit-30-result.md) reproduced a real
resume gap: the default non-overwrite link publication can leave the output and
owned temp on the same complete inode while the saved checkpoint is transferring.
FileTransaction::begin previously rejected this state in both directions.

The new repair accepts only CRC32C transferring checkpoints with complete verified
coverage, matching file/source/target/chunk identities and the expected owned temp
name. It opens the regular output without following a symlink, proves descriptor,
output and temp device/inode identity, verifies size/mtime and rereads every chunk
to check its CRC. Equivalent bytes on another inode are insufficient proof.
The checkpoint itself must also be a regular non-symlink file.

After validation it reapplies the requested file/directory sync, atomically saves
committed state using the existing manifest writer, then retires the proven temp
alias. Descriptor/path metadata is rechecked around validation, sync, save and
cleanup. No successful empty resume response is sent for this published state
before reconciliation succeeds. The save retains the existing observation stats.

Save failure leaves the valid transferring state and aliases for retry. Unlink
failure leaves a committed checkpoint and alias; the next committed resume again
verifies ownership/content and retries cleanup. Replacement during the checkpoint
save is rejected without unlinking the retained temp. Existing committed resumes
retain their validation. Upload requires the incoming transfer ID; download keeps
its legitimate validated stored-local ID mapping, including fresh remote resume IDs.

Production changes are confined to src/core/io/persistent_tree_transfer.cpp.
No normal fresh-file payload path, wire format, checkpoint schema, ACK frequency,
runtime default, stage/window setting, RDMA code or system policy changes.

## Verification

The new actual-V3 unit regressions were first built against the old runtime.
Both primary upload/download linked-transferring cases failed with the expected
identity rejection. After the repair, the dedicated Linux executable passes22
parameterized CTest cases, including17 negative fixture variants per direction.
Its rename/unlink/fsync wrappers are linked only into that test executable.

Coverage includes before-link/already-committed controls, corrupted manifest body
and chunk CRC, changed content with restored mtime, incomplete coverage, wrong
identity/paths/chunk size/size/mtime, different inode with identical content,
symlink checkpoint/output/temp, missing temp, failed state, no-checksum rejection,
cancellation, multi-range ownership and download/upload transfer-ID distinctions.
Actual atomic-save failure, alias-unlink failure, requested sync failure and target
replacement during save are injected and followed by recovery/rejection checks.

The new real CLI smoke performs initial upload/download, stops its isolated
loopback server, reconstructs the exact linked-transferring state with independently
computed checkpoint body CRC, then runs resume and repeated resume. Each direction
contains a4096-byte file, a16384-byte nested file and an empty file. Results:

| Check | Upload | Download |
|---|---:|---:|
| Recovered members | 3 | 3 |
| First resume payload bytes | 0 | 0 |
| Repeated resume payload bytes | 0 | 0 |
| Original output inodes/content SHA-256 | preserved | preserved |
| Committed checkpoint body CRC / stored ID | valid / preserved | valid / preserved |
| Owned temp aliases retired | yes | yes |

This is faithful staged-state CLI recovery, not actual SIGKILL or power-loss testing.
It uses127.0.0.1 with explicit local bind, not the inter-node100G data path.

Both RelWithDebInfo builds pass. The final related selection passes **130/130 on
R1 (46.32s) and130/130 on R2 (46.21s)**, including the22 new unit cases, the new
CLI smoke and107 existing session/checkpoint/range/lease/lifetime/directory/smoke
cases. The exact expression is retained in ctest-pattern.txt and was run with
inherited CPNETFLUX_ settings cleared. Reproduction:

```sh
cmake --build build -j8
ctest --test-dir build -R 'PublicationRecovery|cpnetflux_publication_resume_smoke' --output-on-failure
# Full related selection: use ctest-pattern.txt from the evidence directory.
```

The first CLI smoke exposed two test-harness mistakes: receiver-only flags were
passed to the upload client, and stop_server lacked its required log argument.
After correcting the harness, the focused CLI and full selections passed. Its
exact orphan server PID3661906/root was verified, terminated and confirmed gone;
cli-first-failure.log is retained. No production service was stopped.

## Build, Evidence And Cleanup

Evidence:/home/Su/experiments/CPNetFlux/2026-11-10-v3-publication-reconcile-31.
It contains red/green/build/full-test logs, both CLI result sets, source/build
receipts, cleanup snapshots, final closure and hash manifests. Payload is not
checked into Git or copied into the Windows evidence mirror.

The three runtime SHA-256 values match across both nodes at the fixed commit:

| Binary | SHA-256 |
|---|---|
| cpnetflux-gridftp-server | 4b19ebcfceeb4b16eb85fc3372e2248e9d7c2e6a61bd7ed922f48a5e710c232a |
| cpnetflux-tree-download-client | 0fca1fb202343b1d63288cfce62346fcbd75ea3ae2bc0110f220ccb48fc1fb3a |
| cpnetflux-tree-upload-client | 98ae67006f4aa538dbf673713ac7313829484936fc2814cecc15c324a7981749 |

Owned publication temp roots and isolated servers are absent on both nodes.
Production PID/start identities remain R1 126766/1191040 and R2 164834/1679976.
Post-test MemAvailable snapshots are30750892032/30187204608 bytes respectively;
they are snapshots, not continuous test minimums or proof of zero host swap-out
throughout all130 tests. No large payload/performance matrix ran this round.
The two unrelated October14 worker-state documents remain untracked and untouched.

## Limits And Next Work

This repair does not newly promote created/failed or no-checksum transferring
checkpoints. Overwrite/rename publication has a different crash state and remains
outside this hard-link proof. The existing manifest atomic writer is not a new
checkpoint fsync/power-loss guarantee. Repeated identity checks assume the
project's controlled single-writer namespace; they are not an adversarial external
directory-mutation lock. These boundaries prevent claiming all crash states fixed.

Large-file throughput remains the historical approximately27.3Gbps under its
limited tmpfs/TCP contract.30-50/70-85/100Gbps, S1B default promotion and historical
NIC+94 remain open; RDMA and per-file performance attribution remain paused.

The next explicit architecture question is
[TCP Buffer Ownership32](2026-11-10-v3-tcp-buffer-ownership-32.md): can retaining
bounded sender-owned frames until kernel completion eliminate enough ordinary TCP
copy work to justify integration? First establish safe ownership, capabilities and
a bounded diagnostic; require physical-NIC copy/fallback evidence and a conservative
full-task budget before runtime integration. Publication31 itself makes no speed claim.
