# CPNetFlux Security Alpha

Phase 6A adds opt-in control-plane token authentication. Phase 6C adds
opt-in control-plane TLS. Phase 6D adds opt-in TLS for the STOR/RETR framed
file data channel. These are alpha operator features, not production security.
LIST/NLST passive ASCII listing data remains plaintext in Phase 6D. GSI/DCAU/
PROT, AUTH TLS, production CA management, and raw FTP TLS compatibility are not
implemented.

## Modes

Default mode remains anonymous:

```bash
./build/cpnetflux-gridftp-server \
  --root /tmp/cpnetflux-root \
  --host 127.0.0.1 \
  --port 2121 \
  --auth-mode anonymous
```

Anonymous mode preserves the existing `USER cpnetflux` / `PASS cpnetflux`
placeholder behavior for demos and compatibility tests.

Token mode is explicit:

```bash
umask 077
printf '%s\n' '<token-value>' > /tmp/cpnetflux-token.txt

./build/cpnetflux-gridftp-server \
  --root /tmp/cpnetflux-root \
  --host 127.0.0.1 \
  --port 2121 \
  --auth-mode token \
  --auth-token-file /tmp/cpnetflux-token.txt
```

The token is read once at startup. Token files must be regular files, non-empty,
and must not be group/world accessible. Token values are not accepted directly
on the command line.

## Client Login

In token mode a CPNetFlux-aware control client logs in with:

```text
USER token
PASS <token-value>
```

Protected commands return `530` until authentication succeeds. Protected
commands include `TYPE`, `EPSV`, `PASV`, `OPTS`, `REST`, `STOR`, `RETR`,
`SIZE`, `MDTM`, `CWD`, `CDUP`, `LIST`, and `NLST`.

`FEAT`, `SYST`, `NOOP`, `QUIT`, `USER`, and `PASS` remain available before
login so clients can discover capabilities and complete authentication.

## Control-Plane TLS Alpha

TLS is opt-in and disabled by default:

```bash
./build/cpnetflux-gridftp-server \
  --root /tmp/cpnetflux-root \
  --host 127.0.0.1 \
  --port 2121 \
  --tls-mode off
```

Required TLS mode starts the control listener with an immediate TLS handshake:

```bash
umask 077
openssl req -x509 -newkey rsa:2048 \
  -keyout /tmp/cpnetflux-control-key.pem \
  -out /tmp/cpnetflux-control-cert.pem \
  -sha256 -days 1 -nodes -subj '/CN=localhost'

./build/cpnetflux-gridftp-server \
  --root /tmp/cpnetflux-root \
  --host 127.0.0.1 \
  --port 2121 \
  --tls-mode required \
  --tls-cert-file /tmp/cpnetflux-control-cert.pem \
  --tls-key-file /tmp/cpnetflux-control-key.pem
```

`--tls-mode required` requires `--tls-cert-file` and `--tls-key-file`.
Private key files must be regular files and must not be group/world accessible.
If OpenSSL development/runtime support is unavailable in the build, explicit
TLS use fails with a clear configuration error while `--tls-mode off` remains
usable. `--tls-mode explicit` is reserved for a future AUTH TLS design and is
rejected in Phase 6C.

Tree clients can connect to a TLS-required control server with:

```bash
./build/cpnetflux-tree-upload-client \
  --host 127.0.0.1 \
  --port 2121 \
  --source-dir /tmp/demo-tree \
  --dest-dir demo-tree \
  --tls-mode required \
  --tls-ca-file /tmp/cpnetflux-control-cert.pem
```

Token auth and TLS can be combined: TLS is established first, then the existing
`USER token` / `PASS <token-value>` control login is performed.

## STOR/RETR Data-Channel TLS Alpha

Phase 6D can wrap framed STOR/RETR file data sockets in TLS:

```bash
./build/cpnetflux-gridftp-server \
  --root /tmp/cpnetflux-root \
  --host 127.0.0.1 \
  --port 2121 \
  --tls-mode required \
  --tls-cert-file /tmp/cpnetflux-control-cert.pem \
  --tls-key-file /tmp/cpnetflux-control-key.pem \
  --data-tls-mode required
```

Clients opt in per framed data connection:

```bash
./build/cpnetflux-file-client \
  --host 127.0.0.1 \
  --port <epsv-data-port> \
  --input /tmp/source.bin \
  --transfer-id <transfer-id> \
  --data-tls-mode required \
  --tls-ca-file /tmp/cpnetflux-control-cert.pem
```

`--data-tls-mode required` is valid on `cpnetflux-gridftp-server` only when
`--tls-mode required` is also enabled. It reuses the control-plane certificate
and key for the passive STOR/RETR data sockets. The TLS wrapper is socket-only:
CPNetFlux frame layout, CRC32C checksums, manifest/verified_chunks, resume, and
final verify semantics are unchanged.

Directory upload/download clients pass the same setting through to each
per-file STOR/RETR operation:

```bash
./build/cpnetflux-tree-upload-client \
  --host 127.0.0.1 \
  --port 2121 \
  --source-dir /tmp/demo-tree \
  --dest-dir demo-tree \
  --tls-mode required \
  --tls-ca-file /tmp/cpnetflux-control-cert.pem \
  --data-tls-mode required
```

Important Phase 6D limit: `LIST` and `NLST` are not data-TLS protected. The
commands themselves can be protected by control-plane TLS, but the passive ASCII
listing data connection remains the existing plaintext metadata channel.
Phase 6D data TLS does not provide raw FTP TLS compatibility and is not a GSI
replacement.

## Tree Clients

Directory clients support the same auth settings:

```bash
./build/cpnetflux-tree-upload-client \
  --host 127.0.0.1 \
  --port 2121 \
  --source-dir /tmp/demo-tree \
  --dest-dir demo-tree \
  --auth-mode token \
  --auth-token-file /tmp/cpnetflux-token.txt
```

`--auth-token-file` is read locally by the client and is never written to JSON
summary output.

## Release And Public Hygiene

Token files are runtime-only private inputs. They must not be committed,
included in artifact manifests, copied to public export, or written to demo/perf
logs. Public docs should use placeholders such as `<token-value>` rather than
real token contents.

Phase 6B/6C/6D event logging follows the same rule: auth/TLS events may record
`auth_required`, `auth_failed`, `tls_required`, `tls_failed`,
`data_tls_required`, `data_tls_failed`, component names, modes, and result, but
never token contents, passwords, `PASS <value>` strings, certificate private
keys, or file contents. Token, certificate, and key files remain local runtime
inputs and are not release artifacts.

`tools/release/check_public_hygiene.py --strict` rejects obvious token leaks and
token-like artifact paths.

## Non-Goals

Phase 6A/6C/6D does not provide:

- GSI/DCAU/PROT or production PKI integration;
- production identity, account, or authorization management;
- production-grade encrypted transport policy or LIST/NLST data encryption;
- AUTH TLS explicit upgrade semantics;
- raw FTP STOR/RETR compatibility;
- changes to checksum, manifest, resume, or final verify semantics.

The final alpha limitation list is maintained in
`docs/release/ALPHA_LIMITATIONS.md`.
