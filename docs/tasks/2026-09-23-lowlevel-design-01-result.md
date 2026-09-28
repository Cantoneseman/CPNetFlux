# LOWLEVEL-DESIGN-01：单文件与目录底层优化选型结果

日期：2026-09-23（Asia/Shanghai）
路线/任务：`R2026-09-23.4 / v1`。本文是固定源码上的方案比较，不代表实现、测试或性能验收。

## 输入核验与证据等级

- 执行前 `git rev-parse HEAD` 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，符合任务输入。最近实现基线仍记为 `3b0820d`；`git diff --name-only 3b0820d HEAD -- src include` 无输出，本任务涉及的源码与 HEAD 一致。当前共享工作区有 00/02/03/04/05 文档修改和未跟踪任务/决策，暂存区为空；不操作这些路径或 index。
- 已读任务、`docs/DECISIONS/2026-09-23-lowlevel-design.md`、HIST v2 索引、04 `HIST-QA-01` 独立结果和 03 `DIR-PERF-SOURCE-01` 源码映射；源码定向读了 tree/control server、framed socket、单文件 STOR/RETR sender、manifest、测试与 CMake 注册。
- **历史直接观察（旧构建，仅供优先排序）：** 04 复算 dense 深圳→上海、128×1 MiB upload 的 CPNetFlux client-process goodput 中位数 fp1≈18.106 Mbps、fp8≈101.431 Mbps，约 5.60 倍；GridFTP 同格约 93.516/95.170 Mbps。04 明确标为内部并发响应诊断：双方 GSI/data privacy 与 anonymous/TLS off 不等价，CPNetFlux 旧 CSV 行标有 `fail_correctness` accounting/evidence 缺口，payload 未在 QA 中重算 hash。不能当公平性能比，也不是当前 HEAD 的复现结果。
- **源码直接事实：** 单文件 DATA 帧在明文传输下分别调用 `sendFrame(header)` 和 `sendAll(payload)`；`FramedDataSocket::writeAll` 对 plain TCP 循环 `send`。Tree worker 在单文件引擎、控制 226 和 completed manifest 更新返回后才 dequeue 下一项。普通 tree 状态保存持 scheduler mutex 序列化并原子替换完整 manifest。
- **待测假设：** 当前证据没有分阶段时间、CPU/syscall profile 或匹配安全配置的跨域样本。无法判定单文件受 send syscall 限制，也无法判定目录受控制 RTT、data connect、server finalize 或 manifest 锁写限制。

## 候选比较

| 候选 | 当前依据 | 兼容性与代价 | 选型 |
| --- | --- | --- | --- |
| 明文 DATA header+payload vectored write | `file_transfer_client.cpp:321-335` 和 `file_download_sender.cpp:280-293` 分开写 DATA header 与 payload；socket 明文 `writeAll` 每段调用 `send`，通常每 DATA 至少两次 send。默认 buffer 64 KiB，256 MiB 单文件约 4096 个 DATA payload 帧。 | Linux `sendmsg`/iovec 可把同一 header 和 payload 作为一个有序字节流写出；需正确推进短写的多段 iovec、重试 EINTR、保留 `MSG_NOSIGNAL`。无 wire 改动、无额外 payload copy。TLS 必须保留现有 TLS write 路径，不能把明文 `sendmsg` 用到 TLS fd。 | **单文件首选，条件实施。** 机制可在本地验证，性能收益仍待 profile 和 100 Mbps A/B。 |
| 每 worker 一个待用 EPSV/TCP endpoint（有界 lookahead） | Tree 每 worker 同步跑完一个 file lifecycle 再取下一个（`tree_transfer_client.cpp:2343-2359`）；当前每文件 EPSV、STOR/RETR 与每 stream TCP connect 顺序发生。旧 dense fp1/fp8 内部斜率支持调查供给间隙，但不定位阶段。 | 每 worker 交替使用两个已认证控制连接：当前控制连接执行文件传输时，备用连接只执行 EPSV，并将下一 endpoint 所需的 `connections` 个 TCP socket connect 到 passive listener；此时不发 STOR/RETR、不发 SessionInit、不写 manifest。当前文件收到 226 后，备用连接发 REST（resume 时）和 STOR/RETR，取得 transfer ID，再开始 data TLS/SessionInit 和传输。原控制连接转为新的备用连接。保留每 worker 同时活动文件数、每文件 transfer ID 与 wire；控制连接数增至约每 worker 两条，data socket 仍每文件重建。需要额外 socket adoption/TLS-wrap 内部 API、lookahead 生命周期与超时/失败处理。 | **目录首选，先测后实现；默认关闭，首批限 scheduler off + 显式 worker control reuse。** 仅当观测证明 EPSV/TCP 建连位于关键路径且可与前一文件 payload 重叠时推进。 |
| 跨文件复用 framed data TCP/TLS session | 每条连接启动时携带 `SessionInit.transferId`；上传 receiver 校验 transfer ID（`file_transfer_server.cpp:386-412`），下载 client 也按 `options.transferId` 校验（`file_download_client.cpp:211-247`）。服务器每次 STOR/RETR 消费一个 listener 并运行一个 file transfer；完成后控制 server 才返回 226（`control_server.cpp:521-536,597-611`），file server 等本文件配置的 stream FIN 后结束。 | 当前 socket 生命周期只能归属一个 file transfer。跨文件复用要求 server 持有长连接并重新建立每文件 session，定义新 transfer 边界、复用连接上的 stream/file 归属、错误隔离及 final status；也必须解决 TLS session 生命周期、resume ranges、取消和重试。需协议版本/能力协商与 v1 fallback。 | **当前拒绝为首项。** 收益机制可能更大，但不是兼容的内部微优化；只有 preconnect A/B 显示 TCP/TLS setup 仍占显著比例时，另立 v2 wire 设计与升级/回退任务。 |
| 让一个 worker 同时完整传多个文件 | 高 fp 的旧结果速度显著提高；当前已有 `fileParallelism` 控制多个 worker。 | 这会提高有效在途文件数，本质上利用并发隐藏固定成本；若仅把默认 parallelism 调高，不能证明底层路径改善，还会改变带宽公平性、资源占用和尾部行为。 | 不作为独立优化或默认值修改。只有显式、有界、可配置的并发实验可作为后续矩阵的一格。 |
| 批量/延迟 tree manifest checkpoint | `updateRecord` 与 `updateRecordForTransfer` 在 mutex 内各保存完整 manifest（`tree_transfer_client.cpp:1068-1093`；`tree_manifest.cpp:407-435`）。 | 降低 flush 频率或移出锁可能改变崩溃后 transfer ID/status 的最新恢复点；并发快照还需防止旧状态覆盖新状态。当前没有 mutex wait/serialize/write 耗时。 | 暂缓。先量化锁等待、每次 serialize/write/rename、manifest bytes；不先减少 durable checkpoint。 |

## 单文件建议：明文 DATA vectored write

**首选次序。** 先做 Linux 本地机制确认，再发窄实现任务；它不依赖旧跨系统吞吐结论。将 framed socket 增加一个“写两个 segment”内部操作，STOR client 和 RETR sender 仅 DATA header/payload 改走 plain TCP `sendmsg`。短写时逐段推进 iovec 游标直到两段全部发送；EINTR 重试；零写或其它 errno 按当前 `writeAll` 失败语义返回。ChunkComplete/SessionInit 等小帧和其它路径先保留现状，防止把改动扩成帧发送器重构。TLS 情况调用原有两段 TLS write，不做明文降级。

预计机制收益是 plain TCP 下每 DATA 帧常见的两个 send 系统调用变为一个 sendmsg；以默认 64 KiB buffer、256 MiB 文件为例，机制目标约少 4096 次发送 syscall。它不减少 bytes、checksum、read/write 总字节或数据 RTT，也不保证减少 TCP packet 数。收益指标依次为 syscall/DATA-frame、CPU 秒/GiB、固定单文件 CPU time 和端到端 goodput。若 syscall 数未接近减半，说明短写/调用层或统计口径与假设不符；若 syscall 数下降但 CPU/GiB 和固定链路 goodput 差异落在噪声内，则保留为无可测收益的优化并回退。

复杂度低到中。认证使用独立控制通道，不受此改动影响；data TLS 必须完整覆盖且行为不变。协议兼容要求编码后 `FrameHeader || payload` 字节完全相同，server 仍按同一 header/payload 解析。partial write 后只能续写未发送 iovec 尾部，不能从头重发或改走整帧 fallback，以免重复字节。

**本地机制验收建议：** 新增 `FramedDataSocket` socketpair 测试，核对拼接字节经现有 frame decoder 可还原；小 `SO_SNDBUF` 加并发慢 reader 覆盖短写；覆盖 EINTR（若测试 seam 可控）、peer close、零长度第二段与不触发 SIGPIPE。以 `strace -f -c -e trace=sendto,sendmsg,write` 跟踪 file smoke 比较每 DATA frame syscall 数。未来 Linux build 的定向回归命令：

```sh
<build-dir>/cpnetflux_unit_tests --gtest_filter='FramedDataSocketTest.*'
ctest --test-dir <build-dir> --output-on-failure -R 'cpnetflux_(file_(transfer|resume|checksum)|gridftp_data_tls)_smoke'
```

现有 CMake 入口在 `CMakeLists.txt:332-341,380-381`。同源 256 MiB 单文件 upload/download、固定 connections/chunk/buffer/checksum/TLS 分别做旧实现与候选 A/B；测试和 socketpair 只能验证字节和短写机制。CPU/GiB 的可测性取决于 Linux 工具链，真实 100 Mbps 端到端吞吐和有效匹配 GridFTP 比值仍必须等认证/data TLS/计时与环境 gate 冻结后跨域确认。100G 不能由本轮或本地 syscall 结果外推。

## 目录建议：一个待用 endpoint 的 worker lookahead

这个候选比保留跨文件 data session 风险低，并保持 wire format。Server 在收到 EPSV 后将 `PassiveListener` 留在该 control handler 的局部对象中，继续读该 control session（`control_server.cpp:635-693`）；listener backlog 按 per-file connections 创建（`:175-191`，`socket_utils.cpp:93`）。客户端可在另一条已认证 control connection 上先取得 passive port，并尝试建立一个 per-stream TCP connection；之后再发 REST/STOR/RETR。TCP handshake 是否能稳定排队至 server 开始 accept，必须由 loopback smoke 验证，不能只由源码推出。Server 只有 STOR/RETR 后才发 150 并进入同步数据处理；该命令所在控制连接直到整个文件处理后才回 226（`:696-704`、`:521-536`、`:597-611`），所以备用控制连接必须与当前传输控制连接分开。

一个 worker 最多保留一组未使用的 passive endpoint/socket；它不是活动 file transfer，不预先分配 transfer ID、不更新 manifest、不发任何 framed data。文件被选中后仍按当前顺序检查本地/远端元数据；resume 的 REST 只在即将 STOR/RETR 的备用控制连接上发送。取得本文件 transfer ID 后，才将已连接 fd 包装为 `FramedDataSocket`：明文直接接管；data TLS 模式在 STOR/RETR 150 之后用现有 `TlsClientContext::connect(fd, host)` 完成 TLS handshake，再照旧发送 SessionInit/ResumeResponse。配置的并行 stream 为 N 时预建 N 条 TCP socket；不得改变 max active file transfer 或 fileParallelism。

这能从下一文件关键路径上搬走 EPSV 往返与 TCP connect；仍不能重叠 STOR/RETR 的 150 往返、TLS handshake、SessionInit/ResumeResponse、文件 payload、服务器 finalize 与 226。启动 standby control 的认证成本须与当前 file payload 并行；若未及时 ready，按原路径建连接，绝不等待一个未完成的 lookahead 才开始文件。该候选预期节省上限是测得可被重叠的 EPSV+TCP connect 时间，不足以先验解释旧 fp1→fp8 的约 5.6 倍差距。

复杂度中到高，主要在 tree worker 预取线程/取消、`UniqueFd` 交接和 data TLS 延迟包装，不在 wire。控制 auth/TLS 必须按同一配置建立第二条控制连接；不能从当前控制连接复制认证状态。每个 worker 额外占用一条控制连接和最多 N 条已连接 socket；过期 endpoint、被服务器关闭的 listener、目标文件变化与远端 passive port/ backlog 限制都会令预热失效。仅 `control_reuse=worker` 显式开启时试行；全局 scheduler、control reuse off、scheduler 压缩重试先走原实现，待分别设计覆盖。

失败与恢复约束：预热失败或过期且还未发 STOR/RETR 时，关闭所有预建 fd/listener-control，并回退到现有 EPSV→REST→STOR/RETR→connect。STOR/RETR 已返回 150 后若 data/TLS/session 失败，沿现有 550/226 收敛与 resume/retry 路径处理，不能悄悄另起同一 transfer ID 的第二个 data session。任何 retry 丢弃旧 endpoint，重新 EPSV/TCP connect；不得将未发送 SessionInit 的准备 socket当作已验证 transfer。worker 停止、run 首错、max-files、取消或进程 teardown 时先关闭 standby sockets，再关闭 standby control，使 server 持有的 PassiveListener 释放；server 被动端口不得因取消残留。source/dest metadata 在延迟 start 前复核，预热不占用 `maxFiles` 计数、不改变 worker 取队列顺序。当前 manifest 的 transferring/completed durable 更新仍在既有时点执行，不能为了 pipeline 批量延迟。

**可量化与证伪。** 先在 loopback 固定数据集跑串行旧路径和 lookahead 路径，再在 Linux namespace/veth 上按 0/10/25 ms RTT 与 100 Mbps egress cap 做 A/B；保持 fileParallelism、per-file connections、worker reuse、auth/data TLS、文件清单和 checksum 相同。记录每文件 EPSV/TCP connect 的原始持续时间、lookahead ready 时刻、inter-file idle gap、file-start→first-payload、run wall、file count、失败/retry、峰值 active file 数。成功机制必须显示 per-file EPSV/TCP 仍发生但其大部分持续时间落在前一文件 payload 区间内；峰值 active files 不得超过原 `fileParallelism`。若 ready 常晚于前一文件完成、idle gap/客户端 wall 没有超过测量噪声的下降，或额外 auth/socket 开销更大，则拒绝该优化。真正的 100 Mbps 深圳—上海吞吐与 GridFTP 配对收益只能由后续固定 commit、相同认证/data privacy、完整证据的跨域矩阵确认；本地仿真只证明时间重叠机制。

**本地回归建议：** 扩展真实 tree control reuse smoke，覆盖 Worker 模式下多文件 upload/download、每 worker 最多一个 standby endpoint、当前 active file 上限未变、数据 TLS on/off、resume、changed file、首文件/中间文件失败、取消时无 passive listener/socket 残留。特意构造 TCP connect 已成功但 server 尚未发 STOR 的等待场景；随后验证 STOR 后 session transfer ID 正确、hash 与 manifest 一致。测试新建备用 control 失败和 socket 被拒绝时必须走原路径。定向 CTest 命令建议：

```sh
ctest --test-dir <build-dir> --output-on-failure -R 'cpnetflux_(tree_(upload|download|resume|parallel|control_reuse|changed_file|edge_cases)|gridftp_data_tls)_smoke'
```

对应 CMake 入口见 `CMakeLists.txt:388-417,380-381`。若真实失败恢复 smoke 不可执行，不得以 mock/parser 单测替代整条 tree/data/control 链路验收。

## 次序、回退与任务拆分

1. 先冻结并实现阶段观测，使 dense fp1 可区分 EPSV、STOR 150、TCP connect、TLS/session init、首 payload、226、manifest lock/save；timer 契约由 01 冻结。旧 evidence 只能说明并发响应，不能跳过观测。
2. **单文件实现先行：** 若 Linux profile 确认 DATA send syscall 占用值得优化，派独立 `LOWLEVEL-SENDMSG-IMPL`：仅 `FramedDataSocket` plain TCP 两段写 API及 upload/download DATA 两处 callsite、socketpair C++ 测试/CMake 注册。默认值、TLS、frame bytes、checksum/resume 不变。若每帧 syscall 没减少或 CPU/GiB无可测改善，revert 该 commit；无远端部署兼容负担。
3. **目录实现其次：** 若阶段数据确认 EPSV/TCP connect 是可重叠且占比显著的空档，派独立 `LOWLEVEL-TREE-PRECONNECT-IMPL`：只实现 scheduler-off + explicit worker reuse 的一-slot lookahead，新增 opt-in 且默认 off；涵盖 client fd prepare/adopt、TLS 延迟握手和真实 tree smoke。先通过 local loopback，再做可控 RTT；无收益或失败/取消泄漏就保持 flag off/回退旧串行路径。不要把 preconnect 宣称为跨文件 socket reuse。
4. 仅当 measured TCP/TLS handshake 本身仍支配目录 critical path，另开 `DATA-SESSION-V2` 设计任务：冻结协议版本/能力协商、v1/v2 双端部署与 rollback、transfer-id/session/file 边界、TLS、resume/partial failure 与旧客户端服务器互通矩阵后才准实现。manifest batching 需单独先完成耗时归因与崩溃恢复规格。

## 执行回执

- 输入 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；输出 HEAD 应保持不变。最近实现基线 `3b0820d` 到 HEAD 的相关 `src/include` 差异检查为空。
- 改动：仅新增 `docs/tasks/2026-09-23-lowlevel-design-01-result.md`。按派单调用 `codex exec --ephemeral --output-last-message docs/tasks/2026-09-23-lowlevel-design-01-last-message.md ...`，命令退出码 1，未生成该文件。未改源码、测试、runner、决策、BOARD/ROSTER、角色回执或原始证据；未操作 Git index。
- 实际命令：`git rev-parse HEAD`（0）、`git status --short --branch`（0）、`git diff --cached --name-only`（0，空）、`git diff --name-only 3b0820d HEAD -- src include`（0，空）、`git diff --exit-code 3b0820d HEAD -- src include`（0）、定向 `rg`/`Get-Content`、`git diff --check`（0）；`codex exec --help` 与 `codex exec resume --help` 均为 0。`codex exec --ephemeral --output-last-message ...` 为 1：CLI 报告 `%USERPROFILE%\\.codex\\state_5.sqlite` 只读，随后 app-server 初始化因拒绝访问失败。
- 选型：单文件首选 plain-TCP vectored DATA write（待 syscall/profile gate）；目录首选一个 standby endpoint 的 worker lookahead（待 setup critical-path gate）；两者没有性能结论。
- TLS、认证、协议、校验/resume 风险及测试/回退边界见上。编译、CMake、CTest、代码测试、smoke、benchmark、传输、profile、实验、SSH、云端与清理全部 `NOT_RUN`。
- 下一角色可直接执行：04 独立审查两项候选的兼容性、收益证伪指标、失败取消与回归边界；审查通过后由 01/00 冻结阶段 telemetry 后再选择实现任务，不得把本报告当实现批准。

## 写后检查

- 报告以严格 UTF-8 解码成功、末尾有换行；写后空白扫描曾发现元数据一处尾随空格，已移除，最终扫描为 0。`git diff --check` 退出码 0；其 CRLF 提示只涉及工作区既有的共享文档。
- 最终核验 `git rev-parse HEAD` 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，`git diff --cached --quiet` 退出码 0，`git status --porcelain -- src include` 为空，`git diff --exit-code 3b0820d HEAD -- src include` 退出码 0。
- CLI last-message 文件未生成：该命令因 Codex 用户目录状态库只读和 app-server 拒绝访问而退出 1；没有手动冒充 CLI 输出。
- 构建、CMake、CTest、代码测试、smoke、benchmark、传输、profile、实验、SSH、云端和清理均 `NOT_RUN`。
