# Phase 4A Baseline

Phase 4A 的目标是在不更换 IO 后端的前提下，用现有 POSIX socket + epoll + `pread/pwrite` framed STOR/RETR 路径建立私网性能基线。该文档记录可复现命令、CSV 路径、代表性样本和初步瓶颈判断。

## 范围

- 覆盖 GridFTP-like 控制面发起的 framed `STOR`、`RETR`、`STOR resume`、`RETR resume`。
- 不测试普通 FTP raw stream。
- 不引入 io_uring、O_DIRECT、sendfile、mmap 或系统级调参。
- LIST/NLST 目录元数据通道不作为文件数据性能目标。

## 命令

基础验证：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++-13
cmake --build build
ctest --test-dir build --output-on-failure
python3 -m py_compile tools/perf/run_gridftp_private_matrix.py
```

私网 smoke：

```bash
export CPNETFLUX_SSH_PASSWORD='***'
tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --output-dir tools/perf/results
```

代表性 1GiB：

```bash
tools/perf/run_gridftp_private_matrix.py \
  --smoke \
  --directions stor,retr \
  --bytes 1073741824 \
  --connections 8 \
  --chunk-sizes 4194304 \
  --buffer-sizes 262144 \
  --checksums crc32c \
  --remote root@<redacted> \
  --server-host <redacted> \
  --local-build-dir /root/projects/CPNetFlux/build \
  --remote-build-dir /root/projects/CPNetFlux/build \
  --output-dir tools/perf/results
```

## 当前采样

已完成一轮 Phase 4A 初始私网采样：

- smoke CSV：`tools/perf/results/20260516T151103Z_gridftp-private-matrix-smoke.csv`
- 代表性 1GiB CSV：`tools/perf/results/20260516T151131Z_gridftp-private-matrix-smoke.csv`
- resume smoke CSV：`tools/perf/results/20260516T151242Z_gridftp-private-matrix-smoke.csv`
- <redacted>一环境：`iZwz940p86wfyf3lh4emn7Z`，kernel `5.15.0-177-generic`，server temp FS `ext4`。
- <redacted>二环境：`iZwz940p86wfyf3lh4emn8Z`，kernel `5.15.0-177-generic`，client temp FS `ext4`。

Smoke 矩阵结果，64MiB/128MiB、1/4 connections、1MiB chunk、64KiB buffer：

| direction | bytes | connections | checksum | backend | throughput_gbps | result |
|-----------|-------|-------------|----------|---------|-----------------|--------|
| STOR | 64MiB | 1 | crc32c | hardware | 8.17878 | pass |
| STOR | 64MiB | 1 | none | none | 15.2021 | pass |
| STOR | 64MiB | 4 | crc32c | hardware | 8.05871 | pass |
| STOR | 64MiB | 4 | none | none | 16.9174 | pass |
| STOR | 128MiB | 1 | crc32c | hardware | 8.17215 | pass |
| STOR | 128MiB | 1 | none | none | 15.839 | pass |
| STOR | 128MiB | 4 | crc32c | hardware | 8.28839 | pass |
| STOR | 128MiB | 4 | none | none | 14.0477 | pass |
| RETR | 64MiB | 1 | crc32c | hardware | 7.50698 | pass |
| RETR | 64MiB | 1 | none | none | 14.129 | pass |
| RETR | 64MiB | 4 | crc32c | hardware | 7.82893 | pass |
| RETR | 64MiB | 4 | none | none | 13.2575 | pass |
| RETR | 128MiB | 1 | crc32c | hardware | 7.57327 | pass |
| RETR | 128MiB | 1 | none | none | 12.4479 | pass |
| RETR | 128MiB | 4 | crc32c | hardware | 8.30637 | pass |
| RETR | 128MiB | 4 | none | none | 13.4099 | pass |

代表性 1GiB 样本，8 connections、4MiB chunk、256KiB buffer、CRC32C auto/hardware：

| direction | bytes | connections | chunk | buffer | throughput_gbps | result |
|-----------|-------|-------------|-------|--------|-----------------|--------|
| STOR | 1GiB | 8 | 4MiB | 256KiB | 0.656442 | pass |
| RETR | 1GiB | 8 | 4MiB | 256KiB | 0.973053 | pass |

Resume smoke，16MiB、4 connections、1MiB chunk、64KiB buffer、CRC32C auto/hardware：

| direction | throughput_gbps | skipped_bytes | resent_bytes | verified_bytes | result |
|-----------|-----------------|---------------|--------------|----------------|--------|
| STOR resume | 7.17879 | 1048576 | 15728640 | 16777216 | pass |
| RETR resume | 4.86455 | 5242880 | 11534336 | 16777216 | pass |

## 初步判断口径

- `checksum=crc32c` 与 `checksum=none` 的差距用于判断 checksum 剩余开销。
- connections 扫描用于判断 socket/epoll 与并行度收益。
- chunk/buffer 扫描用于判断应用层 frame、manifest flush 和系统调用粒度。
- STOR 与 RETR 差异用于判断磁盘写入、磁盘读取和方向性调度开销。
- resume 样本的 `skipped_bytes` / `resent_bytes` / `verified_bytes` 用于确认恢复不是完整重传伪 resume。

最终 sha256 仍只是测试验收手段；内部恢复事实源是 manifest v2/download manifest 的 `verified_chunks`。

## 初步瓶颈判断

- 在 64MiB/128MiB smoke 上，CRC32C hardware 路径约 7.5-8.3Gbps，`checksum none` 约 12.4-16.9Gbps；checksum 仍是显著开销来源，尽管 Phase 2C hardware backend 已明显优于软件 CRC32C。
- 1/4 connections 在 smoke 样本中没有稳定带来线性提升，说明当前瓶颈不只是单连接 TCP 窗口，后续需要继续扫描 8/16 connections、larger buffer 和 chunk。
- 代表性 1GiB 样本下降到 STOR 0.656Gbps、RETR 0.973Gbps，明显低于 64/128MiB smoke。下一步应优先排查磁盘路径、临时文件落盘、cache/dirty writeback、manifest flush 和测试文件生成/读取路径，再评估 socket/epoll 或 io_uring。
- Phase 4A 结论暂不支持直接切 io_uring：先补齐 full/更多 1GiB 参数点和 fio/iperf 对照，确认瓶颈是否在存储或 checksum。
