# LOWLEVEL-SINGLE-PROFILE-00：00 总指挥复核结果

- 路线/任务版本：`R2026-09-23.4 / LOWLEVEL-SINGLE-PROFILE-01 v1`
- 执行者：00 总指挥本机 shell；02 原 CLI 轮次的 `E_ACCESSDENIED` 结果保留，不覆盖
- 结论：**单文件明文 DATA vectored-write 候选通过 profile 门，可派窄实现；目录 lookahead 仍需独立阶段观测。**

## 固定输入与环境

- 源码输入：实现提交 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 的 `git archive`。
- source archive SHA-256：`bbf320f20230700d61f318c9081694f00294044ae39a5ed308bfcdbc01bf1b5e`。
- 输出文档 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；源码相对 `3b0820d` 无差异；暂存区为空。
- Linux：WSL Ubuntu-22.04，ext4 `/dev/sdd`，约 951 GiB 可用；CMake 3.22.1、Ninja 1.10.1、g++ 11.4.0、strace 5.16；zlib 1.2.11、OpenSSL 3.0.2、spdlog 1.9.2、GTest 1.11.0。
- 证据根：`/home/sumu/cpnetflux-evidence/LOWLEVEL-SINGLE-PROFILE-01-root-20260923-192000`。该目录保留在 WSL ext4；未使用云端、SSH、GridFTP 或历史 payload。

## 构建与安全门

- CMake：`Release`、`CPNETFLUX_BUILD_TESTS=ON`、`CPNETFLUX_ENABLE_IO_URING=OFF`、`CPNETFLUX_ENABLE_TLS=ON`，Ninja build 110/110 成功。
- `cpnetflux-file-client` SHA-256：`4be0978b01b16c9d21dc6f47d3d73e1755f9b3ea3dee625fdd1af57d4aa04e21`。
- `cpnetflux-file-server` SHA-256：`5b5f8664553d81a80a635eeac62c1c01bb33139e10a7d2398cb02aee10aca20b`。
- `ctest --test-dir <build> --output-on-failure -R 'cpnetflux_file_(transfer|resume|checksum)_smoke'`：3/3 通过；复核轮次再次 3/3 通过。
- 256 MiB plain TCP、TLS off、checksum none、chunk 1 MiB、buffer 64 KiB；源文件 SHA-256：`545a77fd421938559bd8fff15e56493a4fa27d2c5961a661fdc3f6ad7fa8251f`。

## 实际 profile

1. client-only `strace -ff -yy`，只统计发往数据 peer 的 `sendto/sendmsg/write/writev`；1 和 8 connections 各 3 次。
2. 每次 4,096 个 DATA frame，4,096 个 64-byte frame header 写调用和 4,096 个 payload 写调用；1 连接 peer sendto 总数为 8,707，8 连接为 8,728（其余为会话/完成帧）。
3. 1/8 连接所有重复均 `requested_bytes == returned_bytes`，`short_calls=0`、`unresolved_calls=0`；目标文件 12 个均与源文件 hash 一致。
4. 未跟踪 wall/CPU：1 连接 elapsed 中位数 0.28 s、sys CPU 中位数 0.12 s；8 连接 elapsed 中位数 0.36 s、sys CPU 中位数 0.17 s。loopback 数值只用于机制对照，不外推 100 Mbps。

## 验收判断与下一步

- 证据确认“每个 plain DATA frame 目前至少两次 socket 写”的机制假设，并且可按 frame 归一；profile 不证明该项是跨域主瓶颈。
- **批准** `LOWLEVEL-SENDV-IMPL-01`：仅把 plain TCP DATA header+payload 合并为一次 `sendmsg`/等价 vectored write；TLS 继续旧路径，协议 bytes、checksum/resume、错误语义和默认值不变。
- 实现验收必须证明短写/EINTR/零写/peer close、协议字节完全一致，TLS on/off、resume、checksum smoke 通过，并用同一固定 case 做 before/after syscall、CPU/GiB、wall A/B。没有可测收益或发生回归则回退。
- 目录 lookahead 未获授权；须先取得 EPSV/TCP connect、首 payload、226、manifest finalize 和 inter-file idle 的阶段数据，再单独派任务。

## 限制

本轮没有跨域传输、GridFTP 对照、目录阶段 profile 或源码修改。末尾复核脚本的文件索引命令有 Windows CR 字符导致退出码 1，但发生在所有 case、hash、CTest 和 trace 生成之后；证据根、二进制、目标文件和 `sha256-manifest.txt` 已独立存在并复核。该命令错误不作为测试通过依据。

