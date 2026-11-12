# Stage81: TLS crosshost verified; authentication harness false negative

Status: original matrix FAIL_HARD_GATE preserved; overall NOT_READY. C/F were NOT_RUN. Runtime remains dbb2d6bac86b29914dbea655b29660937267e38f, delivery 5fb5096f2eed3ebd3425999a5be0681dbd32002d, identical four binaries/config on both hosts.

- Fresh 1 GiB whole-task admission: 33.3531 Gbps, content/resource/network/cleanup gates passed. Single sample only; Stage80 five-pair comparison remains the comparison evidence.
- Real crosshost 8 MiB verified TLS download passed: 2 files, 3 directories including empty directory and zero-byte file, full external SHA/tree equality, control_tls_verified/data_tls_verified true. Not a large-file encrypted throughput measurement.
- Deliberately wrong token: normal return code 1, structured error auth_failed / PASS rejected, bytes_total=bytes_transferred=file_count=0 and no file publication. Product correctly rejected authentication; S text-substring oracle falsely rejected the diagnostic wording. Original false-negative result is not rewritten as matrix PASS.
- Each host passed 42 offline tests, input/import/compile/read-only gather and actual observer deadline probe before starting. No source/Git changes or same-window retries. On first failure RX/TX restored1024/1024, all owned processes/listeners/mounts/payload/credentials cleaned; original2121 and unrelated state preserved.

Stage82 replaces fragile text matching with a strict structured auth oracle: successful exit, signal/deadline, TLS/transport failure, missing counters, inventory/payload or published files cannot pass. 8 behavioral tests and replay of the actual Stage81 rejection passed.

Stage82 also exercised the actual F helper functions against real binaries on R1 loopback (guard bypass is explicit for this local functional test only; not crosshost acceptance). It caught a harness counter mismatch: control-reset events do not increment FaultProxy.dropped (data counter). Predicate now requires exactly one reset JSON event of the requested scope and full final tree SHA. After correction, all six data-reset/control-reset/bad-data/cancel-resume/bad-checkpoint/restart-resume cases passed; all generated <=1 GiB fixtures removed. Input-missing/fixture-mismatch/control-counter RED evidence retained. Crosshost F and full C remain necessary.

Evidence: /home/Su/experiments/CPNetFlux/2026-11-13-field-trial-followup-81. delivery-final.tar.gz 10,205,575 bytes; SHA256 b4387739447739c4f594d95bb0f54af9a99132ad5fdfabf732e25050254266ef. Windows D:/Project/CPNetFlux/_codex_field_trial_followup_81: archive SHA and 248 individual manifest file hashes verified. No private credential or payload in Git.
