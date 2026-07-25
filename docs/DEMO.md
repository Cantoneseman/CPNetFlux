# GridFlux Alpha Demo Quickstart

This guide is for the alpha operator demo. It uses the GridFTP-like control
server and the GridFlux framed data channel. It does not enable raw FTP
STOR/RETR, GSI, production auth, or any non-default transfer backend. Phase 6C
can optionally wrap the control connection with TLS for alpha demos. Phase 6D
can also wrap STOR/RETR framed file data sockets with opt-in data TLS. LIST/NLST
listing data remains plaintext metadata in Phase 6D.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
```

For the optional io_uring correctness build:

```bash
cmake -S . -B build-io-uring-real -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=g++-13 \
  -DGRIDFLUX_ENABLE_IO_URING=ON
cmake --build build-io-uring-real
```

## Generate Demo Data

```bash
python3 tools/demo/make_demo_dataset.py \
  --output /tmp/gridflux-demo-data \
  --profile tiny \
  --seed 20260518
```

Profiles are deterministic:

- `tiny`: fastest handoff demo.
- `small`: still lightweight, useful for operator walkthroughs.
- `mixed`: larger but still below heavy performance-matrix scale.

The dataset contains `single.bin`, `tree-small/`, and `tree-mixed/`.

## Run The Local Demo

```bash
python3 tools/demo/run_alpha_demo.py \
  --mode local \
  --build-dir build \
  --profile tiny \
  --json-output tools/perf/results/alpha-demo-local.json
```

The local demo starts a loopback `gridflux-gridftp-server` and runs:

- single-file STOR;
- single-file RETR;
- STOR resume;
- RETR resume;
- tree upload;
- tree download;
- tree resume;
- changed-file fail-safe.

The JSON output contains per-case result, elapsed time, bytes, throughput,
source/dest hashes, error, and log paths.

To also capture Phase 6B JSONL events:

```bash
python3 tools/demo/run_alpha_demo.py \
  --mode local \
  --build-dir build \
  --profile tiny \
  --event-log tools/perf/results/alpha-demo-events.jsonl \
  --json-output tools/perf/results/alpha-demo-local.json
```

The demo JSON includes `event_summary`, `error_code_counts`, and `first_error`.
Event logs do not include token or password values.

To run the same local demo with control-plane TLS enabled, let the demo runner
generate a temporary self-signed certificate:

```bash
python3 tools/demo/run_alpha_demo.py \
  --mode local \
  --build-dir build \
  --profile tiny \
  --tls-mode required \
  --event-log tools/perf/results/alpha-demo-tls-events.jsonl \
  --json-output tools/perf/results/alpha-demo-local-tls.json
```

The generated certificate and key live in a temporary work directory and are
not written to release artifacts.

To additionally wrap STOR/RETR framed file data sockets in TLS:

```bash
python3 tools/demo/run_alpha_demo.py \
  --mode local \
  --build-dir build \
  --profile tiny \
  --tls-mode required \
  --data-tls-mode required \
  --event-log tools/perf/results/alpha-demo-data-tls-events.jsonl \
  --json-output tools/perf/results/alpha-demo-local-data-tls.json
```

`--data-tls-mode required` requires `--tls-mode required`. It does not protect
LIST/NLST passive listing data.

## Run The Private Demo

```bash
python3 tools/demo/run_alpha_demo.py \
  --mode private \
  --build-dir build \
  --remote <remote> \
  --remote-root <remote-root> \
  --server-host <server-host> \
  --profile tiny \
  --json-output tools/perf/results/alpha-demo-private.json
```

Private mode reuses the established private smoke helpers. Passwords must come
from environment or the local private operator setup; they are not printed or
written into JSON.

To run the private demo against a token-auth server path, create a private token
file with owner-only permissions and pass it to the demo runner:

```bash
umask 077
printf '%s\n' '<token-value>' > /tmp/gridflux-token.txt

python3 tools/demo/run_alpha_demo.py \
  --mode private \
  --build-dir build \
  --remote <remote> \
  --remote-root <remote-root> \
  --server-host <server-host> \
  --profile tiny \
  --auth-mode token \
  --auth-token-file /tmp/gridflux-token.txt \
  --json-output tools/perf/results/alpha-demo-private-token.json
```

The token value is not written to demo JSON. See [SECURITY.md](SECURITY.md) for
the exact alpha auth boundary.

Private TLS smoke is available through the release gate and the dedicated
helper. It verifies that a remote client can connect to a TLS-required control
server:

```bash
python3 tools/test/run_gridftp_control_tls_private_once.py \
  --remote <remote> \
  --server-host <server-host> \
  --local-build-dir /root/projects/GridFlux/build \
  --output-dir tools/perf/results
```

Private framed data TLS smoke verifies remote STOR/RETR data clients over TLS:

```bash
python3 tools/test/run_gridftp_data_tls_private_once.py \
  --remote <remote> \
  --server-host <server-host> \
  --local-build-dir /root/projects/GridFlux/build \
  --remote-build-dir <remote-root>/build \
  --output-dir tools/perf/results
```

## Manual Server And CLI Walkthrough

Start a server:

```bash
./build/gridflux-gridftp-server \
  --host 127.0.0.1 \
  --port 2121 \
  --root /tmp/gridflux-demo-root \
  --data-port-base 20300 \
  --connections 2 \
  --checksum crc32c \
  --checksum-backend auto
```

Token-auth server:

```bash
umask 077
printf '%s\n' '<token-value>' > /tmp/gridflux-token.txt

./build/gridflux-gridftp-server \
  --host 127.0.0.1 \
  --port 2121 \
  --root /tmp/gridflux-demo-root \
  --data-port-base 20300 \
  --auth-mode token \
  --auth-token-file /tmp/gridflux-token.txt
```

TLS-required control server:

```bash
umask 077
openssl req -x509 -newkey rsa:2048 \
  -keyout /tmp/gridflux-control-key.pem \
  -out /tmp/gridflux-control-cert.pem \
  -sha256 -days 1 -nodes -subj '/CN=localhost'

./build/gridflux-gridftp-server \
  --host 127.0.0.1 \
  --port 2121 \
  --root /tmp/gridflux-demo-root \
  --data-port-base 20300 \
  --tls-mode required \
  --tls-cert-file /tmp/gridflux-control-cert.pem \
  --tls-key-file /tmp/gridflux-control-key.pem
```

Directory upload:

```bash
./build/gridflux-tree-upload-client \
  --host 127.0.0.1 \
  --port 2121 \
  --source-dir /tmp/gridflux-demo-data/tree-mixed \
  --dest-dir demo/tree-mixed \
  --connections 2 \
  --file-parallelism 2 \
  --tls-mode required \
  --tls-ca-file /tmp/gridflux-control-cert.pem \
  --json-summary /tmp/tree-upload-summary.json
```

Directory download:

```bash
./build/gridflux-tree-download-client \
  --host 127.0.0.1 \
  --port 2121 \
  --source-dir demo/tree-mixed \
  --dest-dir /tmp/gridflux-demo-download \
  --connections 2 \
  --file-parallelism 2 \
  --tls-mode required \
  --tls-ca-file /tmp/gridflux-control-cert.pem \
  --json-summary /tmp/tree-download-summary.json
```

Resume drill:

```bash
./build/gridflux-tree-upload-client ... --max-files 1
./build/gridflux-tree-upload-client ... --resume
```

`--max-files` intentionally exits nonzero after committing a bounded number of
files, leaving the tree manifest for resume.

## Troubleshooting

- `530` replies mean the control session was not logged in.
- TLS-required servers close plaintext control clients during handshake. Use
  `--tls-mode required` and `--tls-ca-file <cert>` on GridFlux-aware clients.
- `550` during transfer usually means path validation, changed-file fail-safe,
  checksum failure, or data-channel failure. Check the JSON summary and the
  server/client logs.
- Changed-file failures include the relative path plus manifest/current
  size/mtime. Phase 5D does not auto-overwrite or auto-retransfer changed files.
- For structured troubleshooting, enable `--event-log` and inspect
  [OBSERVABILITY.md](OBSERVABILITY.md) for the JSONL schema and stable error
  codes.
- Empty directories, permissions, owner, xattrs, and ACLs are not preserved.
- STOR/RETR file contents still use the GridFlux framed data channel; stock FTP
  recursive/raw transfer is not supported.

## Alpha Release Candidate

For a complete alpha handoff run, use the Phase 6E release-candidate wrapper:

```bash
GRIDFLUX_SSH_PASSWORD='***' python3 tools/release/run_alpha_release_candidate.py \
  --build-dir build \
  --io-uring-build-dir build-io-uring-real \
  --remote <remote> \
  --remote-root /root/projects/GridFlux \
  --server-host <server-host> \
  --results-dir tools/perf/results
```

The RC wrapper runs the full release gate, then a longer local soak using token
auth, control TLS, and STOR/RETR data TLS. It writes
`docs/release/ALPHA_RELEASE_CANDIDATE.md` plus a machine-readable JSON report.
See [ALPHA_LIMITATIONS.md](release/ALPHA_LIMITATIONS.md) for alpha boundaries.
