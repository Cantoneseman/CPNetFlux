# V3 Memory Attribution 04

Date: 2026-11-08
Input: 12bc1ee5d834b6a465ca0135ae167058cc35dba5
Status: scope fixed; measurement implementation in progress.

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
