# LOWLEVEL-DESIGN-QA-01：优化架构选型独立审查

路线/任务：`R2026-09-23.4 / v1`
审查日期：2026-09-23

## 输入与范围

- 任务输入与执行前实时 HEAD 均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，未 superseded。
- 执行前工作区已有共享文档修改和未跟踪任务文件；暂存区为空。审查未覆盖或改写这些文件。
- 审阅 03 的 `LOWLEVEL-DESIGN-01` 结果、路线决策、HIST-QA 与 DIR-PERF-SOURCE 结果，并定向核对 DATA 写入、tree worker、EPSV/被动 listener、现有 smoke 与 CMake 注册。
- 只新增本结果文件。无代码、测试、路线、BOARD/ROSTER 或 Git index 修改。

## 分项结论

| 项目 | 结论 | 独立判断 |
| --- | --- | --- |
| 历史证据边界 | PASS | HIST-QA 只把 dense fp1→fp8 的内部变化作为“低并行供给不足”的源码映射线索；同时保留旧行 `fail_correctness`、GSI/data privacy 与 anonymous/TLS-off 配置不匹配、payload 未由本轮重算等限制。它不证明当前瓶颈或跨系统性能优劣。 |
| 单文件 plain-TCP DATA vectored write | PARTIAL | 源码 spot-check 确认 STOR client 与 RETR sender 分开发送 header/payload，`FramedDataSocket::writeAll` 明文分支循环 `send(..., MSG_NOSIGNAL)`；现有 TLS 写路径不同。候选限制在两处 DATA callsite，保留 frame bytes、checksum/resume、默认值和 TLS 路径，短写/EINTR/零写/peer close 约束清楚且可回退。机制可证伪指标已提出，但没有 Linux syscall/CPU profile；不能称为已证实瓶颈或现在就批准优化代码。 |
| 目录 worker lookahead | PARTIAL | 一 worker 一组备用 EPSV/TCP sockets、独立已认证控制连接、至 STOR/RETR 150 后才进行 data TLS/session init 的边界，与当前每文件 transfer ID 和同步完成模型相容；规格要求不改变并行文件上限、manifest 时点，并有失败回退和 socket/control 清理约束。实际 backlog 等待和可重叠时间仍是待验证假设；必须先有冻结的阶段计时并证明 EPSV/TCP connect 位于关键路径。 |
| 跨文件复用 data TCP/TLS session | PASS（首项否决合理） | 当前 SessionInit 带每文件 transfer ID，STOR/RETR 同步运行至文件完成后才回 226。跨文件复用会改变 session/file/stream 归属、TLS、resume、取消和失败隔离，需另立版本化协议及新旧端协商/回退设计；不应混入这次兼容性优化。 |
| 增大默认 fileParallelism、manifest batching | PASS（暂缓合理） | 前者改变在途文件数及公平性，不能作为底层路径收益证据；后者会触及 durable checkpoint 与并发快照语义。目前没有 manifest 锁等待、序列化/写入耗时证据。 |
| 回归及回退设计 | PASS（仅规格） | 03 提出的 socketpair/短写/EINTR/peer-close 检查，以及真实 tree upload/download、TLS、resume、changed-file、首个/中间失败、取消和 hash/manifest 检查覆盖方向合适。现有控制复用 smoke 是真实 client/server loopback 并比较 tree hash/控制连接数；现有 resume smoke 覆盖上传下载并比较 tree hash。它们尚未覆盖新 lookahead 状态机，单文件也未发现现有 `FramedDataSocket` 专项测试。所有这些是拟议门禁，不能记作已执行通过。 |

## 实现前的最小门禁

1. **先冻结观测契约。** 由 01 定义各阶段的起止事件、关联键、timer 分母和失效规则；目录记录须能关联 worker/file/stream，并保留阶段重叠，不得把阶段时长相加冒充 wall time。实现并验证观测后，先用 dense fp1 数据判断 EPSV、TCP connect、TLS/session init、first payload、payload I/O、226 与 manifest finalize 的位置。
2. **单文件候选需先有本机 profile 证据。** 固定明文 DATA 字节数、frame 数和其他配置，在 Linux 上基线 profile；`strace -c` 汇总值需限定到客户端数据 fd/相关 syscall，并按 DATA frame 归一，避免把控制通道、服务端写入混作 DATA 成本。只有 syscall 负担值得优化时才下发窄实现任务。任务需包含确定性短写/错误测试，验证 partial iovec 只续写未发送尾部；TLS on/off file transfer、resume/checksum smoke；以 syscall/DATA-frame、CPU/GiB 和固定 A/B 为验收，未见机制或可测收益则回退。
3. **目录候选需阶段证据后单独立项。** 必须实证 EPSV/TCP connect 可与前一文件 payload 重叠且减少 inter-file idle/wall，而非仅有 worker 串行源码事实。实现任务限 scheduler off + 显式 worker control reuse、默认关闭、一槽 lookahead，不改变 active file 上限或 manifest 时点。
4. **目录真实集成测试需有明确状态断言。** 除多文件 upload/download、TLS on/off、resume 与 changed-file 外，至少覆盖备用控制认证/TLS失败、EPSV/backlog 过期、TCP 已连但尚未 STOR、STOR/RETR 150 后 TLS/session 失败、首个/中间文件错误、retry、max-files 与取消。断言每次只使用当前文件 transfer ID、活动文件数不增加、fallback 走旧序列、恢复后 payload/tree hash 与 manifest 一致，并通过可观察方式确认 standby fd 与 passive listener 已关闭。现有 smoke 可作基础，但不能替代这些新状态断言。
5. 先在 loopback 验证协议/清理，再固定数据与配置做本地 RTT/cap A/B；跨域 100 Mbps 对照仍需独立冻结 auth/data protection、timer、commit/build/binary hash 与环境准入。静态方案、旧历史数据和本地机制测试都不构成 100 Mbps 或 100G 性能验收。

## 关键证据定位

- 单文件源码：`src/core/io/file_transfer_client.cpp:321-335` 与 `src/core/io/file_download_sender.cpp:280-293` 分别调用 header `sendFrame` 和 payload `sendAll`；`src/core/io/framed_data_socket.cpp:75-101` 明文 `writeAll` 循环 `send` 并保留 `MSG_NOSIGNAL`，TLS 分支调用 `tls_.writeAll`。`connectFramedDataSocket` 在 `:140-167` 连接后立即完成 TLS，说明 lookahead 若预建明文 TCP fd，需在 150 后延迟执行 TLS wrap；现有 fd 接管入口见 `include/cpnetflux/core/io/framed_data_socket.h:14-16` 和 `include/cpnetflux/core/io/tls_socket.h:110`。
- Tree/server 生命周期：`src/core/io/tree_transfer_client.cpp:2334-2359` 显示 worker 处理完 `processFile` 才回队列取下一文件；`src/protocol/control/control_server.cpp:175-191` 按 connections 建 passive listener/backlog，`:635-693` 在该 control handler 生命周期内持有 listener 并处理 EPSV，`:696-703` 同步进入 STOR/RETR；STOR 的 150、数据完成和 226 位于 `:510-536`。这支持方案的独立 standby control 约束，但不证明客户端预连 socket 的排队时长/成功率。
- 真实现有 smoke：`tools/test/run_gridftp_tree_control_reuse_smoke.py:22-68` 启动真实 client/server，核对上传 tree hash 与连接数；`tools/test/run_gridftp_tree_resume_smoke.py:10-75` 做真实上传/下载中断后 resume 并核对 tree hash。CTest 注册在 `CMakeLists.txt:380-417`。这些是已有可用覆盖入口，不是本轮执行结果，也没有 lookahead 专项断言。
- 设计证据：03 结果中单文件/目录候选分别见 `docs/tasks/2026-09-23-lowlevel-design-01-result.md:24-39`、`:41-61`，依赖次序/实现门见 `:63-68`；历史证据限制见 `docs/tasks/2026-09-23-hist-qa-01-result.md:8-10,105-110`，目录源码映射及结论见 `docs/tasks/2026-09-23-dir-perf-source-01-result.md:3-5`。

## 总结与状态

**总体：PARTIAL；本轮不授权开始优化代码任务，也不授权实验。** 两个候选的机制、边界、主要兼容性风险和回退思路足以供 00/01 继续冻结门禁；但 03 的方案本身将单文件实现置于 Linux profile 之后、目录实现置于阶段观测之后，这些先决证据当前不存在。下一步由 01 冻结 timer/阶段观测契约，再安排只做本机基线观测的任务；按结果决定是否分别下发单文件或目录实现任务。跨文件 data session、默认并行度变更和 manifest batching 不在该实现任务范围内。

## 执行回执

- 输入/输出 HEAD：执行前后均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 实际改动：仅新增 `docs/tasks/2026-09-23-lowlevel-design-qa-01-result.md`；未操作暂存区。
- 实际命令与门禁：`git rev-parse HEAD`（0，前后相同）、`git status --short --branch`（0）、`git diff --cached --quiet`（0，index 空）、定向 `rg`/`Get-Content` 及源码/测试引用 spot-check（0）；写后 `git diff --check`（0，只有共享文档的既有 LF→CRLF 提示）、结果文件严格 UTF-8 解码与逐行尾随空白/末尾换行检查（PASS，空白 0）。本轮未调用 Codex CLI，因此没有 CLI last-message 文件。
- 构建、CMake、CTest、代码测试、smoke、profile、benchmark、传输、实验、SSH、云端和清理：均 `NOT_RUN`。
- 剩余风险：阶段观测契约与结果、Linux send syscall profile、新 lookahead 的 loopback 实现及真实回归、固定提交跨域准入尚未完成。
- 下一步：01 冻结阶段计时/事件契约；00 根据本机 profile 与阶段观测结果决定是否派发各自独立的窄实现任务。本回执不是产品验收或性能通过。
