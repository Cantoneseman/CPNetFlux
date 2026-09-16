# CPNetFlux Directory Transfer Alpha

Phase 5A adds alpha-grade multi-file directory transfer on top of the existing
single-file GridFTP-like control plane. It does not add raw FTP recursive
transfer: every file still uses the CPNetFlux framed STOR/RETR data channel.

## Commands

Upload a local directory into a server root-relative directory:

```bash
cpnetflux-tree-upload-client \
  --host <server-host> \
  --port <control-port> \
  --source-dir <local-dir> \
  --dest-dir <remote-dir> \
  --connections 2
```

Download a server root-relative directory into a local directory:

```bash
cpnetflux-tree-download-client \
  --host <server-host> \
  --port <control-port> \
  --source-dir <remote-dir> \
  --dest-dir <local-dir> \
  --connections 2
```

Common options include `--file-parallelism`, `--chunk-size`, `--buffer-size`,
`--checksum`, `--checksum-backend`, `--resume`, `--max-files`, `--user`, and
`--password`.

Phase 5C adds opt-in structured summaries:

```bash
cpnetflux-tree-upload-client ... --json-summary /tmp/tree-upload-summary.json
cpnetflux-tree-download-client ... --summary-json /tmp/tree-download-summary.json
```

`--json-summary` is the canonical flag; `--summary-json` is an alias. The JSON
contains direction, source/destination, file counts, completed/skipped/failed/
changed counts, total and transferred bytes, file parallelism, per-file
connections, checksum settings, resume flag, elapsed seconds, throughput,
result, a stable tree verification hash when available, and a structured error
object on failure.

Phase 5D adds an opt-in control reuse mode:

```bash
cpnetflux-tree-upload-client ... --control-reuse off|worker --planner-preset <name>
```

`off` keeps the Alpha RC behavior: each file opens its own control connection.
`worker` keeps one control session per file worker and reuses it across multiple
files. The data channel, framed STOR/RETR protocol, checksum behavior, and
resume semantics stay unchanged. JSON summaries now also record:

- `control_reuse_mode`
- `control_connect_count`
- `control_reconnect_count`
- `data_transfer_count`
- `planner_preset`

Phase 5B makes `--file-parallelism` active: it controls how many files are
processed concurrently. The default remains `1`. Each file worker opens its own
control session and each file still uses `--connections` for per-file framed
data connections; the directory scheduler never reimplements chunk transfer
logic.

## Manifests

Directory transfer adds a file-level manifest:

- Upload: `<source_dir>.cpnetflux.tree.upload.manifest`
- Download: `<dest_dir>.cpnetflux.tree.download.manifest`

The tree manifest records the transfer mode, logical root path, checksum
policy, and one record per regular file: relative path, size, mtime,
transfer_id, status, and error text. It is atomically saved and protected by a
CRC32C body checksum.

Each file still has its existing single-file manifest:

- Upload/STOR uses server-side `<output>.cpnetflux.manifest`.
- Download/RETR uses receiver-side `<output>.cpnetflux.download.manifest`.

## Resume

Use `--resume` to continue a directory transfer. Completed files are skipped
only after validation. Pending or failed files reuse their stored `transfer_id`
and enter the existing `REST GFID:<transfer_id>` per-file resume path.

If the tree manifest is corrupt, resume fails. If a source or destination file
has changed relative to the tree manifest, the file is marked `changed` and the
transfer fails safely before new file tasks are dispatched. Error text includes
the relative path, manifest size/mtime, and current size/mtime. Phase 5B does
not automatically overwrite, delete, or retransfer changed files; a future
`--retransfer-changed` option may be designed separately.

When JSON summary is enabled, changed-file failures also write:

- `error.message`
- `error.changed_path`
- `error.manifest_size`
- `error.manifest_mtime`
- `error.current_size`
- `error.current_mtime`

`--max-files <N>` intentionally stops after scheduling N file transfers and
exits nonzero; it is intended for smoke tests and recovery drills. Already
completed file state remains in the tree manifest for resume.

## Path And Metadata Limits

The scanner only includes regular files. Symlinks, non-regular files, absolute
paths, `..`, Windows drive-style paths, backslashes, and control characters are
rejected. Remote paths are always interpreted relative to the configured
`cpnetflux-gridftp-server --root`.

Phase 5A/5B/5C does not preserve empty directories, permissions, owner/group,
xattrs, ACLs, or directory mtimes. Empty directories are intentionally not
created on the destination; only regular file entries are transferred. It is
not a replacement for production rsync.

## Boundaries

Directory transfer does not change defaults:

- `file_io_backend=posix`
- `final_verify_policy=full`
- `manifest_flush_policy=every_n_chunks`
- `preallocate=off`
- `posix_write_strategy=auto`

It also does not implement raw FTP STOR/RETR streams, TLS/GSI, production
authentication, MLST/MLSD, third-party server-to-server transfer, or Mode E.
