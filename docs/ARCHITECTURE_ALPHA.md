# CPNetFlux Alpha Architecture

CPNetFlux alpha is a GridFTP-like control plane wrapped around a CPNetFlux framed
file transfer engine. The alpha package is designed for demonstrable single-file
and directory dataset movement with resume, checksum, JSON summaries, event
logs, and release validation.

## Control Plane

`cpnetflux-gridftp-server` implements a constrained FTP/GridFTP-like command
subset: `USER/PASS`, `TYPE I`, `EPSV/PASV`, `OPTS PARALLELISM`, `REST GFID`,
`STOR`, `RETR`, `SIZE`, `MDTM`, `CWD/CDUP/PWD`, `LIST`, `NLST`, `FEAT`, `SYST`,
`NOOP`, and `QUIT`. Unsupported commands return `502`.

Passive mode uses a 512-port window anchored at `--data-port-base`; `EPSV`
returns the actual chosen port, and clients connect to that returned port.

Paths are resolved under the configured root. Absolute paths, `..` escapes, and
symlink escapes are rejected.

## Framed STOR/RETR Data Path

File payloads do not use raw FTP streams. STOR and RETR use the CPNetFlux framed
data channel with offset-aware chunks, transfer IDs, complete/error frames, and
connection-level parallelism. `OPTS PARALLELISM` maps to the per-file connection
count.

Phase 6D can wrap STOR/RETR framed file data sockets in opt-in TLS. LIST/NLST
ASCII listing data remains plaintext alpha metadata.

## Resume And Manifests

Upload resume uses the receiver-side file manifest and verified chunk ranges.
Download resume uses the download-side manifest and verified chunk ranges.
Directory transfer adds a tree manifest that records file-level status and
per-file transfer IDs; each file still uses the same chunk-level manifest logic.

`REST GFID:<id>` resumes a known CPNetFlux transfer. Offset REST is intentionally
not used as a fake resume mechanism.

## Integrity

CRC32C is the default checksum policy, with software/hardware/auto backend
selection. `checksum none` remains available for performance comparison but is
not the recommended reliable resume setting. Final verify defaults to `full`.

## Directory Transfer

Tree upload/download clients scan regular files, sort relative paths
deterministically, and schedule per-file STOR/RETR operations. `--file-parallelism`
controls file-level concurrency; `--connections` controls per-file connection
parallelism. Directory transfer does not preserve permissions, owner/group,
xattr, ACL, symlinks, or empty directories.

## Auth And TLS

Anonymous mode is the default. Token auth is opt-in and reads token values only
from local token files. Control-plane TLS is opt-in. STOR/RETR framed file data
TLS is opt-in and requires control TLS. Full GSI, AUTH TLS, production CA
management, and raw FTP TLS compatibility are not implemented.

## Observability And Release Gate

Most alpha executables support opt-in JSONL event logs. Demo, soak, and release
gate tools produce JSON summaries. The alpha release gate and release candidate
tools run builds, tests, demos, smoke tests, hygiene, artifact manifest
freshness, and remote artifact sync/verify.

Public export excludes `AGENTS.md`, build outputs, perf result archives,
secrets, private keys, tokens, and unknown binaries.
