# LOWLEVEL-TREE-TELEMETRY-PLAN-01：目录阶段 telemetry 契约与插点设计结果

日期：2026-09-23（Asia/Shanghai）
路线/任务：`R2026-09-23.4 / v1`。本文件是固定源码上的只读契约设计；没有实现 telemetry，也不构成任何性能结论。

## 输入与基线核验

- 任务版本仍为 `R2026-09-23.4 / v1`。执行前 `git rev-parse HEAD` 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；这是共享文档 HEAD，不是本任务源码输入。
- `git diff --name-only 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src include tests tools CMakeLists.txt` 无输出；对应 `git diff --exit-code` 退出码 0。因此本次所读源码与固定实现提交 `3b0820d` 一致。
- `git status --short --branch` 显示共享工作树有既存 BOARD、角色回执及任务/决策文档修改；`git diff --cached --name-only` 无输出。只读工作，未改共享文件或 index。
- 输入资料齐全：tree stage profile 补充/阻塞结果、low-level 设计与 QA 结果、目录源码调查结果、2026-09-17 profiling 决策均可读。补充 profile 在本机 loopback 得到 upload/download fp1/fp4 各 6/6 正确完成、tree hash 重复一致；它只证明并发变化可重复影响 wall。已有逐文件 `elapsed_seconds=0`、秒级 UTC 时间戳、无实际 tree `transfer_id`，不能归因阶段成本；原阶段 profile 01 仍是 WSL E_ACCESSDENIED 阻塞，没有可替代的阶段样本。

## 当前实现事实与观测缺口

| 位置 | 当前事实 | 对新契约的影响 |
| --- | --- | --- |
| `src/core/metrics/event_log.cpp:14-21,141-165`；`include/cpnetflux/core/metrics/event_log.h:13-24` | event log 是 JSONL；UTC 时间戳精度为秒；`EventRecord` 有 `transferId` 和单个 `elapsedSeconds`，没有单调时间、schema version、run/file/attempt/stream/worker scope 或可空数值字段。 | 需要新增版本化 stage 事件及真正的 JSON `null` 表示；不能把 `elapsed_seconds=0` 改释为新计时。 |
| `src/core/io/tree_transfer_client.cpp:890-905` | `emitTreeEvent` 调用 `writeEventLog`，传入空 `transferId`、`elapsedSeconds=0`；忽略写日志的返回状态。它与 `EventLogger::write` 不同，未使用实例 mutex。 | 新事件要有共享、线程安全的 run recorder；日志失败必须报告 evidence partial，但不得改传输状态。 |
| `tree_transfer_client.cpp:2380-2429,2433-2514` | upload/download 在入口取 `steady_clock`，最终 tree summary 输出 run elapsed；upload 有本地 scan/manifest prepare，download 先建控制连接并 NLST/SIZE/MDTM，再作 resume preflight。 | 有 run wall 的 monotonic 起点，但没有 run id、分阶段 scan/preflight 事件或与文件事件的统一关联。 |
| `tree_transfer_client.cpp:475-488,1141-1179` | 新 control connection 的 `connectTo` 含 DNS/TCP、可选 TLS、greeting；`login` 含 USER/PASS/TYPE/OPTS。worker reuse 命中直接复用，parallelism 变化时仍发 OPTS。 | `control_acquire` 应记录新建/复用以及 control connection ordinal；复用不能假报为一次 TCP connect。 |
| `tree_transfer_client.cpp:1307-1476,1479-1632` | 两方向的逐文件路径中有 metadata、EPSV、可选 REST、STOR/RETR + 150、file client 调用、226 wait、tree record update。upload 有 global scheduler 压缩 fallback retry。 | file 级控制/完成/manifest 边界在 tree wrapper；需要 attempt ID，不能把 retry 合并成一个成功 duration。 |
| `tree_transfer_client.cpp:1046-1093,1109-1116` | `updateRecord`/`updateRecordForTransfer` 在 scheduler mutex 内更新状态并原子保存整份 manifest；`TreeWorkerRuntime` 持有可复用 ControlClient，但没有 worker ID。 | `manifest_finalize` 计时需覆盖 mutex 等待与现有 save 调用，不更改锁或 checkpoint 时点；增加 worker ordinal 的传递。 |
| `tree_transfer_client.cpp:2231-2280,2334-2359` | Global 与普通 worker loop 都按 worker thread 调用逐文件函数；普通 loop 当前 lambda 未捕获 `worker` 序号；每个文件完成后才取下一项。Global 有完成文件的 worker 外 resume-validation pass，及等待容量的 dispatch loop。 | worker id 必须从 loop 明确捕获；resume-validation 无 worker 时写 null/`run_preflight` scope。inter-file idle 要跨相邻 file attempt 关联。 |
| `src/core/io/file_transfer_client.cpp:390-487`；`src/core/io/file_download_client.cpp:374-429,438-627,632-758` | upload 的每个 `sendStream` 建 data socket、做 SessionInit/ResumeResponse，再在 `sendChunkRange` 读/校验/发送 DATA，最后 FIN 并等 DATA Complete。download 的每个 `receiveStream` 建连接、收 SessionInit、初始化/恢复 session、发 ResumeResponse、收 DATA/ChunkComplete/FIN；`runFileDownloadClient` 之后还做 verify、rename/commit。 | `data_connect` 与 payload 插点必须进入 per-stream file client；download 的本地 finalize 在控制 226 之前。仅在 tree wrapper 外围计时会把两个方向的 payload 和 finalize 混在一起。 |
| `src/core/metrics/transfer_phase_stats.cpp:27-39,96-116`；对应头文件 `include/cpnetflux/core/metrics/transfer_phase_stats.h:13-80` | 已有 `steady_clock` 纳秒累计及 Send/Recv/Read/Write/Checksum 等 atomic 总量；Scope 是一次 file client 的聚合 counters，不保存起止点、stream ID 或每文件事件。 | 可供诊断对照，不能代替 per-stream 时间戳或并发区间。 |

## 推荐的最小事件契约

只追加 JSONL 事件种类，例如 `event="tree_stage_v1"`、`schema_version=1`；现有 `file_start/file_complete/file_failed/file_skipped/tree_*_complete` 行保持原名和原字段语义。每一条记录表示一个 stage span 或明确的非适用/未开始状态，不用一行覆盖整棵目录。

必需关联键与类型：

| 字段 | 类型/规则 |
| --- | --- |
| `run_id`、`clock_domain_id` | 非空字符串；每次 tree client invocation 新建唯一 run id。同一 run 的线程共享一个 steady-clock 域；通常 `clock_domain_id=run_id`。 |
| `direction` | 枚举 `upload` / `download`。 |
| `file_id` | uint64；当前 manifest 的稳定 file index。附带已有 `path` 供人读，不用路径本身充当跨 attempt 主键。 |
| `attempt_id` | uint32，从 1 开始，按该 run 内同一 file 的每次实际 transfer attempt 递增；resume skip 仍有一次 file decision 记录，`attempt_kind=skip`。 |
| `worker_id` | uint32；从 0 开始的 worker loop ordinal。run 级 preflight 或 Global scheduler 的 worker 外验证为 JSON null。 |
| `control_connection_id` | uint64 或 null；run 内分配的客户端 control connection ordinal，重连另取新值。复用命中沿用原值。 |
| `stream_slot`、`stream_id` | uint32 或 null。slot 是本地 stream worker；upload 的协议 stream ID 在 connect 前已知，download 在收到 SessionInit 后才可填 `stream_id`，之前只填 slot。 |
| `transfer_id` | 字符串或 JSON null；只写本 attempt 实际用于 REST/SessionInit 的 effective ID。尚未从 150/manifest 语义确认时为 null，不能用请求参数、客户端临时随机 ID 或空字符串假充。 |
| `scope`、`stage`、`state` | 字符串枚举。scope 为 `run` / `file_attempt` / `stream` / `worker`；state 为 `complete` / `failed` / `not_applicable` / `not_started` / `incomplete`。 |
| `start_monotonic_ns`、`end_monotonic_ns`、`elapsed_ns` | uint64 纳秒或 JSON null。只允许在相同 `clock_domain_id` 中相减；complete/failed span 应有有效起止点，其他 state 按下表置 null。 |

可选但建议字段：`reuse_outcome`（`new`/`hit`）、`reason`、`attempt_outcome`、`error_code`、`retry_reason`、`active_payload_io_ns`、`payload_wire_bytes`、`payload_logical_bytes`。requested/effective 配置及已有 `bytes` 继续由原 summary/其他 telemetry 负责；不得把尝试字节当成已成功 wire bytes。

计时器统一使用 `std::chrono::steady_clock`，序列化为 `duration_cast<nanoseconds>(time_point.time_since_epoch()).count()` 的 uint64 整数。记录可选的人类 UTC 时间只作显示，不参与 duration。不得跨 client/server、跨进程或跨主机相减 monotonic 值；即使传输 ID 相同也只能关联，不能校准时钟。并发 stream/worker 的 span 可以相交；各 span duration 的 sum 不等于 tree wall，且通常可以大于 wall。原始事件是主证据；汇总若给 sum/median/p95/max，必须附 scope 与样本数，并同时保留 run wall。

## 阶段定义、插点与 upload/download 边界

| 阶段 | 起止定义与插点 | 方向差异/注意事项 |
| --- | --- | --- |
| `control_acquire` | 在 `controlForFile` 入口前开始，至 control ready 返回结束；新建时覆盖 `connectTo`、TLS/greeting 和 `login`，worker reuse 记录 hit，若 parallelism 改变则纳入 OPTS。位置：`tree_transfer_client.cpp::controlForFile`、`ensureControlReady`。 | upload/download 相同。download 首次 tree listing 和 resume preflight 用 run-scope/control connection 记录，不能挪到某个 worker file duration。 |
| `control_prepare` | 每个 transfer attempt 的逐文件控制前置区间：从首个文件级 SIZE/MDTM 或 EPSV 开始，到 STOR/RETR 的 150 + transfer ID 已解析；含 REST。完成文件校验而 skip 的路径在完成验证时结束。位置：`processUploadFile` / `processDownloadFile`。 | upload 可能对已完成文件做 SIZE 验证；download 的 SIZE/MDTM 在单文件传输前执行。resume preflight 的批量 SIZE/MDTM 应另记 run-scope `resume_preflight`，不是文件 transfer 的 `control_prepare`。 |
| `data_connect` | 每个 stream 在调用 `connectFramedDataSocket` 前开始，TCP connect/TLS handshake 成功或失败时结束；含 DNS/getaddrinfo，因为它在该调用内。位置：upload `sendStream`；download `receiveStream`。 | download connect 时 wire `stream_id` 未必已知，先关联 `stream_slot`，读到 SessionInit 后补协议 ID。TLS handshake 明确属于本区间。 |
| `first_payload` | 每 stream span 从 `data_connect` 成功结束到首个完整、有效 DATA payload 的发送/接收成功点；含 SessionInit/ResumeResponse、首块文件 read/checksum、DATA header 及等待首 payload 的时间。位置：upload `sendChunkRange` 成功 `sendAll` 后；download `receiveStream` 在 `recvAll` 和 DATA payload 验证成功后。 | upload 的“成功”表示本地 socket writeAll 接受完整 payload；不声称远端应用已处理。download 的“成功”表示完整 payload 已到客户端且协议验证通过。不要把它强行排序为小于 `payload_io` duration。 |
| `payload_io` | 每 stream 一条记录：start 为首次 DATA payload socket send/recv 调用开始，end 为末次完整 DATA payload socket 调用结束；span 包括中间 read/checksum/调度间隙。另可累计 `active_payload_io_ns`（仅围住 DATA payload 的 sendAll/recvAll 调用时长），不要用 TransferPhaseStats 的所有 Send/Recv 总量代替。 | upload 在 DATA payload send；download 在 DATA payload recv，均不含 64-byte frame header。错误结束的调用记 failed/incomplete；没有可得的成功部分字节数时不能推算 wire byte counter。 |
| `data_channel_finalize`（补充子阶段） | 从该 file 最后一个完整 payload end（无 payload 时为所有 stream session-ready）到 `runFileTransferClient` / `runFileDownloadClient` 返回。 | upload 包括剩余 ChunkComplete、FIN、服务端 framed Complete；download 包括接收 FIN、session flush/final verify、文件 rename/commit 与客户端 Complete。此段嵌套在下面的完整 tail 内，用于避免把 download 本地 finalize 误读成控制响应等待。 |
| `transfer_complete_wait` | 与既有 profiling 决策一致：从 file 最后一个 payload end 到 control 226 成功读取结束；无 DATA payload 的真实 transfer 从所有 stream session-ready 起算。记录内部 `data_channel_finalize` 和 `waitTransferComplete()` 的实际边界，以便看到 tail 组成；不要对嵌套区间求和。 | 上传/下载由 `processUploadFile` / `processDownloadFile` 在文件 client 返回后调用 `ControlClient::waitTransferComplete`。若数据阶段失败且没有调用控制 wait，按 `not_started` 或以实际观察到的错误终点记 failed，不能伪造 226。 |
| `manifest_finalize` | 围住 terminal tree record update：上传成功的 Completed 保存；下载 226 后的 mtime 设置 + Completed 保存；失败时只有真的调用 Failed/Changed manifest save 才记录对应 span。插点是 `updateRecord` 的调用边界，不改 `saveManifest` 原子替换或锁语义。 | 这是 tree manifest 持久化，不包含 download transfer session 每 chunk manifest flush 或前置 Transferring/transfer-id checkpoint。计时覆盖 scheduler mutex 等待与 atomic save，若要再分锁/IO 需另项设计。 |
| `interfile_idle` | worker scope，从上一 `processFile` 返回后立即开始，到下一 file 已被取出、即将进入 `processFile` 时结束；记录 `previous_file_id/attempt_id` 与 `next_file_id/attempt_id`。普通 worker loop 在 dequeue 前后；Global loop 从上一完成后到 `nextGlobalDispatch` 给出下一 plan。 | 不包括上一文件的 manifest finalize（它在 `processFile` 返回前）；Global dispatch 的 capacity sleep/调度等待计入 worker idle。首个 file 和 worker 退出时没有相邻 file，必须是 not_applicable。 |

`tree_plan_prepare` / `resume_preflight` 是 run-scope 补充 span：upload 的 local tree scan/manifest load 与校验、download 的 NLST/metadata 构造和已有 resume 预检发生在 worker stage 之外。现有 run wall 从 `runTreeUploadClient` / `runTreeDownloadClient` 函数入口计时；新增 span 应复用这个 run 起点，不能重置 denominator。Global scheduler 对已 Completed 条目的预验证发生在 worker threads 启动前，记录为 run/file validation，`worker_id=null`，不伪装成普通 worker transfer。

## 状态、空值与异常生命周期

| 情形 | 必须表达的状态 |
| --- | --- |
| 非空正常文件 | 所有适用 stage 有同一 run/file/attempt 关联；data_connect 按 stream，control/manifest 按 file attempt，idle 按 worker 邻接 pair。 |
| 空文件（真实传输） | control、每 stream connect/session、data-channel finalize、226、terminal manifest commit 仍可计时；没有 DATA payload，因此 `first_payload`、`payload_io` 是 `not_applicable`，`reason="empty_file"`。transfer-complete tail 的起点用 session-ready，而非 0。 |
| 单 payload 文件 | 正常记录一个 first-payload prefix 和一个 payload_io span；二者允许相交，且不规定两个 elapsed 的大小关系。 |
| 所有文件均因 manifest Completed 而 resume skip | 每个文件保留 decision/skip attempt。control acquire 与已实际执行的验证命令照实记录；所有没有发起 data transfer 的 connect/payload/complete/terminal manifest stage 为 N/A。download run-level SIZE/MDTM resume preflight 仍独立记账。worker 相邻 skip 之间 idle 仍可测。 |
| 首块前失败 | 已进入的 stage 以 failed 及观察到的结束时刻结案；之后未进入的 stage 是 `not_started`，不是 N/A。若 connect 后 SessionInit 或首 DATA 失败，保留失败 stream 和关联 transfer ID（若当时已知）。 |
| retry | 同一 `file_id`，新的 `attempt_id`；各 attempt 有自己的 control/data/complete/manifest 记录与结果。保留失败 attempt，不把 elapsed、stream bytes 或错误覆盖到最终成功 attempt。当前 global compression raw retry 也按此记账；其 retry 原 transfer ID 仍写实际用于 SessionInit 的 ID。 |
| 并发重叠 | 同 clock domain 的 start/end 原样保留，不强制全序；用 worker/file/attempt/stream ID 解释。阶段 duration sum、各 stream active I/O sum 均不能替代 run wall。 |
| graceful cancel/返回错误 | 正在执行且能够观察退出点的 stage 以 failed/end 记录；从未进入的 stage 是 not_started；不存在正常业务阶段（空 payload/已跳过）才用 not_applicable。 |
| 突然进程退出、日志损坏/缺行 | 缺少 end 的已开始 span 为 incomplete；完全缺失的事件属于 evidence gap，不能当 0，也不能从缺失日志自动补 N/A。 |

正式 writer 应序列化 JSON null，而非字符串 `"null"`、空字符串或 0。`state` 必须说明缺时刻的原因；`elapsed_ns=0` 只允许是实测到同一时钟 tick 的有效短 span，不能作为默认值。stage event 写入失败不可让 transfer status 失败/成功状态改变；应在 telemetry/footer 上暴露 `evidence_status=partial`。原有 `emitTreeEvent` 忽略 writer 错误的问题需要单独保留 transfer/evidence 双状态。

## 后续实现插点与测试边界

建议后续窄实现只新增 metrics recorder/context 和 additive JSONL stage event：

1. `include/cpnetflux/core/metrics/event_log.h`、`src/core/metrics/event_log.cpp`：版本化 stage record、nullable uint64 序列化和线程安全批量写入；保留老 `EventRecord` 行格式。
2. `src/core/io/tree_transfer_client.cpp`：在 run 入口建 run/clock domain；在 `processUploadFile`、`processDownloadFile` 与 `controlForFile` 记 file/control/226/manifest spans；在两种 worker loop 捕获 ordinal；在 queue dequeue 边界写 worker idle；记录 resume preflight 与 run wall。不能改 manifest/status 处理顺序、control reuse 或 scheduler 行为。
3. `src/core/io/file_transfer_client.cpp` 与 `src/core/io/file_download_client.cpp`：通过仅进程内 observer/context 从 tree wrapper 传递 run/file/attempt/worker ID；分别在 `sendStream`/`receiveStream` 的 connect、SessionInit 后、DATA payload send/recv 的准确边界写 stream 事件。observer 在 telemetry disabled 时不读时钟、不分配 event；不得改 frame/option wire encoding。
4. 单元入口：`tests/unit/event_log_test.cpp` 加 JSON version/null/uint64/escaping 与旧事件兼容；新增 recorder test（可注入 clock）覆盖 span 生命周期、attempt/worker/stream key、空状态、失败未启动、retry、并发区间和线程并发写不串行损坏；在 `CMakeLists.txt` 注册。单元测试只证明 recorder/serializer，不证明端到端传输。
5. 真实链路 CTest：扩展 `cpnetflux_tree_upload_smoke`、`cpnetflux_tree_download_smoke`、`cpnetflux_tree_parallel_smoke`、`cpnetflux_tree_control_reuse_smoke`、`cpnetflux_tree_resume_smoke`、`cpnetflux_tree_changed_file_smoke`、`cpnetflux_tree_edge_cases_smoke`，并在有实际覆盖时跑 `cpnetflux_tree_scheduler_smoke`。断言两方向 file/attempt/worker/stream coverage，reuse 新建/命中，空文件、单 payload、全 resume skip、connect/首块前失败和 retry 状态；保留现有真实 client/server/hash 断言。方向以外的 TLS/control 认证已有独立 smoke；回归任务要列真实可用的 TLS on/off 路径及 blocked/skip 原因，不把 mock 当链路通过。

观测开关的非干扰验证：用同一固定提交、binary、seed、树清单和配置分别执行 telemetry off/on 的 loopback upload 与 download，比较每个 transfer 状态、source/target SHA-256 与 tree hash、file count、logical bytes、DATA frame 数及协议 payload wire-byte counters；目标文件内容必须逐字节/hash 相等，数据帧编码/协议版本/帧序列不因事件而变。session/transfer ID、时间戳、TCP packet 分段可因每次 run 不同，不能要求整个 TCP capture 字节相同。之后再测 telemetry on 的阶段 overhead；不得用 off run 的 wall 当 on run 的阶段分母。验收必须将 transfer、integrity、evidence、wire accounting 分列。

向后兼容边界：旧 JSONL event 名和字段语义不变，尤其不重释已有逐文件 `elapsed_seconds=0`；新 parser 允许跳过未知 event，通过 `schema_version` 区分新 contract。无新增控制命令、协议帧、manifest 字段、默认并行度或 scheduler 行为。tree plan/resume 与 transfer-session finalize 分开计时；只有在 01/00 冻结本事件契约后才进入独立实现任务。

## 执行回执

- 输入源码提交：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；共享文档 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。本结果写后预期仅文档 HEAD/status 增加目标 untracked 结果文件，不会改提交或暂存区。
- 实际改动：仅新增本结果文件。CLI 摘要文件另由 `codex exec --output-last-message` 尝试生成；没有手工伪造 CLI 输出。
- 实际只读命令：`git rev-parse HEAD`、`git status --short --branch`、`git diff --cached --name-only`；`git diff --name-only` 与 `git diff --exit-code` 对固定 `3b0820d..HEAD` 的 `src include tests tools CMakeLists.txt` 定向检查；定向 `rg` 与 `Get-Content` 读取任务、BOARD、入口/环境/角色说明、列明的结果与决策、tree/file/event-log/phase-stats 源码和 CTest 注册。目标差异/状态检查 0，所用定向检索/读取最终成功为 0；首次一次 PowerShell 读取命令因插值语法错误退出 1，未写入文件，随后修正重跑成功。
- 报告 UTF-8、末尾换行、无尾随空白、`git diff --check`、写后 HEAD 和空暂存区结果在最终执行记录补充。CMake、build、CTest、单元测试、smoke、传输、profile、benchmark、SSH、云端操作和清理均 `NOT_RUN`。
- 剩余限制：本设计尚未经 01/00 冻结；payload active-time 与 payload envelope 是不同测量量；download SessionInit 后才获得协议 stream ID；当前 control/event logger 未提供足够的跨 worker lifecycle recorder。事件写盘开销、阶段覆盖率及 100 Mbps 目标链路行为均未实测。
- 下一角色可直接执行：01/00 审核并冻结本 contract（尤其 transfer-complete tail 嵌套边界、download stream slot/id 与 preflight scope）；冻结后由 03 在 `3b0820d` 派独立 worktree 最小实现，再交 04 用上述真实 tree CTest 与 bytes/hash 门禁复核。未验收前不派 lookahead，也不宣称阶段瓶颈或性能收益成立。

## 写后验证

最终执行核验：

- `git rev-parse HEAD`：退出码 0，仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。源码路径相对固定实现 `3b0820d` 的差异检查退出码 0；暂存区为空、退出码 0。
- 报告严格 UTF-8 解码成功、以换行结尾、尾随空白数为 0；目标文件 `git diff --check` 退出码 0（目标是新文件，另做文本扫描）。全局 `git diff --check` 退出码 0，只有既存共享文档的 LF→CRLF 提示。
- 本任务最后摘要命令：`codex exec --ephemeral --output-last-message docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-01-last-message.md "请仅根据 docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-01-result.md 写一段简短中文执行摘要到最后回复。不要修改仓库文件。"`，退出码 1。CLI 报告 `state_5.sqlite` 只读及 app-server `E_ACCESSDENIED`；未生成 last-message 文件，未手工伪造。
- 定向 status 显示本任务仅新增本结果文件；没有写其它允许范围外文件。CMake configure/build、CTest、单元/集成测试、smoke、传输、profile、benchmark、SSH、云端和清理均 `NOT_RUN`。
