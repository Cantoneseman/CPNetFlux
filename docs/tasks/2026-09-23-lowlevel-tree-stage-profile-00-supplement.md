# LOWLEVEL-TREE-STAGE-PROFILE-00：00 本地 WSL 补充观测

日期：2026-09-23（Asia/Shanghai）。本文件是 00 对 02 `BLOCKED` 回执的独立补充，不覆盖角色结果，也不授权 lookahead 实现。

固定输入为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 的源码；树目录代码与该提交一致。使用既有 WSL ext4 Release build（CMake 3.22.1、Ninja 1.10.1、g++ 11.4.0），不是云端构建。参数冻结为 anonymous、control reuse worker、scheduler off、compression off、checksum none、POSIX、1 MiB chunk、64 KiB buffer；源树为 32 个 1 MiB 文件，总 33,554,432 B。证据根：`/home/sumu/cpnetflux-evidence/LOWLEVEL-SENDV-IMPL-01-root-final-20260923/tree-stage/`。

00 在本地用固定基线 build 运行 upload 与 download，file parallelism 1/4 各 3 次。两方向各 6/6 返回 0；每个 case 32 files、33,554,432 B，tree summary hash 在同方向重复间一致。upload run elapsed：fp1 `1.49838/1.50121/1.50187 s`，fp4 `0.397249/0.400005/0.407730 s`；download：fp1 `1.62515/1.60242/1.57621 s`，fp4 `0.499105/0.491297/0.529533 s`。这些是本地 loopback 机制数据，不是跨域或 GridFTP 结果。

现有 event log 确实写出每个 `file_start`/`file_complete`，但所有文件事件的 `elapsed_seconds` 都是 `0`，时间戳只有秒级 UTC，`transfer_id` 为空；只在 `tree_complete` 记录 run 级 elapsed（上传示例 1.49838 s，下载示例约 1.6 s）。因此本轮仍无法可靠分离 EPSV/passive endpoint、TCP connect、150、SessionInit、first payload、payload I/O、226、manifest finalize 或 inter-file idle；不能从这些事件推断连接与前一文件 payload 是否重叠。

补充的 `strace -ff -ttt` upload/parallel trace 仅说明多个子进程/连接的 syscall 时间存在，不能安全映射到文件阶段；不把 syscall 行伪装成协议阶段事件。证据汇总：`dense-upload-baseline/summary-concise.json` SHA-256=`dac5f4ce5619a2c9874da3745252b36df4bb46ba07885f5ba13bfc19ada57f9e`；`dense-download-baseline/summary-concise.json` SHA-256=`b671886e43cdcf3773a50def12b2c900024271f278da669a784444516d2ef746`；`tree-stage/sha256-manifest.txt` SHA-256=`8f329a05b2061de0a784fa3e7814ff1197079a960addd33ede4d2bff7e2f8de6`。

结论：目录性能具备并发收益的可重复现象，但当前观测不足以证明 lookahead 或 manifest 优化的因果收益。下一项应派发“目录 telemetry instrumentation 契约与最小实现”任务，限定为同进程 monotonic clock、run/file/stream/worker 关联键和上述阶段边界；完成前不派 lookahead，不启动深圳—上海实验。云端仍受 LOWLEVEL-CLOUD-PREFLIGHT-01 `BLOCKED` 限制。
