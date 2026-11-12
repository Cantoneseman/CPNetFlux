# Stage83: correct payloads, incomplete long gate due image-summary overwrite

Fixed delivery05e48119231632a6f4b2fc1de4e2596aed01ac32/runtime dbb2d6b; same dual build/config/four binary hashes. Both hosts70 tests/42 scripts compiled/18 safe imports passed. S regression passed (verified control/data TLS, complete SHA/tree, auth_failed wrong token with zero payload/no publication).

C status FAIL,189 accepted of190 attempts,189 logicalGiB,elapsed1468.233617s <1800s,actual crosshostF NOT_RUN. C189 task median31.762Gbps; first/last10 medians32.072→31.932Gbps,drift about-0.44%. These are repeated fresh1GiB tasks, with external SHA/observation/cleanup between tasks; not continuous line-rate load. Same sourcePID1712991/start116858812,FD4,swap0,RSS about10.7–11.7MiB; no raw resource/network violations. C is not passed and duration is not combined with other runs.

C190 completed actual payload/1024 chunks/externalSHA/pages. Formal gate falsely reported r1 actual process not observed. Its raw trace contains5 real cpnetflux-tree-download-client samples forPID228576/start116516618; max sample interval0.0512s. Node aggregate repeatedly assigned processes[PID:start]=current; exit-time empty/proc/cmdline erased the final executable field. All raw samples and original FAIL remain frozen. This is an observer aggregation defect, not missed sampling or measured data/resource failure.

Stage84 preserves actual observed_executables per PID+start, keeps latest snapshot fields unchanged, and rejects mixing reused identities. Actual trace replay: old last-image predicate RED/new history GREEN;7 tests also reject never-observed images/PID reuse. Both node and ring-resource collectors use the same helper; formal presence checks actual image history. No product binary/profile/network-gate relaxation. New uninterrupted full C and actual crosshostF still required.

Both rings restored RX/TX1024/1024; owned services/listeners/mounts/payload/private credentials cleaned,original2121 preserved. Source owner probes had been real on both hosts and current-source gate passed beforeC payload. No same-window fixes/retries.

Evidence /home/Su/experiments/CPNetFlux/2026-11-13-field-trial-observer-83; final archive120793448 bytes,SHA25649062318103782480da97d680dcb02d0a71f94fd8c18e3f7b5f6bd0d66ee6c05. Windows D:/Project/CPNetFlux/_codex_field_trial_observer_83: archive and8281 individual hashes checked. No large payload/credential/build output inGit. Overall NOT_READY until remaining acceptance.
