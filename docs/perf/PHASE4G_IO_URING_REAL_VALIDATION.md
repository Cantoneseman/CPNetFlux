# Phase 4G io_uring Real Validation

Date: 2026-05-17

Phase 4G validates the optional file-IO-only `io_uring` backend in a real liburing environment and adds a public-release hygiene/export gate. It does not change the default backend, network epoll, STOR/RETR framed data channel, checksum, manifest, resume, or final verify semantics.

## Scope

- Default file IO backend remains `posix`.
- `io_uring` remains explicit opt-in via `--file-io-backend io_uring` and a build configured with `-DCPNETFLUX_ENABLE_IO_URING=ON`.
- Network IO remains POSIX socket + epoll.
- The io_uring v1 backend is synchronous submit-and-wait regular file IO, not queued/batched async IO.
- Public export must exclude local `AGENTS.md`, build artifacts, private perf results, credentials, and generated large files.

## Environment

| Host | Kernel | OS | Compiler | CMake | liburing |
|------|--------|----|----------|-------|----------|
| machine one | `5.15.0-177-generic` | Ubuntu 22.04.5 LTS | `g++-13 13.4.0` | `3.22.1` | pkg-config `2.0`, `-luring` |
| machine two | `5.15.0-177-generic` | Ubuntu 22.04.5 LTS | `g++-13 13.4.0` | `3.22.1` | pkg-config `2.0`, `-luring` |

Both machines initially lacked liburing. Phase 4G installed `pkg-config` and `liburing-dev` on both machines through apt. No performance conclusion was made until both machines built and tested with real liburing.

## Build And Correctness

Local default build:

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
```

Result: `135/135` passed.

Local real io_uring build:

```text
cmake -S . -B build-io-uring-real -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-13 -DCPNETFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
ctest --test-dir build-io-uring-real --output-on-failure
```

Result: `135/135` passed. `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` ran and passed.

Machine two default build: `135/135` passed.

Machine two real io_uring build: `135/135` passed. `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` ran and passed.

Additional io_uring CLI smoke:

```text
./build-io-uring-real/cpnetflux-storage-bench --path /tmp/cpnetflux-iouring-smoke.bin --mode all --bytes 16777216 --buffer-size 262144 --iterations 1 --preallocate off --file-io-backend io_uring
```

Result: write/read/rewrite all passed with `file_io_backend=io_uring`.

## Public Release Hygiene

Added:

- `.gitignore` entries for local `AGENTS.md`, build outputs, `_deps`, perf results, logs, temp files, environment files, credentials, keys, cookies, tokens, and generated large transfer artifacts.
- `AGENTS.example.md` as a safe public collaboration template.
- `tools/release/check_public_hygiene.py`.
- `tools/release/export_public_repo.py`.

Validation:

```text
python3 tools/release/check_public_hygiene.py --path .
```

Result: failed as expected in the private working tree because local `AGENTS.md` and historical private topology references are present. This is the intended gate behavior.

```text
python3 tools/release/export_public_repo.py --output /tmp/cpnetflux-public
python3 tools/release/check_public_hygiene.py --path /tmp/cpnetflux-public --strict
test ! -f /tmp/cpnetflux-public/AGENTS.md
test -f /tmp/cpnetflux-public/AGENTS.example.md
```

Result: public export passed strict hygiene and did not contain `AGENTS.md`.

## Phase 4G-fix Release Gate

The first Phase 4G release gate was not strict enough: public export could include historical `build-*` directories, and those build outputs could contain private IP strings embedded in binaries. Phase 4G-fix closes that gap.

Fixes:

- `export_public_repo.py` excludes build-like directories at any depth: `build`, `build-*`, `cmake-build-*`, `out`, `dist`, `.cache`, `Testing`, `CMakeFiles`, and `_deps`.
- Export also excludes CMake/Ninja products, object files, libraries, dependency files, ELF executables, logs, and private performance results.
- `check_public_hygiene.py --strict` now fails on build-like directories, CMake/Ninja products, unknown binaries, ELF files, and binary build artifacts instead of skipping them.
- Added `tools/release/test_public_hygiene.py`, which constructs a private fixture containing `AGENTS.md`, build artifacts, and a fake binary with `<redacted>`, then verifies that public export removes them and strict hygiene passes.

Release gate commands:

```text
python3 -m py_compile tools/release/check_public_hygiene.py tools/release/export_public_repo.py tools/release/test_public_hygiene.py
python3 tools/release/test_public_hygiene.py
rm -rf /tmp/cpnetflux-public
python3 tools/release/export_public_repo.py --output /tmp/cpnetflux-public --force
python3 tools/release/check_public_hygiene.py --path /tmp/cpnetflux-public --strict
test ! -f /tmp/cpnetflux-public/AGENTS.md
test -f /tmp/cpnetflux-public/AGENTS.example.md
find /tmp/cpnetflux-public -type d -name 'build*' -print -quit | grep -q . && exit 1 || true
grep -RIn '<redacted-password>\|<public-ip>\|<private-ip>' /tmp/cpnetflux-public && exit 1 || true
```

Result: passed after the stricter export/hygiene rules.

## Artifacts

Storage bench:

- Raw CSV: `tools/perf/results/20260517T083244Z_storage-bench.csv`
- Summary CSV: `tools/perf/results/20260517T083244Z_storage-bench-summary.csv`
- Cases: `320`
- Failures: `0`

GridFTP-like private matrix:

- Raw CSV: `tools/perf/results/20260517T085311Z_gridftp-private-matrix-smoke.csv`
- Summary CSV: `tools/perf/results/20260517T085311Z_gridftp-private-matrix-smoke-summary.csv`
- Cases: `24`
- Failures: `0`

## Storage Bench Median Summary

Representative 1GiB medians:

| side | operation | buffer | preallocate | POSIX Gbps | io_uring Gbps | conclusion |
|------|-----------|--------|-------------|------------|---------------|------------|
| local | write | 256KiB | off | 0.930925 | 0.910293 | POSIX slightly higher |
| local | write | 1MiB | off | 0.928365 | 0.925284 | effectively tied |
| local | write | 256KiB | full | 1.238325 | 1.221720 | POSIX slightly higher |
| local | read | 256KiB | off | 72.126550 | 56.542300 | POSIX higher |
| local | read | 1MiB | off | 78.700300 | 66.775000 | POSIX higher |
| remote | write | 256KiB | off | 0.924404 | 0.922011 | effectively tied |
| remote | write | 1MiB | off | 0.931898 | 0.978580 | io_uring slightly higher |
| remote | write | 256KiB | full | 1.850705 | 1.840910 | effectively tied |
| remote | read | 256KiB | off | 71.389200 | 52.089050 | POSIX higher |
| remote | read | 1MiB | off | 77.221550 | 65.785750 | POSIX higher |

The real io_uring backend is correct and usable, but the synchronous submit-and-wait v1 does not beat POSIX on native read and is mostly tied on write/rewrite. `preallocate=full` still affects write throughput more than backend choice, so it remains a separate opt-in dimension rather than evidence for switching backend.

## GridFTP-like Private Matrix Median Summary

1GiB, 8 connections, chunk size 4MiB, network buffer 256KiB, `preallocate=off`, `final_verify_policy=full`, repeat 3:

| direction | checksum | POSIX median Gbps | io_uring median Gbps | pass/fail | conclusion |
|-----------|----------|-------------------|----------------------|-----------|------------|
| STOR | crc32c hardware | 1.112390 | 1.083810 | 3/0 each | POSIX slightly higher |
| STOR | none | 1.381770 | 1.411140 | 3/0 each | io_uring slightly higher |
| RETR | crc32c hardware | 2.888140 | 3.325980 | 3/0 each | io_uring higher in this sample |
| RETR | none | 4.064710 | 3.224280 | 3/0 each | POSIX higher |

All cases passed sha256 validation. The mixed result is useful but not sufficient to make `io_uring` default. RETR crc32c improved with io_uring in this sample, while native storage read and RETR checksum-none both favored POSIX.

## Decision

- Keep `file_io_backend=posix` as default.
- Keep `io_uring` as explicit opt-in.
- Do not change network epoll.
- Do not change STOR/RETR framed data, checksum, manifest, resume, final verify, or `verified_chunks` defaults.
- Phase 4G validates the real liburing path as correct and comparable, not as a default replacement.

## Phase 4H Recommendation

Proceed to Phase 4H only as a focused optional io_uring experiment if desired:

- keep backend file-IO-only;
- add queue depth / batching for regular file read/write;
- preserve POSIX fallback and default;
- compare against the Phase 4G CSVs with repeat median;
- do not move sockets to io_uring until file IO queue depth has clear evidence.

If Phase 4H is deferred, continue POSIX tuning around storage writeback, preallocation policy per filesystem, and final verify policy evidence.

## Out Of Scope

- No raw FTP STOR/RETR.
- No TLS/GSI, MLST/MLSD, multi-file directory sync, Mode E, SPAS/SPOR, or third-party transfer.
- No default switch to `verified_chunks`, `preallocate=full`, file IO buffer/advice, or io_uring.
