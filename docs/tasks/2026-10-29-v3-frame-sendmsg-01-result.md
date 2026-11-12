# V3 frame sendmsg batching result

- Date: 2026-10-29
- Route task: P0-3
- Input commit: `0fba62b81cdc77d8a959d14c940766dca12e004e`
- Branch: `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`
- Implementation commit: `87b389fc34615d1c43ccb36b5064beda2f47d1f6`
- GitHub implementation SHA readback: `87b389fc34615d1c43ccb36b5064beda2f47d1f6`
- Source archive SHA-256, matched on both hosts: `eac263310902ffd48d18bf1cb92c6598ebd5ef0724907275d00504fad47a6ff8`
- Status: implementation, software acceptance, implementation GitHub backup, and same-commit two-host candidate builds complete. Physical-pair throughput and CPU acceptance remains deferred by the user.

## Problem and implementation

The V3 persistent DATA sender used separate writeAll calls for each frame's 64-byte header and payload. FramedDataSocket::writeFrame now submits both spans through two stack iovecs and sendmsg(MSG_NOSIGNAL) for plain sockets. In an uncontended frame this changes two send syscalls to one sendmsg. A full kernel buffer can still require multiple calls; this is not a claim of one call for every frame under every load or of one TCP packet per frame.

The implementation advances through short writes inside either span and across the boundary, retries EINTR, preserves errno in TransportError, and returns on EAGAIN instead of busy retrying. Null nonempty spans and total-length overflow are rejected before any byte is sent. Empty spans require no extra call.

The common persistent metadata writer also uses this API for plain sockets and avoids its concatenation vector. DATA TLS preserves the existing two SSL writes through writeAll. Metadata TLS preserves its existing concatenation and single SSL write. TLS never calls raw sendmsg for encrypted bytes. Frame layout, CRC prefix, checksum strength, wire version, scheduling defaults, and executor IO accounting are unchanged. socket_write_frame_calls still counts logical DATA frames, not kernel syscalls; its scope label names writeFrame.

## Correctness failure found during acceptance

The first full 467-case run had 465 passes, one io_uring capability skip, and one failure in the existing interrupted static-upload checkpoint assertion. That test requires a failed checkpoint immediately after the terminal 550 response, then resumes and verifies the completed file.

Source inspection found a real ordering window: static file transactions remain owned by integrity-check closures in the local transfer context; their destructors save failed checkpoint state. The control handler sent its final response before destroying that context. It now releases the exclusively owned static context before sending the terminal response. Shared dynamic batch contexts retain their lifetime for sibling channels and recovery. The test keeps its immediate assertion and includes checkpoint text in failure output.

A separate 12-case immediate-versus-20-ms diagnostic passed both before and after this fix. It did not deterministically reproduce the initial failure. The evidence establishes the source ordering correction and a passing regression, not that sendmsg introduced this preexisting ordering window. Filesystem failures during the existing destructor's best-effort markFailed remain outside this task's guarantee.

## Verification

- Tests were added first. They initially failed compilation because writeFrame was absent. A temporary two-write implementation then failed the syscall-count assertions (2 send, 0 sendmsg instead of 0 send, 1 sendmsg); both red logs are retained.
- Ten new socketpair tests pass: exact reference bytes and syscall counts; partial writes at 1/63/64/65/320 of 321 bytes plus EINTR; empty spans; invalid input and overflow with no output; zero progress; EAGAIN without retry; partial-header EPIPE; invalid socket; closed peer with default SIGPIPE; real kernel backpressure with a small send buffer and bounded reader.
- Test-only linker wrappers are confined to the Linux socket test executable. Production binaries contain no new syscall interception or fault injection.
- strace independently observed 2 sendto calls from the reference's two writeAll operations and 1 sendmsg from the candidate, with identical delivered bytes.
- Release CMake full build passed, GCC 11.4, -O3 -DNDEBUG, TLS enabled, io_uring disabled. No compiler warnings were found in the successful build logs.
- After the checkpoint-order fix, the focused socket plus interrupted-transfer run passed all 11 cases in 3.80 seconds.
- Final complete CTest: 467 cases in 231.27 seconds, 466 passed, one io_uring capability skip, zero failures. This includes existing TLS, compression, corruption, recovery, resume, and publication-audit coverage. A process-only test token was generated on Shenzhen and was not logged.
- Shanghai built the exact implementation archive and passed the 10 socket tests in 24 ms. Its server and uploader SHA-256 values match Shenzhen's values below.
- Named-file diff whitespace checks passed with core.whitespace=cr-at-eol. Two unrelated untracked 2026-10-14 worker-state documents were preserved and excluded from the commit.

| Binary | Matching SHA-256 on Shenzhen and Shanghai |
|---|---|
| cpnetflux-gridftp-server | `336b2f21f610fa25c9c5dfb3afa95ac163e4ffda735a18014e290c69e25194b3` |
| cpnetflux-tree-upload-client | `9708f90109d008cd7c7e57dcdb121abb6210c8f46adad95ee527df6614611e58` |

## Locations and reproducibility

- Authoritative development and Git: Shenzhen `/root/projects/CPNetFlux`.
- Shenzhen build: `/tmp/cpnetflux-build-fd-release-01`.
- Shanghai exact-commit source: `/tmp/cpnetflux-frame-sendmsg-01-20261029/source`.
- Shanghai candidate build: `/tmp/cpnetflux-frame-sendmsg-01-20261029/build-shanghai`.
- Source archive on both hosts: `/tmp/cpnetflux-frame-sendmsg-01-20261029/source-87b389f.tar.gz`.
- Shenzhen raw evidence: `/tmp/cpnetflux-p0-3-evidence`.
- Shenzhen log index: `log-sha256.txt`, SHA-256 `c3142328cbe59c8c5486cc829749f3f07ad44a9cce74e86e7032156c257236fc`.
- Shanghai build/test evidence: `/tmp/cpnetflux-frame-sendmsg-01-20261029`, log index SHA-256 `ecbdc6fc9450fad58079c7577eb02872cd192942b07165da1920b5ffb63dae3d`.

| Shenzhen evidence file | SHA-256 |
|---|---|
| ctest-full.log (initial checkpoint assertion failure) | `785ee4d940ebfe924fa20f587b86d8888677a5e02bf6ec34b9557fd97456f909` |
| ctest-final.log | `9b80207632dbe051b876fd35ec30cf3a292646c7787545436ff00456acabc1aa` |
| red-count-test.log | `07e62649f18fa39fe701b1b29710f04ce41dbb16aba6534be7a37887446bde91` |
| syscall-count.txt | `55377e52e44098365b4c3c91697540d1be53028a35407308f6860a39b0a8f417` |
| checkpoint-regression.log | `a7cc8febbe4233c1d3a96488c7be28a1083374699d520965d8fa38607cf72692` |

```sh
cmake --build /tmp/cpnetflux-build-fd-release-01 -j2
export CPNETFLUX_TEST_TOKEN=$(openssl rand -hex 24)
ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j2
strace -c -e trace=sendto,sendmsg /tmp/cpnetflux-build-fd-release-01/cpnetflux_framed_socket_tests --gtest_filter=FramedDataSocketTest.BatchesFrameIntoOneSyscallWithIdenticalBytes
git -c core.whitespace=cr-at-eol diff --check
```

Shanghai's project services were not switched; the synchronized candidate is built and identified above. No physical-server performance experiment or CPNetFlux Shenzhen-Shanghai data transfer was started. Source synchronization and loopback/socketpair correctness checks do not constitute throughput acceptance.

## Next task

P0 has three of four implementation items complete; P0-4 remains: remove avoidable CRC-prefix allocations and copies. The supplied route incorrectly assumes eight spare bytes in the current 64-byte header. rangeId occupies bytes 48..55 and attempt occupies bytes 56..63; the header is full. The roadmap now records this correction. First design payload views and scatter/gather around the existing prefix; an eventual header change requires a separate explicit protocol design and tests. The projected throughput/CPU improvements in the route remain hypotheses until physical-pair measurement is available.
