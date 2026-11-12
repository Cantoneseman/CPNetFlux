# Stage82: structured authentication oracle and remaining acceptance

Scope: preserve E81 false-negative S result; replace message substring matching with strict auth_failed/zero-payload/no-publication/normal-failure validation, reject other TLS/transport/deadline failures; preflight actual F harness on loopback before expensive long acceptance; finish real crosshost S/C/F in fresh <=90 minute reversible ring window. No product or throughput-profile changes, no reusing old window, no scanning. Input delivery 5fb5096f2eed3ebd3425999a5be0681dbd32002d; runtime dbb2d6b unchanged.

Affected: tools/perf/field_trial_auth_oracle.py, tools/test/run_field_trial_auth_oracle_test.py, stage82 experiment harness and local F preflight, E81 result/E82 scope/current status docs. Acceptance: old actual auth rejection passes structured oracle, TLS/transport/timeout/success/payload/published-file failures cannot pass; dual-host tests/build/hash/frozen-input gates; actual verified TLS+bad token; C same A backend and source PID >=1800 seconds and >=100 fresh target tasks; six crosshost fault cases plus postrestart TLS; original resource/network guards and cleanup/rollback/backups.

Pre-window local actual F preflight: {"passed": true, "cases_passed": ["data", "control", "corrupt", "cancel", "bad-checkpoint", "restart"], "loopback_only": true, "entry_guard_bypassed_only_for_local_functional_test": true, "network_untouched": true, "payload_credentials_cleaned": true}

Crosshost S/C/F are still pending; no readiness promotion.
