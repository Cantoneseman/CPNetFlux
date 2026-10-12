# DESKTOP-REAL-POLICY-EVIDENCE-02

## Scope
Fix the real desktop agent readback boundary: keep actual mode, scheduler,
checksum, resume and TLS unknown/pending until the core JSON summary returns.
After completion, copy only structured summary facts into the public snapshot.
The current core summary does not expose a verified TLS handshake fact, so the
UI keeps TLS unknown. Add a regression test and rebuild the installer from the
packaging-fix ancestor.

## Non-goals
No protocol, core V2/V3, authentication, QML, single-file client, remote
directory browsing, performance experiment, or cross-domain network changes.

## Acceptance
- Agent contract and policy-readback regression tests pass.
- Desktop CMake/CTest tests pass.
- A clean build generates DEB/TGZ and DEB declares required Qt runtime packages.
- Install staging, package contents, and SHA256 verification pass.
- The Shenzhen authority root remains untouched.

## Allowed paths
- desktop/agent/cpnetflux_agent.py
- desktop/tests/test_agent.py
- docs/tasks/2026-10-12-desktop-real-policy-evidence-02.md

## Rollback
Revert this branch commit to return to the PACKAGING-DESKTOP-REAL-02 package-fix
version. The Shenzhen authority root is not modified.
