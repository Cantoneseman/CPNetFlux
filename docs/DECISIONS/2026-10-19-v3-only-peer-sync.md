# V3-only runtime and synchronized peer builds

Date: 2026-10-19

## Decision

CPNetFlux runtime behavior is V3-only. The V3 persistent executor owns single-file,
directory, range, resume, scheduler, connection epoch, and recovery behavior. Pre-V3
execution paths are not valid runtime fallbacks.

The Shenzhen repository is authoritative. A code or protocol change is not eligible for
cross-host transfer testing until the same committed revision has been built on both
Shenzhen and Shanghai and the executable identities have been recorded. A failed or
ambiguous version check blocks the transfer test.

## Compatibility boundary

Only options that select V3 behavior are active. Because CPNetFlux has not been deployed,
the current V3 manifest/checkpoint schema is the only supported persisted input; no
pre-V3 manifest migration reader is retained. Numeric schema identifiers describe file
or frame structure and do not select a V1/V2 runtime. No old per-file scheduler, old
control pipeline, or legacy wire execution path can run.

An endpoint that cannot negotiate V3 is an incompatible endpoint for the main route. It
must fail before payload transfer; an automatic downgrade is not a V3 result.

## Scope

This decision covers the CPNetFlux server and clients used for Shenzhen/Shanghai
development transfers. It does not claim physical multi-NIC routing, local source
binding, RDMA, production GSI lifecycle, or high-bandwidth readiness.

## Acceptance

1. Shenzhen and Shanghai use the same source commit or an artifact manifest generated
   from that commit.
2. Both hosts complete the same build configuration and record binary SHA-256.
3. Active option inputs report V3 execution and do not enter an older runtime.
4. V3-to-V3 upload and download pass with matching tree hashes.


## 2026-10-19 clarification: V3-only control negotiation

The V3 persistent execution capability must be explicitly negotiated as `XCPNETFLUX V3` and acknowledged as `CPNetFlux V3`. The old `XCPNETFLUX V2` greeting is not an alias and must be rejected before payload transfer. The `file-transactions/1` suffix identifies the transaction schema revision and does not select a V2 executor. This clarification preserves the existing transaction framing while making runtime selection fail closed to V3.
