# Stage83: cover persistent source PID before long acceptance

Scope: preserve E82 failed C observation; add an exact stage-private-source argv ownership predicate for CPNetFlux server, keep all existing owned-process/swap/network gates, prove actual detached source PID/start/RSS/fd/swap coverage using the real observer before touching RX ring, then complete C/F under unchanged runtime/profile with fresh bounded window. S was verified in E82 and may be referenced with exact provenance, with a short S regression if needed.

Non-goals: no product/performance-profile change, no hiding missing coverage, no reusing old window, no network changes other than authorized temporary RX4096. Input60a3d87c0f7b1402c7050bc7d1a0a88a3da0ab50/runtime dbb2d6b. A/B/reverse Stage80 retained.

Affected: tools/perf/field_trial_owner.py, tools/test/run_field_trial_owner_test.py; Stage83 node selector/probe/frozen experiment inputs and E82 result/current status docs. Acceptance: captured C command old selector false/new true; near prefixes/unrelated executables false; actual detached real source service observed by PID/start with live fd/RSS/swap through real node watch samples, both hosts; same fixed dual build/four hashes; C >=1800s and >=100 fresh 1GiB native targets with same source PID and A backend; six real crosshost fault cases+postrestart TLS; strict resource/network+cleanup/rollback+backups.

R1 actual observer proof: {"passed": true, "node": "r1", "source_pid": 207585, "start_ticks": 116310241, "old_selector_covered": false, "new_selector_covered": true, "actual_root_observer": true, "source_observed_samples": 14, "total_samples": 14, "fd_range": [4, 4], "rss_range": [8749056, 8749056], "swap_peak": 0, "network_untouched": true, "payload_bytes": 0, "source_stopped_and_root_removed": true}

R2 actual proof and final long crosshost C/F pending; readiness remains NOT_READY.
