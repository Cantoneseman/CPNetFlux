# Stage 81: TLS configuration correction and remaining acceptance

Scope: reproduce Stage 80 zero-payload TLS failure with the same runtime and one changed variable (sender application pacer); correct harness TLS/plain profile selection, reject incompatible frozen inputs before effects, handle owned root metadata during cleanup, retain accepted Stage 80 A/B/reverse evidence, then run real cross-host S/C/F under a fresh <=90-minute ring window.

Non-goals: no runtime/network algorithm changes, no pacing/rate scan, no reusing old ring window, no changing the accepted comparison, no RDMA tuning, no persistent network setting.

Input delivery commit: 5e68e1c163e22d7dfb20241e3069d8993cf199ad; runtime dbb2d6bac86b29914dbea655b29660937267e38f. Stage 80 writer released in serial-return.json, all owned resources cleaned and RX/TX=1024/1024.

Acceptance: bounded local 8 MiB TLS pacer-on RED / pacer-off GREEN including complete file hashes and directories; meaningful frozen-profile regression rejected before process/network changes; same dual-host commit/config/four hashes; verified TLS and bad-token rejection; C same A backend/source PID >=1800 s and >=100 fresh targets; six actual cross-host fault/recovery cases and TLS after restart; resource/network guards, cleanup/rollback proof and evidence backup.

Affected files: tools/perf/field_trial_profiles.py, tools/test/run_field_trial_profiles_test.py; stage81 experiment harness copies and scope/evidence outside repository; task result and current roadmap/baseline links. Product runtime files unchanged.

Cleanup helper scope refinement: tools/perf/field_trial_cleanup.py and tools/test/run_field_trial_cleanup_test.py. Only the exact owned dst-* tree-download metadata basename may be archived and removed; unexpected files/symlinks/collisions must stop cleanup and preserve input.

Root causal result: {"passed": true, "one_changed_profile_variable": "CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND", "runtime_unchanged": true, "temporary_payload_credentials_cleaned": true, "rows": [{"label": "pacer-on", "returncode": 1, "elapsed_seconds": 10.019906086847186, "pacer": "1500000000", "bytes_transferred": 0, "result": "fail", "error": {"error_code": "tls_failed", "message": "reconnect attempt budget exhausted: TLS peer closed unexpectedly"}, "control_tls_verified": false, "data_tls_verified": false, "complete_tree_match": false, "source_bytes": 8388608, "loopback_only": true, "network_settings_unchanged": true}, {"label": "pacer-off", "returncode": 0, "elapsed_seconds": 0.12607414205558598, "pacer": "off", "bytes_transferred": 8388608, "result": "pass", "error": null, "control_tls_verified": true, "data_tls_verified": true, "complete_tree_match": true, "source_bytes": 8388608, "loopback_only": true, "network_settings_unchanged": true}]}

Harness local checks: profile 6/6, owned-metadata cleanup 5/5. Runtime unchanged; final crosshost S/C/F still pending.
