# Stage84: retain actual executable history in observation summaries

Scope: preserve Stage83 C failure and raw traces; fix per-PID/start aggregation so an exit-time empty command cannot erase an actual observed CPNetFlux image; keep all observed resource samples/gates and current-snapshot fields; validate recorded five-client-sample/empty-command trace plus negative/PID-reuse cases. Then complete new uninterrupted >=1800-second,>=100 fresh1GiB C and six crosshost F+postrestartTLS. No combining Stage83 duration, no product/profile/rate changes.

Input05e48119231632a6f4b2fc1de4e2596aed01ac32/runtime dbb2d6b. A/B/reverse Stage80 and verified S82/83 retained with provenance. Affected tools/perf/field_trial_observation.py and tools/test/run_field_trial_observation_test.py; Stage84 node/formal aggregation/gate; Stage83 result/Stage84 scope/current nav. Acceptance: actual original trace replay RED last-image check / GREEN history preserves five real CPP samples; never-observed executable and PID-reuse cannot pass; unchanged runtime/four dual hashes, complete frozen gates; new full C same source PID+A backend and actual process histories+SHA/resource/network; real six crosshost faults and postrestartTLS; rollback/cleanup/GitHub+Windows evidence backup.

The same image-history aggregation correction also applies to ring-control/resource84.py; sampling/resource/termination rules remain intact.

Actual failed trace replay: {"passed": true, "source_trace": "/home/Su/experiments/CPNetFlux/2026-11-13-field-trial-observer-83/watches/C-190-cp/trace.jsonl", "actual_client_samples": 5, "old_last_image_predicate": false, "new_history_predicate": true, "old_matrix_failure_preserved": true, "never_observed_and_pid_reuse_negative_tests": true}

7 new history tests passed; final C/F remain pending, no readiness promotion.
