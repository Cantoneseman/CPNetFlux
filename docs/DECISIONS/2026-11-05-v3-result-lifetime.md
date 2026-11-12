# V3 result and commit lifetime

Date: 2026-11-05. Status: accepted for implementation.
Input: 28d22ab7fbd8c3d9ec82c50ef64b0a455bb0f46d.

The traced 64 GiB download receives a successful FILE_RESULT after 172.614 seconds, but the sender rejects it against the payload idle lease. The receiver performs about 150 seconds of full-file verification. A later control FIN does not currently wake the RDMA reader.

Keep the existing 30 second payload idle lease. On a live, zero-inflight InFlight -> CommitPending transition, allow an explicit finite absolute result deadline. The persistent runtime selects min(now + 5 minutes, configured task deadline). Callers without an explicit result deadline retain the current queue behavior. Only this transition may select the result deadline: expired, reclaimed, cancelled, foreign-owner, old-epoch and old-attempt handles cannot extend it. Receiver DATA operations update the same IO lifecycle, and FILE_END enters CommitPending before local verification/publication.

TCP/TLS result reads must use the result budget instead of inheriting the 30 second socket receive timeout. Restore the 30 second receive timeout for a subsequent resume handshake. The lifetime guard remains the absolute limit, including concurrent outstanding results and partial/trickled reads; payload write timeout is unchanged. Validate this with a real 31 second delayed result on a TCP lane, in addition to logical-clock tests.

A per-lane lifetime guard tracks all outstanding result deadlines and the task deadline, shuts down the data socket on expiry, and preserves a deadline error instead of a consequent WC flush. It runs outside payload processing. A local filesystem operation cannot be preempted by socket shutdown; after returning it must check the deadline before reporting success. This is a cooperative storage boundary, not a hard wall-clock limit on an uninterruptible syscall.

The client reads the one persistent-directory control terminal reply concurrently with data execution. Control failure/EOF shuts down that lane's data socket. A successful control terminal does not terminate data processing: DirectoryEnd and file results remain required. There is one reader per control socket; it joins before reconnect or any further command. After data execution returns, a missing terminal is bounded by a 60 second wait, additionally constrained by the task guard. Preserve the data-transport error classification for local lane recovery when the server sends a generic 550 reporting that same disconnect. RDMA CQ event waits also inspect the socket's atomic shutdown flag on each bounded poll slice.

No wire-format change, global idle-timeout increase, late-result acceptance, verification-policy change, scheduler tuning or performance claim is authorized by this decision. Full-file verification and verified_chunks remain separate measurement policies. The previous recommendation to implement result lifetime is replaced by this decision; throughput work remains behind complete-file correctness.
