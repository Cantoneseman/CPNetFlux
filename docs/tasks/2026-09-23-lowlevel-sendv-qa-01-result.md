# LOWLEVEL-SENDV-QA-01：plain DATA vectored write 独立质量复核

路线/任务：`R2026-09-23.4 / v1`
复核日期：2026-09-23

## 结论

**总体 PARTIAL。建议保留在隔离、未提交 worktree 供后续复核；目前不建议合入，也不把它表述为已证明的端到端性能优化。** 静态检查支持 vectored send 的数据与错误处理实现；00 报告的 Linux build、测试与 A/B 结果积极，但 04 无法读取 WSL 上的原始汇总/trace 文件独立核对，且报告中的 8-connection wall 中位数从 0.23 s 增至 0.25 s。需先能复核原始证据并厘清该 20 ms 变化相对样本噪声的意义，再由 00 决定是否保留到正式集成。

## 输入与实际工作区

- 任务指定输入基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。复核时实现 worktree HEAD 确为该提交，分支为 `codex/LOWLEVEL-SENDV-IMPL-01`；实现尚未提交。
- 实现 worktree 状态包含 7 个代码/测试/CMake 路径：`CMakeLists.txt`、`include/cpnetflux/core/io/framed_data_socket.h`、`src/core/io/framed_data_socket.cpp`、`src/core/io/framed_data_socket_internal.h`、`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_sender.cpp`、`tests/unit/framed_data_socket_test.cpp`。另有未跟踪的 `docs/tasks/2026-09-23-lowlevel-sendv-impl-01-result.md`，不属于代码差异。
- 共享文档仓库 HEAD 前后为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。根工作区既有共享修改保持原样。根与实现 worktree 暂存区均为空；本轮未改 Git index、源码、测试或任务板。
- 实现者结果记录源码快照归档 SHA-256 `d541882f34603aab82a97078cb1e821cb8030df0cffeb5692cecb099835282e9`、测试快照 SHA-256 `ce480050b934c50480b35117937a023943fd8e760d86d475c71f679532ebfec3`。这些 hash 来自其回执，本轮未能用底层归档文件复算。

## 源码与测试静态复核

**PASS：发送范围与字节推进。** `framed_data_socket.cpp` 的 `detail::writeSegments` 只提交非空 segment，使用两个 `iovec` 和 `sendmsg(..., MSG_NOSIGNAL)`；正数短写会逐段推进当前 base/length，正确跨过 header 边界；`EINTR` 重试，零写返回 runtime error，其它 errno 走 `systemStatus("sendmsg", errno)`。peer close 测试实际断言 `EPIPE` 且进程不因 SIGPIPE 终止。upload 与 download sender 仅在 DATA callsite 对明文 socket 且非空 payload 调此方法；TLS 和空 payload 走旧 header/payload 写路径。其它 frame、协议编码器、checksum/resume 及默认值没有被改动。

**PASS：wire bytes 的静态断言。** 新测试用真实 `encodeFrameHeader` 构造 DATA header；`EncodedFrameHeader` 是长度为 `kFrameHeaderSize` 的 `std::array`，常量为 64。测试将 socketpair 接收字节与编码后的 header 拼接 payload 逐字节比较。其余测试覆盖 header-only/payload-only、header 内短写、跨 header/payload 短写、payload 内短写、注入 EINTR、零写、peer close/EPIPE 和 `MSG_NOSIGNAL`。测试通过可控 sendmsg 函数指针制造短写，避免依赖调度碰巧产生短写。

有一项范围备注：`writeSegments` 声明放在 `include/.../framed_data_socket.h` 的 `FramedDataSocket` 公共类中，因而是可见的类成员；若该头文件属于稳定外部 API，应额外确认此成员被视为内部/非稳定接口。它不是本次数据正确性 blocker。

**PARTIAL：TLS、transfer 与 checksum/resume 运行验证。** 源码显示 TLS 路径继续使用已有 `sendAll`，且 sendv 不触碰 checksum/resume 控制流程。00 的结果称 TLS-enabled Release build 成功，file transfer/resume/checksum smoke 通过；但专门的 data TLS smoke、完整 CTest 本轮/该固定结果都没有完成。故 TLS 行为目前只有源码隔离与编译配置佐证，不能写成 TLS smoke 通过。工作目录里的单测源码及上述两个 untracked 文件以严格 UTF-8 解码、尾随空白扫描为 0；tracked diff 的 `git diff --check` 退出码为 0。

## 00 汇总数据的独立算术复核与证据等级

任务指定 ext4 证据根为 `/home/sumu/cpnetflux-evidence/LOWLEVEL-SENDV-IMPL-01-root-final-20260923/ab`。本轮尝试 `wsl.exe --exec bash -lc ...` 读取 `summary.json`/`results-corrected.csv`，退出码 1，返回 `E_ACCESSDENIED`；再尝试 `\\wsl$\Ubuntu-22.04\home\sumu\...` 只读文件共享也被拒绝（命令退出码 1）。所以以下数值是根据 00 写入实现回执的汇总做算术复核，不是 04 对原始 CSV/trace 的独立重算；24 个进程退出码、hash 和实际 trace 明细均标为 **reported / raw evidence unknown**。

在回执给出的每次 4,096 个 DATA frame 前提下，计数关系自洽：

| 指标 | baseline | sendv | 由回执数值复算 | 独立性 |
| --- | ---: | ---: | ---: | --- |
| DATA 写调用/每轮 | 8,192（4,096×2） | 4,096 | 减少 4,096，−50% | 汇总算术通过；原始 trace 不可读 |
| peer send 调用中位数，1 connection | 8,707 | 4,611 | 减少 4,096，−47.04% | 数字来自 00 汇总 |
| peer send 调用中位数，8 connections | 8,728 | 4,632 | 减少 4,096，−46.93% | 数字来自 00 汇总 |
| system CPU，1 connection | 0.76 s/GiB | 0.56 s/GiB | −26.32% | 根据每 256 MiB 的 0.19→0.14 s 换算 |
| system CPU，8 connections | 0.92 s/GiB | 0.76 s/GiB | −17.39% | 根据每 256 MiB 的 0.23→0.19 s 换算 |
| wall 中位数，1 connection | 0.27 s | 0.21 s | −0.06 s（−22.22%） | 数字来自 00 汇总 |
| wall 中位数，8 connections | 0.23 s | 0.25 s | +0.02 s（+8.70%） | 数字来自 00 汇总；不能忽略此方向相反的变化 |

据 00 回执，固定矩阵使用同一 256 MiB payload、1 MiB chunk、64 KiB buffer、plain TCP、checksum none，1/8 connections 各 3 次；6 个 wall case 与 6 个 client-only trace case 共 12 对、24 个 client/server 进程均 exit 0，目标 SHA-256 都是 `545a77fd421938559bd8fff15e56493a4fa27d2c5961a661fdc3f6ad7fa8251f`。新 trace 每帧两个 iovec 长度为 64 与 65,536，报告返回 65,600 bytes 且没有 short write。**以上结果与目标机制一致，但无法检查每行返回值、hash 字段及 binary/source hash 链条；证据 hash 清单也在 00 回执中仍标为待生成。**

样本量仅三次/格，wall 本身约四分之一秒，汇总没有给出各次观测与离散度。1 connection 的 wall 有下降；8 connections 的中位数则上升 20 ms（8.7%）。因此 syscall 确实是汇总所示的强机制变化，CPU/GiB 也在两档下降，但不能用 CPU 数字抵消 8-connection wall 的反向结果，也不能据现有摘要判定它是真回归还是 loopback 小样本噪声。

## 证据结论与下一步

- **代码/字节/错误路径：PASS（静态复核）。** 未发现 DATA header 重发、payload 重复、TLS 降级或范围越界。00 报告 `FramedDataSocketTest 6/6` 及功能 smoke 通过；04 没有运行这些测试。
- **固定 build 与功能测试：PARTIAL。** 00 的结果记录 Release build 111/111、单测 6/6、file smoke 3/3、tree/resume/control-reuse/changed-file/edge/manifest smoke 8/8；这些为 00 的实际运行报告，不是 04 独立运行，也因日志所在证据根不可读而未逐项复核。Data TLS smoke 和完整 CTest 未运行；不将其列为 pass。
- **trace、hash 与退出状态：PARTIAL / raw evidence unknown。** 由摘要可复算的调用数与百分比自洽，但原始 `summary.json`、CSV、trace、退出码清单和最终证据 hash 清单均未能读取，不能独立验真 24/24。
- **性能解释：PARTIAL。** 结果支持“DATA socket 调用数约减半，loopback system CPU/GiB 汇总值下降”；8-connection wall 中位数 `0.23→0.25 s` 阻止宣称该档 wall 改善。此 loopback、plain TCP、checksum none、小样本测试不能证明 100 Mbps 跨域收益、GridFTP 等价，也不能外推到 100G。
- **保留建议：** 暂留该隔离 worktree，不合并、不扩大功能范围、不启动云端工作。待 00 提供可供只读复核的完整 evidence manifest、CSV/trace 与逐次 wall 值后，确认 binary/source 归属、所有 hash/exit 状态及 8-connection wall 波动；data TLS smoke 若是发布门也须另有实际结果。最终保留/回退由 00 收口。本报告不决定目录 lookahead。

## 执行回执

- 实际输入/输出：共享文档 HEAD 前后均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；只读实现 worktree HEAD 为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`，保持未提交。
- 实际改动：仅新增本文件及 `docs/tasks/2026-09-23-lowlevel-sendv-qa-01-last-message.md`；未改源码、测试、其他结果、BOARD/ROSTER 或 index。
- 实际命令/退出码：根与 worktree `git rev-parse HEAD`（0）；两处 `git status --short --branch` / index quiet 检查（0；工作区按任务预期 dirty、index 空）；worktree `git diff --check`（0，只有 tracked CMake/header 的既有 LF→CRLF 提示）；源码/结果定向 `rg`、`Get-Content`（0）；两个 untracked C++ 文件严格 UTF-8/尾随空白扫描（0，空白 0）；WSL 读取和 `\\wsl$` 只读访问（均 1，`E_ACCESSDENIED`）。
- 构建、单测、CTest、smoke、传输、strace、benchmark、云端/SSH：04 均未运行；引用的 00 运行状态已明确标成报告来源。
- 剩余风险：底层 A/B 原始数据与 artifact hash manifest 无法由本轮读取；8-connection wall 中位数上升的噪声范围不明；专门 data TLS smoke 未运行。
- 下一步交接：00 使 ext4 摘要、逐次结果、raw trace 与 evidence manifest 可由 04 只读复核，再收口隔离 worktree 的保留/回退决定。无论本地结论如何，100 Mbps/GridFTP 与 100G 结论仍须各自正式准入。
