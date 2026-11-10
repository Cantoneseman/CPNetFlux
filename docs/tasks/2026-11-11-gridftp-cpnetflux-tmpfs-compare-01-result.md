# CPNetFlux/GridFTP tmpfs compare 01 result

Date: 2026-11-11
State: BLOCKED; credential refresh, GSI smoke, build comparison, screening and transfers were not run.
Source commit on both nodes: a811f0b54c4075a5c13a3208cf14015e378d3963.

## Preflight

- R1 branch: codex/V3-GRIDFTP-OPTIMIZED-RERUN-01; origin is git@github.com:Cantoneseman/CPNetFlux.git. Remote SHA matches source commit. Two pre-existing unrelated untracked R1 documents were preserved. R2 is clean at the same commit (detached HEAD), with the same GitHub repository over HTTPS.
- Both CMake configurations match: RelWithDebInfo; RDMA, IO_URING, ISAL and TLS ON. No build or binary hash comparison was run after the auth stop gate.
- Both machines: Intel Xeon 6515P, 32 logical CPUs; MemAvailable approximately 28.6/28.2 GiB; /dev/shm tmpfs 16 GiB free. Root filesystem 90%/91% used, so all experiment data must remain on tmpfs. No payload was generated.
- Reverse routes for .1/.2 select eno17095np0. Link UP at 100000 Mb/s, MTU 1500. No route changes. Management IPs are 192.168.1.109/.102.
- No benchmark, Globus GridFTP server, or task experiment process found. Each node has a long-running cpnetflux-gridftp-server from 2026-10-30 on its data IP port 2121; untouched. No Globus daemon currently runs.
- globus-url-copy is 10.13 and globus_gridftp_server is 13.28 on both.

## Blocker

- grid-proxy-info on both nodes reports no valid proxy; /tmp/x509up_u1002 is absent.
- Both nodes lack /home/Su/.globus/usercert.pem, userkey.pem, /etc/grid-security/hostcert.pem, hostkey.pem and /etc/grid-security/certificates. No MyProxy client command or usable credential files were found.
- Cannot refresh or validate a proxy with one-hour lifetime or verify server trust. No isolated GridFTP server or 8 MiB private smoke was started. Existing TCP/2121 belongs to CPNetFlux and was left unchanged.

## Not run

No rebuild/hash gate, GridFTP tuning screen, formal workload, transfer timing, NIC/HCA delta, CPU/I/O sampling or external SHA was run. There are no performance samples. Old M1-44 and GridFTP results from other machines are excluded.

Resume when an existing secure proxy refresh procedure or managed credential/proxy path is provided. Never send credential contents in chat.
