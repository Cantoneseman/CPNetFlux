# R770 TLS comfort-profile compatibility

## Scope

Make the existing V3 TCP 256 KiB DATA path usable with control TLS, data TLS, or both while retaining the established four-lane comfort profile. Keep buffered inline mmap receive enabled with direct-to-mmap receive explicitly off. Allow the existing bounded 1 MiB `SO_RCVBUF` request and 1.5 GB/s/lane application pacer to run on TLS-wrapped TCP sockets.

The security profiles are token authentication with plaintext transport, control TLS with plaintext data, and control plus data TLS with anonymous authentication. Each profile changes only its selected authentication or TLS property relative to the plaintext comfort profile.

## Non-goals

- No throughput tuning, protocol/frame-format change, TLS downgrade, weaker certificate validation, network or kernel tuning, or RDMA change.
- Do not enable exact-read, kernel pacing, or direct receive into mapped pages for TLS.
- Do not combine token authentication and TLS in the security-profile smoke unless a separate experiment requests it.
- Do not run the annual comparison matrix in this task.

## Affected files

- `src/config/tree_transfer_options.cpp`
- `src/protocol/control/control_server.cpp`
- `src/core/io/framed_data_socket.cpp`
- `src/core/io/persistent_tree_transfer.cpp`
- `src/core/io/tree_transfer_client.cpp`
- `tools/test/run_cpnetflux_tls_comfort_smoke.py`
- `CMakeLists.txt`
- Result document under `docs/tasks/`

## Acceptance

- A real V3 integration test transfers 512 MiB through four TCP lanes at 256 KiB DATA, 1 MiB chunks, CRC32C, checkpoint 16, requested `SO_RCVBUF=1 MiB`, application pacing at 1.5 GB/s/lane, and buffered inline mmap receive.
- Test token-only, control-TLS/data-plaintext, and control-plus-data-TLS modes. Assert negotiated TLS state, buffer readback, pacing receipts, zero direct-receive calls, all 512 verified chunks, independent SHA-256, committed final state, and resource/artifact cleanup.
- The full control-plus-data TLS integration interrupts after a partial verified checkpoint and resumes to a complete file without changing the profile.
- Related unit and CTest gates pass. R1 and R2 build from one pushed commit with matching server/client binary SHA-256 values.
- Run one controlled 512 MiB control-plus-data TLS transfer on R770 using private tmpfs endpoints and task-local credentials. Record exact configuration, hashes, resource/network gates, and cleanup. This is functional evidence, not a performance comparison.
- Report TLS capability limits separately from performance results.
