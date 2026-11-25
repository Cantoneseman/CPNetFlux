# R770 Receiver CPU Follow-up 01

## Scope

Establish a receiver-side CPU cost profile for one 1 GiB transfer using the current fixed commit and production-default buffered inline mmap path. Use the profile to review one bounded software candidate that reduces a measured receiver cost. A candidate may proceed to a 1 GiB five-pair interleaved A/B only after its mechanism is supported by the profile and focused correctness checks pass.

## Baseline and known evidence

- Fixed starting commit: `6a308498a5434f8df28dd57e0a3b8b1be14f2490`.
- The existing phase-level receiver profile is for 512 MiB, not 1 GiB. It measured mapped copy and physical mmap write at about 0.34 s each, socket read at 0.092 s, CRC32C at 0.009 s, and 131,072 minor faults.
- Direct-to-mmap was rejected by a 1 GiB five-pair A/B and remains opt-in/off.
- Private tmpfs `huge=within_size` was rejected in two 1 GiB matrices by RX missed/OOB and TCP retransmissions. Do not repeat it or weaken network gates.
- Historical `MADV_POPULATE_WRITE` and larger write-batch candidates have negative evidence.

## Required profile configuration

Use V3 TCP with four lanes, buffered inline mmap, direct receive disabled, 256 KiB DATA frames, requested 1 MiB receive buffer, 1.5 GB/s per-lane application pacing, CRC32C, 1 MiB chunks, checkpoint every 16 chunks, tmpfs, compression off, and explicit inter-node binds from `192.168.100.1` to `192.168.100.2`. Both endpoints must use the fixed commit and matching server/client binary SHA-256 values.

Capture receiver process CPU, mapped-copy and mapped-write cost, page faults, socket receive/read cost, CRC cost, range-lock/state/checkpoint cost, syscall/scheduling observations where available, memory pressure, and before/after network counters. Preserve task protocol completion, SHA-256, chunk validation, and cleanup evidence.

## Candidate and A/B gates

Do not choose or implement a candidate until the 1 GiB profile identifies an attributable cost and a mechanism that has not already been rejected. Keep the candidate limited to one measured software cost. Run focused correctness tests and a bounded 512 MiB smoke/profile before the 1 GiB comparison.

The 1 GiB comparison requires at least five interleaved pairs on the same commit, build, binary hashes, workload, and explicit binds. Accept only if receiver CPU decreases and median end-to-end task throughput improves by at least 10%, coefficient of variation is at most 5%, at least four of five pairs favor the candidate, and all integrity, memory, cleanup, and network gates pass. Reject any sample with changed RX/TX errors, RX missed/OOB, unexplained retransmission growth, SHA/chunk mismatch, or resource-limit failure. Stop after a failed gate. Do not change NIC, IRQ/RSS, routing, MTU, offloads, kernel settings, pacing, DATA frame size, or protocol semantics.

If the profile does not support a safe, non-rejected candidate, record the measured profile and candidate review, retain the current default, and report that no performance improvement was proven. Do not run an A/B matrix without a viable candidate.

## Non-goals

- No annual GridFTP comparison or security-group matrix.
- No TLS compatibility change represented as a throughput optimization.
- No retry of the rejected TLS inter-node sample.
- No direct-to-mmap, private THP, frame-size, pacing, or network-configuration scan.

## Evidence and handoff

Store raw run evidence under `/home/Su/experiments/CPNetFlux`. Record input/final commit, commands, binary hashes, profile artifacts, integrity and network gates, cleanup, and the candidate decision in a result document. R2 only builds a pushed fixed commit. Do not touch protected historical untracked worker-state documents or CPSS(DCC).
