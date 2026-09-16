# Phase 3 Hot-Path Lossless Compression

## Scope

Phase 3 adds an opt-in, raw-first zlib representation for framed `DATA`
payloads. The existing 64-byte `FrameHeader`, frame version, manifest format,
`REST GFID`, `verified_chunks`, final verify, and tree completion semantics are
unchanged.

Compression is enabled only when the global tree scheduler has classified an
upload file as `compression_candidate`. `--scheduler off` remains the existing
raw path. Tree download remains raw because Phase 1/2 do not provide a safe
server-side compression decision.

## Wire Contract

Only `DATA` frames may set `kDataCompressed`. The compressed payload is:

```text
8-byte network-order uncompressed length
zlib bytes
```

`stream_id`, `chunk_id`, `offset`, and `total_size` describe the original
logical bytes. `payload_size` describes the wire payload. `ChunkComplete`
length and checksum always describe the original bytes after decompression.
The receiver writes and checksums logical bytes, and records a verified chunk
only after write, flush, and checksum validation succeed.

The codec rejects null, empty, truncated, malformed, length-mismatched, and
output-limit-exceeding payloads. Decode allocation is bounded by the configured
data buffer. Compression output is accepted only when it is smaller than the
raw frame and within the current payload limit.

## Runtime Behavior

For a scheduler upload candidate, each bounded data buffer is sampled by the
existing file executor and compressed independently. There is no whole-file
buffer, unbounded compression queue, or compression worker pool. Poor ratio,
payload-limit overflow, zlib failure, or an explicit raw decision immediately
uses the raw frame.

If a candidate transfer fails after compressed DATA was sent, the tree executor
consumes the failed control reply and performs one resume attempt with the same
GFID and forced raw DATA. A second failure follows the existing file-failed
path. Completion still comes only from the existing per-file and tree manifests.

Direct framed RETR sender options can emit compressed DATA for compatibility
testing and future internal integration. The tree RETR path does not set those
options in this phase.

## Metrics

Global scheduler runs write:

- `scheduler_summary.csv`
- `scheduler_events.jsonl`
- `scheduler_samples.csv`

The summary defines `logical_bytes` as original bytes, `wire_bytes` as actual
DATA payload bytes, `goodput` from logical bytes, wire throughput from wire
bytes, and `compression_ratio_effective` as compressed wire bytes divided by
compressed logical bytes. Phase 3 summary additions are
`compression_attempts`, `compressed_workitems`, `raw_fallback_workitems`,
`compression_failures`, and `decompression_failures`.

The event stream supports `compression_candidate`,
`compression_dispatch`, `compression_reject`, `compression_fallback_raw`,
`compression_failed`, `decompression_failed`,
`compressed_checksum_mismatch`, and `workitem_retry`. Events retain task, file,
link, offset, length, reason, result, and connection context.

Metrics directories must be outside the local tree transfer root. If omitted,
the default is a sibling of the tree manifest path.

## Tests

The focused C++ coverage includes codec round trips and malformed payloads,
network-order length encoding, frame flag validation, raw compatibility,
compressed direct RETR, and a corrupted compressed STOR followed by raw
manifest resume.

The tree scheduler smoke creates deterministic compressible and
incompressible files, validates upload compression and raw fallback, validates
download raw behavior, checks all scheduler sidecars and headers, exercises
`--max-files` plus `--resume`, and compares source/destination tree hashes.

Recommended local verification:

```bash
cmake --build build
./build/cpnetflux_unit_tests \
  --gtest_filter='CompressedDataTest.*:FrameTest.*:HotPathCompressionTest.*:Scheduler*:*TreeTransferOptions*'
python3 tools/test/run_gridftp_tree_scheduler_smoke.py --build-dir build
python3 -m py_compile tools/test/run_gridftp_tree_scheduler_smoke.py
bash -n tools/perf/sync_remote.sh
git diff --check
ctest --test-dir build --output-on-failure
```

## Validation Boundary

Local and Shenzhen validation may establish byte correctness, resume behavior,
raw fallback, metrics explainability, and bounded failure handling. Shanghai
is used only for final synchronization verification. No Phase 3 result is a
100G readiness conclusion, and no daemon, Prometheus/HTTP metrics, data
connection pool, cross-file data-channel reuse, protocol multiplexing,
NIC/NUMA pinning, or lossy compression is introduced.
