# GridFlux Alpha Limitations

This document is the final alpha limitation checklist. It describes current
behavior, risk, and intended follow-up work. It is not a production security or
performance guarantee.

## LIST/NLST Listing Data

- Current behavior: `LIST` and `NLST` commands can be issued over a TLS-protected
  control connection, but their passive ASCII listing data channel remains
  plaintext.
- Risk: directory names and metadata returned by listing commands can be visible
  on the passive data socket.
- Follow-up: add an explicit metadata data-channel TLS design, or replace the
  listing compatibility path with a structured GridFlux metadata API.

## GSI And Production Auth

- Current behavior: token auth is file-based and alpha only; TLS is certificate
  file based and operator managed.
- Risk: no production identity lifecycle, delegation, GSI/DCAU/PROT, account
  mapping, revocation, or policy engine exists.
- Follow-up: design production auth separately from the alpha token/TLS path.

## Raw FTP And Recursive FTP

- Current behavior: STOR/RETR file data uses GridFlux framed protocol only.
  Directory transfer is GridFlux orchestration over per-file framed STOR/RETR.
- Risk: generic FTP clients and raw recursive FTP workflows are not compatible.
- Follow-up: evaluate raw FTP compatibility only after alpha framed behavior is
  stable.

## Directory Metadata Fidelity

- Current behavior: directory transfer sends regular files only. It does not
  preserve empty directories, permissions, owner/group, xattr, ACL, hard links,
  or symlink targets.
- Risk: restored datasets are content-equivalent file trees, not full filesystem
  replicas.
- Follow-up: add an opt-in metadata manifest if dataset workflows require it.

## 100G Performance

- Current behavior: private 1GiB matrix and repeat sampling exist, but a stable
  100G dedicated-line validation has not been completed.
- Risk: alpha throughput can vary with storage writeback, page cache, network
  send path, final verify policy, and environment noise.
- Follow-up: run dedicated 100G validation and storage/network profiling as a
  separate performance phase.

## Observability

- Current behavior: event logs are local JSONL files and release reports are
  local Markdown/JSON artifacts.
- Risk: there is no central metrics server, alerting, retention policy, or
  fleet-wide correlation.
- Follow-up: add a production observability backend after the alpha event schema
  settles.

## Security Scope

- Current behavior: control TLS and STOR/RETR framed file data TLS are opt-in.
  Defaults remain anonymous and TLS off for demo compatibility.
- Risk: alpha deployments must explicitly enable token/TLS settings and manage
  certificate/token files safely.
- Follow-up: define production defaults, CA handling, secret rotation, and
  deployment policy in a separate security phase.
