# V3 Parallel Compression Feature Branch Decision

Date: 2026-10-21
Status: user-authorized experimental feature branch
Decision owner: project user; implemented by current commander only
Task: V3-PARALLEL-COMPRESSION-01
Input baseline: CPNetFlux 390774cad2eaf3b58cb19e0a86ca3d3e02a2699b

## Decision

The CPNetFlux V3 candidate remains frozen and is the accepted rollback/reference baseline. In response to the user's updated direction, hot-path parallel compression will now be integrated and measured in the dedicated branch codex/V3-PARALLEL-COMPRESSION-01. This supersedes the 2026-09-17 research-baseline deferral only for this branch; it does not promote the feature to the candidate or change the main/default branch.

The implementation must use CPNetFlux V3 task/file/range/checksum/resume/finalize semantics. A bounded shared compression executor must enforce process-wide job and memory limits and preserve per-range wire order. Codec support must be capability-negotiated before compressed payloads, with explicit requested/actual mode and no silent downgrade. Compression remains opt-in/experimental until code gates and the lab matrix pass and a later promotion decision is made.

The peer branch is a behavioral reference, not a source dependency. The inspected checkout has no tracked license file, so no peer implementation code is to be copied. All production implementation will be written against CPNetFlux-owned interfaces.

## Validation route

1. Add failing unit/loopback cases before implementation.
2. Build/test CPNetFlux on Shenzhen and pass targeted tests, full serial CTest and applicable release gates.
3. Only then verify both R730/R750 endpoints, disk capacity, actual IP route/interface and server state.
4. Run a bounded raw-versus-parallel-compression matrix on the verified lab path. Native verbs results are separate evidence; CPNetFlux TCP over an RDMA-connected interface is not a native verbs transfer.
5. Keep the prior candidate intact. Any default promotion, package rollout or replacement of formal services requires a separate explicit acceptance stage.

## Rollback

Revert only the feature branch commits. The previously accepted V3 candidate branch/package and its evidence remain untouched. Preserve unrelated dirty files and all user data.

