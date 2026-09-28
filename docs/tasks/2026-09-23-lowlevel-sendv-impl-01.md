# LOWLEVEL-SENDV-IMPL-01：plain DATA vectored write

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：04 测试与质量，最终由 00 收口
- 目标及理由：依据 `LOWLEVEL-SINGLE-PROFILE-00` 的固定提交 profile，将 plain TCP 单文件 DATA 的 64-byte header 与 payload 从两个 socket 写合并为一次 vectored write，减少每 DATA frame 的系统调用；不把 loopback 结果写成跨域性能结论。
- 非目标：不改 wire format、协议版本、默认值、TLS 写路径、checksum、resume、chunk 调度、目录 worker、manifest、scheduler、io_uring、GridFTP、云端目录或 runner。
- 输入 commit：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；实现前必须记录实时 HEAD/status；源码相对该提交不得混入其他文档改动。
- 必读资料：本任务、`docs/tasks/2026-09-23-lowlevel-single-profile-00-result.md`、`docs/DECISIONS/2026-09-23-lowlevel-design.md`、`docs/tasks/2026-09-23-lowlevel-design-01-result.md`、`docs/tasks/2026-09-23-lowlevel-design-qa-01-result.md`。
- 允许修改：03 独立 worktree 中 `include/cpnetflux/core/io/framed_data_socket.h`、`src/core/io/framed_data_socket.cpp`、`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_sender.cpp`，以及为该 API/错误语义所需的窄单元测试和 CMake 测试注册。共享工作树、BOARD、ROSTER、角色回执和 Git index 禁止修改。
- 工作分支/worktree：`codex/LOWLEVEL-SENDV-IMPL-01`；独立 worktree，不在 `D:\Project\CPNetFlux` 根目录切分支。不要提交 Git。
- 前置条件：profile 已证明 4,096 DATA frames 各发生 header/payload 两次 send，1/8 connections 无 short write；当前路线未放行目录实现。

## 实现约束

1. 新内部 API 只对 plain TCP 生效，例如接收已编码 header 与 payload 两个只读 segment；TLS 或空 payload 必须回到现有 `writeAll`/`sendFrame` 路径。
2. `sendmsg`/`iovec` 的 partial write 只能推进尚未发送的 segment；EINTR 重试；零写、其他 errno、peer close 沿现有错误语义返回；不得重复 header 或 payload，不得复制整个 payload。
3. upload `file_transfer_client.cpp` 与 download `file_download_sender.cpp` 仅 DATA frame 改走新 API；SessionInit、ChunkComplete、Fin、Complete 等帧保持旧路径。
4. 编码后的字节序列必须逐字节等于旧的 `FrameHeader || payload`；保持 `MSG_NOSIGNAL` 等现有信号语义。非 Linux 或 TLS 编译路径不能破坏。

## 测试与验收命令

- socketpair 或等价可控测试：完整写、header-only/payload-only、header partial、payload partial、跨 iovec partial、EINTR、零写/peer close；测试必须验证接收字节逐字节一致，不能只断言返回码。
- 在独立 Linux build 中运行：
  - `cmake -S <src> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF -DCPNETFLUX_ENABLE_TLS=ON`
  - `cmake --build <build> --parallel 2`
  - `ctest --test-dir <build> --output-on-failure -R 'cpnetflux_file_(transfer|resume|checksum)_smoke'`
  - tree/control reuse、tree resume、changed-file、edge cases、data TLS smoke（若依赖可用）也必须列出真实通过/失败/blocked，不以 skip 当 pass。
- 固定 256 MiB、buffer 64 KiB、chunk 1 MiB、plain TCP、checksum none、connections 1/8，各至少 3 次 before/after；client-only `strace -ff -yy` 统计 data peer 的 frame header/payload 调用数、returned/requested bytes、short/unresolved、CPU 秒/GiB 和 wall/goodput。原始 trace 和 hash 放任务专属 ext4 证据目录，不放仓库。
- 通过门：协议 bytes、目标 hash、resume/checksum/TLS 行为不变；DATA write 调用数应从约 `2 * 4096` 降到接近 `4096`；若 syscall 下降但 CPU/GiB 和 wall 在重复噪声内无收益，报告为无可测收益并建议回退，不宣称优化成功。

## 交付

只写 `docs/tasks/2026-09-23-lowlevel-sendv-impl-01-result.md` 和 CLI 摘要 `docs/tasks/2026-09-23-lowlevel-sendv-impl-01-last-message.md`；结果包含输入/输出 commit、worktree、修改文件、完整命令/退出码、测试状态、before/after 指标、证据路径、失败与回退建议。禁止 SSH、云端构建/实验、清理历史目录、凭据操作和提交 Git。

