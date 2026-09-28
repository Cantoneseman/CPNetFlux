# LOWLEVEL-TREE-TELEMETRY-PLAN-03 结果：固定源码生命周期冲突，移交 00

路线版本：`R2026-09-23.6`
任务版本：`v1`
日期：2026-09-23（Asia/Shanghai）
固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`

## 结论

**BLOCKED：未形成可交 QA-03 作 PASS 审查的完整 PLAN-03，也不建议按现有契约开始实现。** 对照固定源码发现，两种“完整 resume skip”都要求跳过已在源码中实际发生的 worker/control 或数据连接事件：已完成文件的跳过是在 `controlForFile` 及远端/本地校验之后才确定；“所有 stream 均无 missing range”则只能在逐 stream 建连并完成 SessionInit/ResumeResponse 交换之后才确定。ARCH-REVIEW-01 又要求 skip 只有一条 `file_attempt_decision`，不得有 worker/control/stream 行，且日志 append-only。三者不能同时成立，validator 无法同时如实覆盖实际调用并接受规定的 skip 形状。

按任务单“发现决策需进一步变化时停止，交 00 处理”的要求，本回执不自行改写决策、ARCH schema、BOARD 或 ROSTER，也不把未解决的生命周期向量伪装成已闭合 PLAN。请 00 先决定两种 skip 的权威语义和相应记录形状，再冻结更新版输入；之后 03 才能完成 PLAN，04 再执行 QA-03。实现准入仍未通过。

## 范围、输入与实际核验

- 目标范围：仅修订目录 telemetry 的实现映射；唯一允许写入为本结果文件。
- 非目标：未改源码、测试、runner、CMake、协议帧、manifest/resume/checksum/scheduler/compression/backend、默认配置、decision、BOARD 或 ROSTER；未建 worktree、构建、测试、benchmark、SSH、运行跨域实验或清理环境。
- 输入：`docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`、PLAN-02、QA-02、ARCH-REVIEW-01 结果及固定源码提交。
- `git rev-parse HEAD`：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 固定源码至 HEAD 对 `src include tests tools/demo/run_alpha_demo.py tools/test CMakeLists.txt` 的 `git diff --name-only` 项数为 0；阶段顺序核对仍对应固定源码。
- 写入前 index：`git diff --cached --quiet` 退出码 0；`.git/index` SHA-256 为 `B242B58AA3D07B1F065530EEDE81ADEEBC496B1625789008CDE904C193FF57C4`。尝试 `git write-tree` 时因 `.git/index.lock` 权限拒绝退出失败；未获准也无需改变 Git index，改用只读 index hash 与 cached diff 核验。
- 工作树在开始时已有其他协作文档修改和未跟踪资料；未覆盖或清理。

## 与 R2026-09-23.6 相符的阶段边界

固定源码 upload 顺序：`runFileTransferClient` 返回（`src/core/io/tree_transfer_client.cpp:1409`）→可能为失败 attempt 单独调用一次 `waitTransferComplete`（`:1416`）并进入 retry→最终成功路径调用 `waitTransferComplete`（`:1457`）→终态 `updateRecord`（`:1468`）。因此每次真实调用的 `transfer_complete_wait` 绑定其所属 attempt；同一 attempt 的 `data_channel_finalize` 终点是文件函数返回，wait 起点正是该返回点，两者相接、不重叠。若失败 attempt 没有实际调用 wait，记录单行 `skipped/upstream_failure`，三时间字段为 null。

固定源码 download 顺序：`runFileDownloadClient` 返回（`:1592`）→`waitTransferComplete`（`:1605`）→成功后调用 `setRegularFileMtime`（`:1616`）→终态 `updateRecord`（`:1624`）。文件 download 函数内 flush、校验及 rename/commit 均在函数返回前。因此 `data_channel_finalize` 不含 226 或 mtime；`download_mtime_finalize` 只从成功 226 返回后、`setRegularFileMtime` 调用前开始，到该调用返回；`manifest_finalize` 只包实际终态 `updateRecord` 的 mutex 内更新及 save。失败路径仅对真实进入的操作记录 failed span，下游未进入的阶段为 `skipped/upstream_failure`，不得构造零时长成功区间。

有数据 attempt 的 `data_channel_finalize` 起点是该 attempt 所有有数据 stream 中最后一个完整有效 DATA payload 的最大完成时刻，终点是文件传输函数返回。实际执行的空文件以 `payload_work_closed` 为起点；非空 attempt 在首个有效 payload 前失败时没有此起点，故不得填 0 或伪造 finalize duration。此处应待 00 先决定 skip 生命周期映射，再与 QA-03 一起冻结其应使用的单行 skipped/N/A 规则。以上时序与已接受决策的四个阶段定义一致，但不消除下面的 skip 矛盾。

## 阻断证据：skip 行形状无法覆盖固定调用顺序

### 已完成文件的 resume skip

- Upload 在 `processUploadFile` 中先调用 `controlForFile`（`tree_transfer_client.cpp:1340–1343`），之后才检查 `record.status == Completed` 并执行远端校验及 skip return（`:1346–1358`）。
- Download 同样先 `controlForFile`（`:1492–1495`），之后检查已完成状态、验证本地文件，再 skip return（`:1498–1521`）。
- `controlForFile` 的连接选择、建连或复用发生在 skip 判定之前。若记录 `control_acquire`，则规定的“一条 skip 行、worker/control 全 null 且无 worker/control span”不成立；若不记录，则该实际控制阶段不在 telemetry 中，无法声称阶段观测覆盖这条路径。

### 全部 stream 的 missing range 为空

- Upload 对每个 configured stream 创建线程（`src/core/io/file_transfer_client.cpp:549–554`）；每个线程先建 data socket（`:400–405`），再执行 SessionInit 并读取 missing ranges（`:412–416`）。
- Download 每个 stream 线程也先建连接（`src/core/io/file_download_client.cpp:378–382`），再读 SessionInit（`:388–395`），随后准备 missing ranges 并发送 ResumeResponse（`:415–425`）。
- 因此“所有计划 slot 都没有 missing range”是在真实 `data_connect` 和协议交换之后才可判定的。若此前按 append-only 规则记录连接和 `stream_identity`，不能再把它改写为无 transfer attempt、无 stream 行的完整 skip；若预先不记，则连接实际发生但被漏观测。

ARCH-REVIEW-01 §“固定生命周期行形状”将“整个文件没有任何 missing range”合并为完整 resume skip，并禁止同时存在 transfer attempt 或 stream 行；同节又规定完整 skip 只能写 `file_attempt_decision`，其 worker/control/stream/transfer 均为 null。这个规定与上述固定源码顺序不兼容，不是通过 parser 边界或日志失败策略可以化解的字段细节。

## 交给 00 的最小裁定

请 00 在不改传输行为的前提下，分别冻结以下两类情况，而不是继续共用一个含义不明的 “full resume skip”：

1. manifest 中已是 `Completed`、经 control/文件校验后跳过：决定是否允许记录判定前已经发生的 file-bound control span；若不允许，需明确这段校验控制操作属于观测范围外，并说明 active transfer 分支如何保留所需 control 计时。
2. 文件传输握手后发现所有 missing range 均为空：决定把它定义为实际 attempt（保留已经发生的 connect/identity，payload 阶段 N/A），还是定义为另一种可包含既有 stream 证据的 skip 事件。两种选择都会修改 ARCH 当前“无 attempt/无 stream 行”的互斥规则；不能由 03 私自选择。

本次不新增阶段，不建议通过造零时间、延后 start 写入、回写/删除 JSONL 行或隐藏已发生的连接来绕过冲突。00 更新语义决策与架构映射后，需同步固定 task 输入版本，再派 03 完成 PLAN-03 的字段、ID、状态向量和实现映射。

## QA-02 十项状态映射

| QA-02 项 | 本回执结论 | 后续实现/QA 门禁 |
| --- | --- | --- |
| #1 `first_payload` / `payload_io` | 设计语义可沿用：分别校验各自非负与差值，不建立两者大小不等式；动态测试未运行。 | 单元测试覆盖慢首块例与两 span 独立配对；NOT_RUN。 |
| #2 finalize / 226 / mtime / manifest | 四阶段端点和 upload/download 调用顺序已在本回执核对；其余 skip lifecycle 冲突仍阻止整体验收。 | 实现后验证 retry 每 attempt 的 wait、失败与终态 manifest 顺序；NOT_RUN。 |
| #3 append-only、重复 terminal、flush、四维隔离 | ARCH 规则仍是后续规范来源；本任务未将其落实为完整 PLAN 实现章节，不能报规格验收完成。 | logger 状态单测、并发整行写和短写/poison 故障注入；NOT_RUN。 |
| #4 ID/null 矩阵 | ARCH 有 ID 类型与分配规则；skip 与实际 control/data 事件冲突使 lifecycle row matrix 尚未闭合。 | validator 全 scope null 矩阵、分配失败消耗及唯一性测试；NOT_RUN。 |
| #5 skip/no-range/空文件/retry/失败/早退 | empty/retry/failure 的定义可继承 ARCH；两个 skip 分支不能同时满足固定源码与指定互斥形状。 | 修订决策后为两类 skip、空文件、no-range、连接失败、首块前失败、setup early return 和 interfile idle 建确定向量；NOT_RUN。 |
| #6 `stream_identity` | ARCH 的独立字段集合和 slot/ID 规则尚未复制为 PLAN-03 可验收章节；全文件 no-range 的连接时序使其记录互斥未决。 | parser 验证有效绑定、重复 identity、错 slot、冲突 stream ID 与后续行一致性；NOT_RUN。 |
| #7 uint64 原始 token | ARCH 的 uint64 要求为后续 validator 依据；PLAN-03 本轮未给出最终可审查向量。 | 原始 JSON token 测试 `0`、`UINT64_MAX`、越界、负数、浮点、指数、字符串和 elapsed 差值；NOT_RUN。 |
| #8 alpha demo 旧消费者 | ARCH 要求新行均带 `error_code`、正常行为 `ok`；本任务未运行消费者兼容测试。 | 混合旧事件、stage、identity 的 `run_alpha_demo.py` 回归；NOT_RUN。 |
| #9 on/off、故障注入、真实链路及开销 | 本任务全部未运行。 | parser/unit 与 logger failure injection；真实 tree upload/download on/off hash、manifest、resume、frame 逻辑内容对照；性能 overhead 基准；全部 NOT_RUN。 |
| #10 01→03→04 闸门 | BLOCKED：需 00 决定 skip 语义并更新规范；完成新版 PLAN 后由 04 QA-03 明确 PASS，才可建立实现 worktree。 | 当前不允许派发 telemetry implementation、lookahead、云端构建或实验。 |

## 验收状态与下一步

- 本次文档仅记录确认的源码顺序和阻断证据；未声称“PLAN-03 完成”“确定性 validator 已闭合”或实现准入通过。
- 固定源码与当前 HEAD 的实现路径差异为 0；未触及源代码、测试、runner 或 CMake 路径。
- 写后核验：文件严格 UTF-8 解码成功、纯 LF、末尾 LF、尾随空白 0 行；`git diff --check` 退出码 0（仅报告工作区其他既存 Markdown 文件的 LF→CRLF 提示）；HEAD 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，index cached diff 退出码 0，index SHA-256 与写前一致；固定源码实现路径差异仍为 0。
- CMake build、CTest、单元/parser 测试、故障注入、真实 tree upload/download、hash/manifest/resume/frame 对照、性能 overhead、SSH、云端项目状态、跨域实验：全部 `NOT_RUN`。
- 下一步：00 处理本回执列出的两个 skip 语义，再确定是否 supersede 当前 PLAN-03 任务或更新输入后续作；不能仅凭本回执把 QA-03 或 implementation 标为已通过/已启动。
