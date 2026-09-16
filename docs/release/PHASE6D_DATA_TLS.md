# Phase 6D Data TLS Alpha

Phase 6D adds opt-in TLS for the STOR/RETR framed file data channel. Defaults
remain unchanged: `auth-mode=anonymous`, `tls-mode=off`, `data-tls-mode=off`,
POSIX file IO, full final verify, every-n-chunks manifest flush, preallocate
off, and POSIX write strategy auto.

## Scope

- Control-plane TLS from Phase 6C remains opt-in with `--tls-mode required`.
- File data TLS is enabled with `--data-tls-mode required`.
- Server-side data TLS is accepted only when control TLS is also required.
- STOR/RETR frames, CRC32C, manifests, resume, verified_chunks, and final verify
  semantics are unchanged.
- Tree upload/download inherit data TLS because each file still uses STOR/RETR.

Phase 6D does not protect LIST/NLST passive ASCII listing data. LIST/NLST
commands can be protected by control TLS, but the listing data connection
remains plaintext metadata in this alpha.

## Example

```bash
./build/cpnetflux-gridftp-server \
  --root /tmp/cpnetflux-root \
  --host 127.0.0.1 \
  --port 2121 \
  --tls-mode required \
  --tls-cert-file /tmp/cpnetflux-cert.pem \
  --tls-key-file /tmp/cpnetflux-key.pem \
  --data-tls-mode required
```

CPNetFlux-aware file clients then pass:

```bash
--data-tls-mode required --tls-ca-file /tmp/cpnetflux-cert.pem
```

Tree clients pass the same flags together with `--tls-mode required` and the CA
file.

## Validation

The loopback data TLS smoke verifies:

- framed STOR over TLS;
- framed RETR over TLS;
- plaintext framed data client rejection against data-TLS-required server;
- tree upload/download over data TLS;
- LIST/NLST still functioning as the existing plaintext metadata channel;
- event/log output does not contain private key material.

The full alpha release gate also runs a private STOR/RETR data TLS smoke.

## Non-Goals

Phase 6D does not implement GSI, AUTH TLS, raw FTP TLS compatibility, production
CA management, LIST/NLST data TLS, raw FTP STOR/RETR, or 100G performance
tuning.
