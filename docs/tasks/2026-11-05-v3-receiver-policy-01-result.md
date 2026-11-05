# V3 Receiver Policy 01 Result

Date: 2026-11-05. Input: 330b959d4d30b5e8b63cf049f3a8d61b5fc1bc31.
Status: R770-1 implementation and code gates accepted; peer and performance acceptance pending.

## Implementation

Download CLI now exposes the existing local receiver final-verify, manifest flush policy/interval, commit-sync and range checkpoint override. Defaults remain full, every_n_chunks/16, none, and automatic range selection. The local receiver also receives the selected checksum backend. Upload CLI rejects these receiver flags because its receiver is remote.

The receiver_policy JSON distinguishes local configuration from an unobserved remote receiver. An actual range batch is reported independently of phase timing; whole-file work reports no observed range batch. An explicit range override takes precedence over the existing environment variable. No protocol or server policy negotiation changes.

The truncation regression exposed a pre-existing publication gap: checksum=none skips session readback despite entering the full branch. Both receiver roles now check physical file length after writer flush and complete coverage, before publication. No-checksum JSON accurately reports coverage_only and no readback. This is not a content integrity guarantee. See the verification semantics decision; the historical report's no-checksum readback statement is superseded.

verified_chunks still verifies arriving data and complete chunk coverage, and resumed data is revalidated. It omits full's post-write reread and therefore has different failure coverage. Its availability is an explicit policy choice, not a change to the default or an equivalent-integrity performance claim.

## Code Validation

Evidence root on both hosts: /home/Su/experiments/CPNetFlux/2026-11-05-v3-receiver-policy-01.

- Actual-client red gate rejects the new flag before implementation: acceptance/receiver-policy-red.log.
- Focused accepted unit gate: 47/47, acceptance/receiver-policy-unit-accepted.log. Includes role-scoped parsing/default preservation, bad CRC rejection, corrupt resume checkpoint retransmission under both final policies, and physical truncation rejection for both receiver roles with CRC32C/none.
- Actual-client policy smoke: 7/7 cases, acceptance/receiver-policy-smoke-accepted.log. Includes durability and manifest options, checksum=none semantics, upload unobserved fields, and explicit/automatic range selection without phase timing. Every case independently compares tree content hashes.
- Full R770-1 CTest: 514/514, no skips, 266.74 seconds, acceptance/ctest-r770-1.log. Generated test credentials stay in process; native tests bind 192.168.100.1. Build and git diff --check pass.
- Initial failed smoke and truncation logs are retained as diagnostic evidence. The smoke's no-checksum server fixture was corrected to match the client checksum negotiation. The truncation failure led to the physical-size guard and corrected policy semantics.

## Pending Acceptance

Commit and push the measured source, read back the GitHub SHA, then synchronize that commit to R770-2. Build with matching options/version and verify five binary hashes. Run peer gates before a serial alternating full/verified_chunks single-lane 64 GiB native-RDMA comparison, three repetitions each. Keep every_n_chunks/16, commit_sync=none, CRC32C, no compression/recovery and source/storage/binds fixed. Require data/control terminals, QPs, external SHA after the task timer, route/counters, cleanup and production-service preservation. Record all final measurements and backup receipts here and in the external HANDOFF.md.
