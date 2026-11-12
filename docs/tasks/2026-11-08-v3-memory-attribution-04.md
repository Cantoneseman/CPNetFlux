# V3 Memory Attribution 04

Date: 2026-11-08
Input: 12bc1ee5d834b6a465ca0135ae167058cc35dba5
Status: attribution complete; stability gate not passed; no production candidate promoted.

## Scope

Add a diagnostic benchmark comparing raw TCP, the actual V3 persistent frame/session
code without CRC, and that same code with CRC32C. All modes use one blocking worker
per connection, explicit local bind, bounded reusable memory, identical measurement
boundaries, and a receiver completion acknowledgement. The V3 modes exercise
FILE_BEGIN/DATA/FILE_END/FILE_RESULT and session validation, without filesystem IO,
manifest, work scheduling, compression, or production readiness claims.

Affected files: CMakeLists.txt; tools/perf/tcp_frame_bench.cpp;
tools/test/run_cpnetflux_tcp_frame_bench_smoke.py; this task and docs/ROADMAP.md.
The reproducible serial experiment driver is tools/perf/run_tcp_frame_memory.py.

## Non-goals

Do not change the V3 runtime defaults, wire format, production services, RDMA,
pipeline/writer/queue concurrency, kernel settings, CPU affinity or socket buffers.
Do not reinterpret the closed threaded sink candidate as accepted. No payload
files are created, including no 64 GiB files. Repeated logical bytes use at most
two 64 KiB buffers per worker plus fixed session metadata.

## Acceptance

1. Build and focused CTest on R770-1, then commit/push and sync the fixed commit to
   R770-2; compare benchmark SHA-256 and CMake configuration before network tests.
2. Smoke covers raw and both V3 checksum modes, 1/4 connections, uneven byte
   partitions, CRC corruption rejection and invalid arguments. Each peer reports
   exact bytes, receiver acknowledgement, bounded frame count and memory.
3. TCP uses 192.168.100.2 -> 192.168.100.1 with explicit binds. Keep routes and
   eno17095np0 counters, per-process CPU/context switches/RSS/owned swap, host
   MemAvailable >=12 GiB and host pswpout delta zero.
4. Screening uses 1/4 connections. Stable comparison uses five interleaved
   repetitions of raw/V3-none/V3-crc32c at four connections, streaming 128 GiB
   from reusable memory (not allocated or stored). Report both peers' timings,
   median, CV, CPU seconds/GiB and min/max; CV <=5% qualifies a stable group.
5. Separate strace diagnostic samples from throughput samples. If perf is denied,
   record the limitation without changing system policy.
6. A production optimization is allowed only after the measurements identify a
   distinct cause; one candidate at most in this scope, >=10% median improvement,
   CV <=5%, and all correctness/resource gates. Otherwise finish attribution,
   keep production unchanged and hand off a concrete next hypothesis.

## Evidence

/home/Su/experiments/CPNetFlux/2026-11-08-v3-memory-attribution-04

Only compact metrics, commands and logs are retained. Terminate only owned
benchmark processes; preserve existing services and unrelated dirty files.

## Results

Measurement commit: d660b8cdd48d17e10e1a6a2eba6c1dece8c0d28a.
Both nodes used RelWithDebInfo with ISA-L/TLS/RDMA/io_uring enabled; benchmark
SHA-256 was 932f4637f4232f165fcf5cb75f87aa65d1de4fda7511b605d7c1f91d4d9e628d.
Commands: `python3 -u tools/perf/run_tcp_frame_memory.py --connections 4 --repetitions 5`,
then the same command with `--connections 1 --repetitions 1` for screening only.

| Four connections; 5 samples | Observed median Gbps | Range Gbps | CV | CPU sec/GiB, both ends |
| --- | ---: | ---: | ---: | ---: |
| raw TCP, no CRC | 87.3917 | 77.5821-91.0342 | 5.844% | 0.5190 |
| V3 frames/session, no CRC | 88.0136 | 73.6418-90.5947 | 7.348% | 0.5412 |
| V3 frames/session, CRC32C | 88.5138 | 75.7629-91.1970 | 6.281% | 0.5695 |

No group passes CV <=5%. These are observed diagnostic medians, not accepted
stable throughput or evidence that CRC speeds up transport. The CRC median is
within the runtime variation. CPU includes each process's setup as well as payload.

Single-connection screening (one sample each, not a stability result): raw 38.2129,
V3-none 38.6730, V3-CRC32C 35.7179 Gbps. Four-lane memory transport scales relative
to these screening samples; its behavior differs from the historical full-file
1-vs-4-lane regression. This narrows the next investigation, without proving an
individual filesystem or scheduler cause.

All 18 uninstrumented samples returned rc=0 on both ends, exact 128 GiB logical
bytes and receiver acknowledgement. For the 15 four-connection samples, minimum
MemAvailable was 28.642 GiB on R1 and 28.073 GiB on R2; maximum polled owned RSS
was 8.06/7.90 MiB. Owned swap and host pswpout deltas were zero; interface RX/TX
deltas covered payload and routes used eno17095np0. Lane start spread was <=0.371 ms.
Each four-connection run lasted 12.06-14.93 seconds. No payload file was created.
Raw mode checks exact bytes/acknowledgement, not content integrity; V3 CRC mode
checks DATA and FILE_END CRC through the real session code. This diagnostic has
no filesystem durability, real work lease, control-plane TLS or production service.

Three separate 1 GiB strace samples (not throughput samples) also passed byte and
resource gates. R1 recvfrom calls: raw 16,451; V3-none 32,875; V3-CRC 32,812. The
V3-CRC sender issued 16,396 sendmsg calls. This matches one frame write and roughly
two reads per frame, and does not identify framing as the cause of the historical
approximately 15 Gbps file task. perf is unavailable with perf_event_paranoid=4;
system policy was not changed. Traced timings are excluded from the table.

## Verification and Handoff

- Both nodes: `cmake --build build -j8 --target cpnetflux-tcp-frame-bench`.
- Both nodes: CTest filter `FramedDataSocketTest|PersistentSessionTest|PersistentDataSessionTest|cpnetflux_tcp_frame_bench_smoke`, 48/48 passed.
- New smoke covers 12 success shapes, actual DATA corruption rejection, and invalid arguments.
- Measurement labels explicitly distinguish raw payload chunks from actual V3 DATA frames.
  Single-sample CV is unknown, not zero; the driver refuses to overwrite completed
  samples and enforces interface counter/route gates in addition to recording them.
- The production services remain PID 126766/164834; the closed pipeline/threaded
  candidates and all defaults remain unchanged.

No production optimization is promoted. The same code's in-memory V3 DATA/session
path reached much higher rates than the previous full-file tasks, so changing
frame size or weakening CRC is not justified by these measurements. Next scope:
same-commit 8 GiB resident tmpfs source, full V3 1/4 lanes, low-overhead range
observations plus separately traced small samples; isolate queue ownership/wakeup,
shared range/checkpoint work and publication without expanding concurrency. Keep
CRC, verified coverage, resume, leases, atomic publish and explicit binds. Only a
distinct measured cause may justify a simplifying fast path candidate.
