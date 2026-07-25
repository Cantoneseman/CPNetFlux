# Phase 4F io_uring Prototype

Phase 4F implements an optional file-IO-only `io_uring` backend prototype. It does not change network epoll, GridFTP-like control behavior, STOR/RETR framed data, checksum, manifest, resume, or final verify semantics.

## Scope

- Build option: `GRIDFLUX_ENABLE_IO_URING`, default `OFF`.
- Runtime backend option: `--file-io-backend posix|io_uring`, default `posix`.
- io_uring prototype scope:
  - regular file `readAtAll` / `writeAtAll` equivalent semantics;
  - STOR temp write through the shared file IO helper;
  - RETR sender source read through the shared file IO helper;
  - download client temp write through the shared file IO helper;
  - upload client source read through the shared file IO helper.
- Not in scope:
  - socket io_uring;
  - raw FTP STOR/RETR;
  - TLS/GSI, MLST/MLSD, multi-file sync;
  - changing defaults away from POSIX;
  - changing manifest, verified_chunks, checksum, resume, or final verify rules.

## liburing Probe

Current local and machine two environment:

| Host | Kernel | pkg-config liburing | liburing.h | liburing dynamic library | Result |
|------|--------|---------------------|------------|--------------------------|--------|
| machine one | `5.15.0-177-generic` | not found | not found | not found | fallback stub |
| machine two | `5.15.0-177-generic` | not found | not found | not found | fallback stub |

Because liburing is unavailable on both machines, Phase 4F validation focuses on:

- default POSIX build and CTest remain green;
- `GRIDFLUX_ENABLE_IO_URING=ON` configures and builds without failing;
- explicit `--file-io-backend io_uring` returns a clear unavailable error.

## Build Behavior

- `GRIDFLUX_ENABLE_IO_URING=OFF`:
  - does not search for or link liburing;
  - compiles `src/storage/file_io_uring_stub.cpp`;
  - defines `GRIDFLUX_HAS_IO_URING=0`.
- `GRIDFLUX_ENABLE_IO_URING=ON` and liburing found:
  - compiles `src/storage/file_io_uring.cpp`;
  - links `uring`;
  - defines `GRIDFLUX_HAS_IO_URING=1`.
- `GRIDFLUX_ENABLE_IO_URING=ON` and liburing missing:
  - emits a CMake warning;
  - compiles the unavailable stub;
  - keeps default POSIX build/test usable.

## Validation

Local default build:

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

Result: `133/133` passed. `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` was skipped because `GRIDFLUX_HAS_IO_URING=0`.

Local fallback probe:

```text
cmake -S . -B build-iouring-probe -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13 -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-iouring-probe
ctest --test-dir build-iouring-probe --output-on-failure
```

Result: CMake warned that liburing was not found, then build and `133/133` CTest passed.

Explicit unavailable backend probe:

```text
./build-iouring-probe/gridflux-storage-bench --path /tmp/gridflux-iouring-unavailable.bin --mode write --bytes 1048576 --buffer-size 65536 --iterations 1 --preallocate off --file-io-backend io_uring
```

Result: nonzero exit with `result=fail` and `error=file IO backend unavailable: io_uring`.

Wrapper scan probe:

```text
python3 tools/benchmark/run_storage_bench.py --side local --build-dir build --bytes 1048576 --modes write --preallocates off --file-io-advices off --file-io-backends posix,io_uring --buffer-sizes 65536 --iterations 1 --output-dir tools/perf/results
```

Result: POSIX rows passed, explicit io_uring rows failed and were written to CSV. CSV:

- `tools/perf/results/20260517T075109Z_storage-bench.csv`
- `tools/perf/results/20260517T075109Z_storage-bench-summary.csv`

Remote validation:

- machine two default build and full CTest were run after rsync;
- machine two has no liburing, so no posix/io_uring 1GiB performance comparison was run.

## Recommendation

Phase 4F is complete as a prototype/fallback milestone. It establishes the build switch, runtime backend selection, unavailable stub behavior, tests, and script dimensions needed for future comparison.

Do not make `io_uring` default yet. The next step is Phase 4G only if liburing can be installed or otherwise made available on both machines. Phase 4G should compare POSIX and io_uring using storage bench plus GridFTP-like STOR/RETR private matrix, while keeping:

- `file_io_backend=posix` default;
- network epoll unchanged;
- checksum/manifest/resume/final verify semantics unchanged.
