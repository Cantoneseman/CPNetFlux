# CPNetFlux Beta Matrix

This directory holds the local planning layer for the CPNetFlux Beta integration
work. As of Lab Beta 3, retained evidence runs live under
`tools/perf/results/<UTC>_lab-beta-*`.

## Current Lab Status

- Lab Beta 0: ctest gate fix and small public smoke accepted as `pass/go`.
- Lab Beta 1: Shenzhen -> Shanghai public protocol baseline accepted as
  `full_pass/go`.
- Lab Beta 2: public control-reuse expansion accepted as `pass/go`.
- Lab Beta 3: public compression staging history is now split into the original
  no-go, the final-status failure triage, and the after-fix bounded rerun. The
  retained Beta 3 mouthpiece remains: raw/gzip/lz4 Beta 3 staging correctness
  passed; CPSS was `environment_blocked` in Beta 3; this Beta 3 evidence alone
  is not `full_go`.
- Lab Beta 4: CPSS environment unblock and minimal staging gate accepted as
  `cpss_pass_go`. CPNetFlux CPSS worker and GridFTP CPSS passed; optional FTP
  CPSS remained environment-blocked and is not a core failure.
- Lab Beta 5: report evidence pack generated at
  `tools/perf/results/20260722T080536Z_lab-beta-5-report-evidence-pack/`.
  The combined Beta 1/2/3/4 evidence is CPSS-aware go review-ready.

## Scope

- Beta 3 reads deterministic slices from OSS bucket
  `science-compressor-datasets` on the Shenzhen server.
- Dataset total is capped at `<= 3GiB`; the default retained run targets about
  `1.2-1.6GiB` across HPC, AI training, and metadata/small-file samples.
- Compression remains staging-only: raw transfers use the source tree; gzip,
  lz4, and CPSS transfer staged artifacts and restore to the original tree on
  Shanghai before SHA/tree-hash validation.
- The CPNetFlux C++ protocol hot path and public API are unchanged.
- FTP/GridFTP anchors must use real protocol tools. No scp/rsync, SSH tunnel,
  private IP, or fallback protocol substitution is allowed.

## Outputs

The Beta 3 retained runner writes:

- `summary.csv`
- `dataset_summary.csv`
- `method_comparison.csv`
- `oss_inventory.csv/json`
- `staging_manifest.jsonl`
- `method_runs.jsonl`
- `compression_audit.json`
- `restore_audit.json`
- `ratio_audit.json`
- `cleanup_audit.json`
- `completion_audit.json`
- `live_metrics/`
- `raw_logs/`
- `final_summary_zh.md`

## Control Reuse

`cpnetflux_b1_control_reuse` and `cpnetflux_b3_reuse_parallel` map to the new
tree-transfer opt-in `--control-reuse worker`. The default remains `off`.

## Compression Staging

Beta 3 uses archive-based gzip/lz4 staging and block-based CPSS staging. CPSS
uses a temporary venv from `/root/projects/science-compressor`; if CPSS
dependencies fail, CPSS rows are marked environment-blocked and cannot block the
raw/gzip/lz4 Beta 3 correctness gate, but CPSS environment-blocked also means
the result is not `full_go`.

The final status vocabulary is `full_go`,
`partial_go_cpss_environment_blocked`, `blocked_environment`,
`blocked_oss_unavailable`, `fail_correctness`, and `fail_cleanup`.

Beta 4 separately unblocked CPSS with an isolated `/opt/cpnetflux-cpss-venv`,
verified tiny and 64MiB CPSS roundtrips on both hosts, and ran CPSS-only staged
transfer rows. CPNetFlux CPSS worker and GridFTP CPSS are required methods and
passed for the three retained OSS datasets. FTP CPSS is optional and remained
environment-blocked because `vsftpd` did not start for that anchor.

Beta 5 does not add new measurements. It packages Beta 1/2/3/4 retained evidence
into CSV/JSON suitable for plotting:

- `protocol_baseline_comparison.csv/json`
- `control_reuse_speedup.csv/json`
- `compression_staging_comparison.csv/json`
- `cpps_staging_comparison.csv/json`
- `final_claims_and_limits.json`
- `ppt_key_numbers_zh.md`

## Beta 3 Evidence Timeline

The original retained run at
`tools/perf/results/20260720T051105Z_lab-beta-3-public-compression-staging-matrix/`
completed all 99 planned rows. FTP and GridFTP raw/gzip/lz4 anchors passed, CPSS
was marked environment-blocked, and one required CPNetFlux row failed:
`oss_ai_training_mixed` / `cpnetflux_lz4_worker` / repeat 2 returned
`exit_code=1` with `recv: Resource temporarily unavailable`. The restored tree
matched the source hash, but the transfer command did not complete successfully,
so that historical evidence remains `fail_correctness` / no-go.

The failure triage retained evidence at
`tools/perf/results/20260721T034707Z_lab-beta-3-failure-triage-cpnetflux-lz4-final-status/`
is `pass_after_fix`. It covered only the failed
`oss_ai_training_mixed` / `cpnetflux_lz4_worker` case and verified that the tail
data final status timeout fix makes the minimal public repro pass 3/3.

The bounded rerun after fix at
`tools/perf/results/20260721T093510Z_lab-beta-3-compression-staging-rerun-after-final-status-fix/`
is `partial_go_cpss_environment_blocked`: 99 rows were written, raw/gzip/lz4
CPNetFlux/FTP/GridFTP transfer status and restore/tree hash passed, and 18 CPSS
rows remained environment-blocked. The old failure point repeated 0..2 passed;
repeat 2 may record `recovered_after_data_final_status_timeout=true`, which is a
successful recovery path, not a correctness failure.

Beta 3 has not run 10GiB/20GiB/100GiB/heavy soak, and it has not entered a
CPNetFlux C++ hot-path compression design.

## Beta 4/5 Evidence Timeline

The CPSS environment unblock retained evidence at
`tools/perf/results/20260722T032005Z_lab-beta-4-cpss-environment-and-staging-gate/`
is `cpss_pass_go`. CPSS check-env and roundtrip passed on both hosts. CPNetFlux
CPSS worker and GridFTP CPSS passed three repeats over each retained dataset;
FTP CPSS remained optional `environment_blocked`.

The Beta 5 report evidence pack at
`tools/perf/results/20260722T080536Z_lab-beta-5-report-evidence-pack/` is a
summary-only package. It reads Beta 1/2/3/4 evidence as source of truth, keeps
old retained evidence immutable, and states that the combined evidence is
CPSS-aware go review-ready. It still does not imply heavy soak or 50G/100G
readiness.
