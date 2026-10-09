# DIR-INTEGRATION-01 / R2026-10-09.2

Goal: integrate the accepted directory control pool/pending pipeline, timing and short
comparison entry points into the authoritative Shenzhen root, retaining validated
persistent data-session commit 53a6dfb. One serial writer; no additional worktree.

Inputs: root d5da3199c238247a87ad6da01701676107524399;
accepted pipeline 00be35ab96f95ae499f705d0d5b62ba86ec18469;
shared base 47b0ca2050288f4a0f2efec846535c5d130b3188.

Non-goals: no WAN experiment, no service restart, no default feature enablement, no
new performance design, no worktree deletion, no unrelated dirty-file staging.

Allowed: semantic source/test/tool changes between base and accepted pipeline under
src/, include/, tests/, tools/experiments/gridftp_compare/, tools/test/ and CMakeLists.txt;
exclude obsolete Windows manual-runner copies. Resolve overlap with persistent V2
without changing its wire protocol. This task and its result record are also allowed.

Acceptance: Release build from integrated root and then a clean committed archive;
unit tests, directory regressions including V2 reuse and pipeline sidecar/fault tests;
Python comparison/schema tests and shell syntax; real loopback summaries prove
pipeline slots/pending and V2 single-data-connection paths. Feature options explicit;
no silent bypass when both independent optimizations requested. Record exact
commit, binary hashes and origin remote SHA after pushing to Cantoneseman/CPNetFlux.

Preserve existing root dirty files; snapshot affected bytes externally before editing.
No claim of 100 Mbps or GridFTP performance acceptance follows from these tests.

Integration-specific tests may also strengthen the existing V2 smoke JSON parsing and
option-conflict checks; these guard against feature bypass and duplicate timing keys.

Integration-specific tests may also strengthen the existing V2 smoke JSON parsing and
option-conflict checks; these guard against feature bypass and duplicate timing keys.

Integration-specific tests may also strengthen the existing V2 smoke JSON parsing and
option-conflict checks; these guard against feature bypass and duplicate timing keys.

Integration-specific tests may also strengthen the existing V2 smoke JSON parsing and
option-conflict checks; these guard against feature bypass and duplicate timing keys.

Root full CTest exposed ScannerKeepsUserFilesAndExcludesOwnedCheckpointAndPartial:
V1 filtering hid the manifest before V2 could associate its temp file. The allowed
merge fix makes raw sidecar inclusion explicit for persistent_tree_transfer.cpp,
retaining default V1 filtering and V2 owner validation. The existing failing test
is the regression gate; do not weaken its assertion.
