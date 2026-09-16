# CPNetFlux Alpha Release Gate

- Timestamp: `2026-08-31T05:49:55Z`
- Mode: `quick`
- Source tree hash: `c5823abde59f1e22ee4c463dba6bbb64c6dd4e696b9653a4e262172f65b5b7b1`
- Result: `pass`
- Total steps: `22`
- Passed steps: `22`
- Failed steps: `0`

## Step Results

| Step | Status | Error Code | Seconds | Log |
|------|--------|------------|---------|-----|
| `build_debug` | `pass` | `ok` | `0.02` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/build_debug.log` |
| `ctest_debug` | `pass` | `ok` | `34.51` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/ctest_debug.log` |
| `ctest_iouring` | `pass` | `ok` | `33.88` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/ctest_iouring.log` |
| `ctest_iouring_smoke` | `pass` | `ok` | `0.01` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/ctest_iouring_smoke.log` |
| `public_export_hygiene` | `pass` | `ok` | `0.47` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/public_export_hygiene.log` |
| `stor_smoke` | `pass` | `ok` | `0.18` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/stor_smoke.log` |
| `retr_smoke` | `pass` | `ok` | `0.35` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/retr_smoke.log` |
| `stor_resume_smoke` | `pass` | `ok` | `0.23` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/stor_resume_smoke.log` |
| `retr_resume_smoke` | `pass` | `ok` | `0.24` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/retr_resume_smoke.log` |
| `metadata_smoke` | `pass` | `ok` | `0.12` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/metadata_smoke.log` |
| `list_smoke` | `pass` | `ok` | `0.22` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/list_smoke.log` |
| `token_auth_smoke` | `pass` | `ok` | `0.22` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/token_auth_smoke.log` |
| `tls_control_smoke` | `pass` | `ok` | `0.49` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/tls_control_smoke.log` |
| `data_tls_smoke` | `pass` | `ok` | `1.82` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/data_tls_smoke.log` |
| `tree_upload_smoke` | `pass` | `ok` | `0.32` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/tree_upload_smoke.log` |
| `tree_download_smoke` | `pass` | `ok` | `0.46` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/tree_download_smoke.log` |
| `tree_resume_smoke` | `pass` | `ok` | `0.75` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/tree_resume_smoke.log` |
| `tree_parallel_smoke` | `pass` | `ok` | `0.93` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/tree_parallel_smoke.log` |
| `tree_changed_file_smoke` | `pass` | `ok` | `4.20` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/tree_changed_file_smoke.log` |
| `tree_edge_cases_smoke` | `pass` | `ok` | `8.51` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/tree_edge_cases_smoke.log` |
| `tree_manifest_corrupt_smoke` | `pass` | `ok` | `0.18` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/tree_manifest_corrupt_smoke.log` |
| `alpha_demo_local` | `pass` | `ok` | `3.27` | `/root/projects/CPNetFlux-Beta/tools/perf/results/20260831T054824Z_alpha-release-gate/alpha_demo_local.log` |

## Private Baseline

- Not run in quick mode.

## Artifact Sync

- Manifest: not generated.

## Alpha Readiness

- Alpha scope is demonstrable GridFTP-like framed STOR/RETR, bidirectional resume, CRC32C chunk verification, and control metadata commands.
- Not beta/production: performance spread remains significant, 100G dedicated-line validation is not complete, and TLS/GSI/raw FTP stream/directory sync are out of scope.
- Defaults remain POSIX backend, full final verify, every_n_chunks manifest flush, no commit fsync, no preallocate full, and no default io_uring.

## Residual Process Check

- Local: ``
- Remote: ``

## Failures

- None.
