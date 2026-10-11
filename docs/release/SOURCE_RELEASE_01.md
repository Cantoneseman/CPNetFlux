# CPNetFlux Source Release Candidate

The source-release tool builds a deterministic, sanitized source-only archive
from one explicit Git commit. The package is a technical delivery candidate:
it is not a signed installer, public GitHub release, or license grant.

## Create an archive

Run this only from the Shenzhen CPNetFlux source worktree after the intended
commit has been pushed to the verified GitHub remote. The worktree must be
clean and its HEAD must be the requested commit.

```bash
commit=$(git rev-parse HEAD)
python3 tools/release/create_source_release.py \
  --commit "$commit" \
  --remote origin \
  --remote-ref codex/<task-id> \
  --output "/tmp/cpnetflux-runs/source-release/cpnetflux-source-${commit:0:12}.tar.gz"
```

The archive and neighboring `.tar.gz.manifest.json` are created without
overwriting existing paths. The manifest identifies the commit, branch,
verified remote URL/ref/SHA, normalized exported-tree hash, file count, archive
SHA-256, hygiene result, and license status. Packaging checks the remote before
and after export. Output must be outside the Git worktree.

The exporter omits private collaboration instructions, build products,
credentials/certificates/private keys, and performance payloads. Strict hygiene
runs before archiving. The artifact does not include Git metadata.

## Verify and build

Check the archive hash against `archive_sha256` in the manifest, then unpack
it into a new empty directory and build from that extracted tree:

```bash
sha256sum /tmp/cpnetflux-runs/source-release/cpnetflux-source-<commit>.tar.gz
mkdir -p /tmp/cpnetflux-runs/source-release/unpacked-<commit>
tar -xzf /tmp/cpnetflux-runs/source-release/cpnetflux-source-<commit>.tar.gz \
  -C /tmp/cpnetflux-runs/source-release/unpacked-<commit>
cmake -S /tmp/cpnetflux-runs/source-release/unpacked-<commit>/CPNetFlux-source-<commit> \
  -B /tmp/cpnetflux-runs/source-release/build-<commit> \
  -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_ENABLE_TLS=ON
cmake --build /tmp/cpnetflux-runs/source-release/build-<commit> --parallel
ctest --test-dir /tmp/cpnetflux-runs/source-release/build-<commit> --output-on-failure
```

The desktop client has its own CMake project under `desktop/` and must be
configured, built, and tested separately when desktop delivery is in scope.
A successful build and test run establishes source reproducibility for that
revision; it does not establish production certification or deployment
readiness.

## Redistribution and installation limits

The checked-in source tree currently has no root `LICENSE` file. Therefore
the generated manifest reports `no-root-license-found` and keeps
`public_release_ready` false. A reviewer must select and approve a license
before public redistribution. The source archive can still be used internally
for build and acceptance checks.

The root CMake project currently has no install rules. This source-only package
does not install binaries, configure a service, provision credentials, or
create a Linux installer. Those remain separate delivery work.
