# V3 S1: CRC32C Acceleration and DATA Prefix Copy Removal

- Date: 2026-10-29
- Stage: S1 of the active S0-S8 performance route.
- Input commit: d4da9dfa4c850a2b8b89a2e92b0c1edc5df2cda5
- Branch: codex/V3-GRIDFTP-OPTIMIZED-RERUN-01
- Authoritative workspace: Shenzhen /root/projects/CPNetFlux

## Problem

The current CRC32C hardware path uses SSE4.2 CRC instructions. The 2026-10-29
report identifies checksum cost as a candidate bottleneck, so S1 will use
ISA-L's runtime-dispatched CRC32C implementation on supported x86 builds while
retaining a correct fallback.

For CRC32C DATA frames, PersistentDataSession::writeData currently creates a
temporary vector containing the four-byte checksum prefix and a full payload
copy. S1 will send the existing 64-byte header, four-byte prefix, and original
payload as three spans. The complete header already carries rangeId and
attempt; reclaiming header bytes or changing the wire format is out of scope.

## Scope

Pin Intel ISA-L to v2.32.1 commit
7c3479e0a9dac17f448603ec1ad64c7c625f530c and use its iSCSI CRC32C API, which
runtime-dispatches to supported CPU implementations. Preserve the existing
SSE4.2/software fallback when the optimized dependency is not selected.

Send the existing frame header, four-byte CRC prefix, and original payload as
three spans. Plain sockets should submit three iovecs through one sendmsg when
uncontended. TLS should copy only the fixed 68-byte header-plus-prefix into
stack storage, then retain two writes. Preserve byte order, CRC32C semantics,
payload length, verification, and all protocol fields.

## Non-goals

- No wire format, frame/header layout, checksum polynomial, or payload size
  change.
- No TLS mode change, extra TLS write, or raw sendmsg on encrypted sockets.
- No S2 frame enlargement, range parallel write, checkpoint policy, scheduler,
  network, or default behavior change.
- No end-to-end performance claim from syscall counts or a loopback-only test.

## Acceptance

- Add behavioral tests first and observe the missing accelerated CRC and
  three-span path fail before implementation.
- Verify software/ISA-L CRC equivalence for empty, incremental, boundary, and
  large inputs; benchmark realistic 64 KiB and 1 MiB buffers.
- Verify exact DATA wire bytes, the CRC prefix and payload, three-iovec
  sendmsg, short writes across spans, EINTR, zero progress, EAGAIN, and invalid
  spans.
- Run Release build, focused checksum/socket/session tests, and the CTest gate.
- Treat throughput improvement below 5% as no-go; report the measured result
  and retain correctness/copy-count benefits without claiming a speedup.
- Build the same commit on Shanghai and compare key binary hashes before any
  cross-server transfer.
