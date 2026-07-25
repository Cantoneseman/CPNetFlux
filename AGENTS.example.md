# GridFlux — Public AI Collaboration Guide

## Project Summary

GridFlux is a GridFTP-compatible high-performance transfer foundation. It exposes a small GridFTP-like control plane and uses a GridFlux framed data channel internally for reliable STOR/RETR transfer, checksum, manifest, and resume behavior.

## Development Notes

- Language: C++20, Linux only.
- Build: CMake 3.20+, Ninja recommended.
- Tests: GoogleTest and CTest.
- Baseline transport: POSIX socket + epoll.
- File IO backends: `posix` by default; optional `io_uring` only when explicitly enabled and liburing is available.
- Data-path rules: avoid exceptions, virtual dispatch, serialization frameworks, and per-event dynamic allocation in hot paths.

## Private Test Environment

Do not put real server IPs, passwords, tokens, private keys, cookies, or cloud account details in this file.

Use local/private documentation outside the public repository for concrete machine details. A safe placeholder topology looks like:

| Machine | Public IP | Private IP | User | Password |
|---------|-----------|------------|------|----------|
| machine-one | `<public-ip>` | `<private-ip>` | `<user>` | `<password>` |
| machine-two | `<public-ip>` | `<private-ip>` | `<user>` | `<password>` |

## Release Hygiene

Before publishing to GitHub:

```bash
python3 tools/release/check_public_hygiene.py --path .
python3 tools/release/export_public_repo.py --output /tmp/gridflux-public
python3 tools/release/check_public_hygiene.py --path /tmp/gridflux-public --strict
```

The public export must not contain the private `AGENTS.md` file.
