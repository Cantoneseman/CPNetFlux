# V3 Dynamic Download Local Executor

Date:2026-11-09. Status:implementation approved within existing V3-only mandate.
Input:8f42c8513d0169bbb30a4f4036c2bb868f7f759c.

Receiver CPU instrumentation exposed that dynamic directory download with global
scheduler off passes no ready queue into receivePersistentTree. The conditional
requires globalScheduler,whereas static download already creates incoming work
independently. Actual eight-range transfers report local_work_executor=false and
zero local lease start/finish calls. The sender still owns V3 leases and data
integrity/terminals passed;do not reinterpret this as a demonstrated corruption or
claim that allV3 execution was absent. The local receiver path is incomplete.

Keep V3 data protocol and default scheduler off. Allocate the same bounded
incoming DynamicFileQueue used by the existing scheduler-on dynamic branch for
every dynamic download. Only attach a GlobalScheduler when one exists. Existing
owner/epoch/attempt,deadline,drain,recovery and completion logic must be reused;
never bypass the newly reachable validation to retain an old performance figure.

Regression must exercise real dynamic upload/download with scheduler off and
prove receiver local executor,one start/finish perwritten DATA,content correctness
and thread-CPU ownership. Related interrupted/service-restart/timeout resume and
lease regressions remain required. Same-commit two-host builds and digest check
precede renewed performance measurements.

Old28-29Gbps experiments retain their configuration-specific payload evidence.
They do not prove the repaired local receiver path's performance or readiness.
Stop the original scope13 observer matrix and remeasure a distinct fixed-source
matrix after correction. This decision does not promote sender inline,mmap as a
default,RDMA or additional worker/queue architecture.
