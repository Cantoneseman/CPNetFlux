# Phase 6C TLS Alpha

Phase 6C adds opt-in TLS for the GridFTP-like control connection. It does not
change default behavior: `--tls-mode off` remains the default, anonymous auth
remains the default, and STOR/RETR file data still uses the existing CPNetFlux
framed TCP data channel.

## Scope

- `cpnetflux-gridftp-server` accepts `--tls-mode off|explicit|required`.
- `required` performs TLS immediately after accepting the control socket.
- `explicit` is reserved for a future AUTH TLS design and is rejected in this
  phase.
- Tree clients and Python helpers can connect to a TLS-required control server.
- Token auth can be layered on TLS: the TLS handshake completes first, then
  `USER token` / `PASS <token>` is sent.
- Event logs use stable `tls_required` / `tls_failed` error codes.

## Non-Goals

- No GSI, DCAU, PROT, or production PKI.
- No AUTH TLS upgrade semantics.
- No TLS wrapping for passive STOR/RETR data sockets.
- No TLS wrapping for LIST/NLST metadata data sockets.
- No raw FTP TLS compatibility.
- No change to checksum, manifest, resume, or final verify semantics.

## Secret Handling

TLS cert/key files are runtime inputs. Private keys must not be group/world
accessible. Certificate private keys, token contents, passwords, cookies, and
local topology files must not appear in logs, event logs, JSON reports, release
artifacts, or public exports. Public hygiene rejects private-key PEM markers.

## Validation Summary

The Phase 6C validation path covers:

- anonymous and token auth regressions with TLS off;
- TLS-required loopback metadata plus small framed STOR/RETR smoke;
- plaintext connection failure against a TLS-required control listener;
- private TLS metadata smoke where the remote client connects to the server;
- quick/full alpha release gates with artifact freshness and remote sync;
- public export strict hygiene.

Phase 6C remains alpha. It is suitable for controlled demos and operator
experiments, not production security.
