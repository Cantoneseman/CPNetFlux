# LOWLEVEL-TREE-TELEMETRY-PLAN-02 结果：目录阶段 telemetry v2

日期：2026-09-23（Asia/Shanghai）
路线/任务：`R2026-09-23.5 / v1`
结论：只读设计交付。唯一语义规范为 `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`；本结果把它整理成实现与 QA 可直接检查的 v2 映射，不授权源码实现、构建或实验。

## 输入与基线

- 已读取本任务、BOARD 对 PLAN-02/QA-02 的状态、规范决策、PLAN-01 结果、QA-01 结果、2026-09-17 profiling 决策，以及 `tree_transfer_client.cpp`、单文件 upload/download client、event log、phase stats 和 `tools/demo/run_alpha_demo.py` 的相关源码/消费者入口。
- `git rev-parse HEAD`：退出码 0，当前共享文档 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。固定源码输入仍为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；对本设计涉及的源码、头文件、demo parser 和 CMake 路径执行相对该基线的 `git diff --quiet 3b0820d..HEAD -- <paths>`，退出码 0，未发现源码基线漂移。
- `git status --short --branch`：退出码 0；执行前共享工作区已有其他文档修改/未跟踪任务文件。`git diff --cached --quiet`：退出码 0，暂存区为空。本任务未改这些共享输入。
- `rg` 定向定位和 `Get-Content` 读取任务、BOARD、规范/旧决策、两份先前结果、源码与 parser 均成功（退出码 0）。本报告是唯一手写产物。

## v1 → v2 冻结差异

| 项目 | PLAN-01 v1 旧口径/缺口 | 本 v2 规则 |
| --- | --- | --- |
| `first_payload` / `payload_io` | 沿用旧决策中的 `first_payload <= payload_io`，把两个量误当可排序阶段。 | 撤销该通用不等式。`first_payload` 是 data-connect 完成至首个完整有效 DATA payload 完成的区间；`payload_io` 是首个 DATA socket I/O 开始至最后一个完整 DATA socket I/O 结束的包络区间。分别只验证非负和各自 elapsed 算式。慢首块例：连接于 0 ms 完成，文件读取/调度耗时 790 ms，首个 DATA 写入 10 ms 后完成；`first_payload=800 ms`、`payload_io=10 ms` 合法。 |
| 文件 finalize、226、manifest | 文件本地提交和树终态容易混进 `manifest_finalize`；阶段相加方式未完全冻结。 | 按下文三个独立边界记录；区间允许重叠，不假设构成无缝分段，也不得把阶段 elapsed 求和当 run wall。 |
| 日志生命周期 | 只写终态无法识别崩溃；写失败可能污染传输判定。 | 每个实际执行 span 先 append `in_progress` start，再 append terminal；start/end 不回写。孤立 start 是 `incomplete` + `evidence_gap`，不造 end/零值。日志错误只改变 evidence 维度。 |
| 关联键空值 | run/file/attempt/worker/stream 的 null 规则不足以审查。 | 第 4 节给出 scope × key 矩阵；run 行对实体键显式 JSON null，文件/worker/stream 各按生命周期要求键。 |
| 空、skip、失败、retry、并发 | 完整 resume skip 可能伪造连接或完成阶段；首块前失败和并发 stream 身份不明确。 | 空文件仍可有真实 attempt，但 payload 阶段标 N/A；全文件 resume skip 固定为 `attempt_id=0, attempt_kind=skip` 且不造数据阶段/226；重试按新 attempt 分开；并发按 worker、stream slot 和真实 stream ID 区分。 |
| download stream ID | SessionInit 后才获知 ID，append-only 日志无法回写 connect 行。 | data_connect 可以 `stream_id=null`；SessionInit 解码后追加 `stream_identity` 绑定行，之后阶段同时带 slot 和真实 ID。 |
| 纳秒校验 | 大整数、负数、浮点及 elapsed 不一致的拒绝规则不足。 | 只接受精确十进制无符号 uint64 纳秒；逐区间验证 `end>=start`、`elapsed=end-start` 和无溢出。缺失/null/真实 0 分开。 |
| 旧消费者 | 新 JSON 行缺 `error_code` 会被旧 demo parser 归类成 `unknown_error`。 | 每条新行带 `error_code`；旧事件形状继续可读；混合日志必须由既有消费者回归测试覆盖。 |

## 固定阶段边界与时间规则

所有时间取自同一进程的 `std::chrono::steady_clock`，编码为纳秒 uint64。不同进程/主机的时间戳不可相减。每个区间单独校验：两个端点存在时 `end_ns >= start_ns`；`elapsed_ns` 必须精确等于 `end_ns - start_ns`；计算不得溢出。并发 span 可重叠；阶段区间及其汇总值不能相加来推导 wall time。

| 阶段 | 起点 → 终点 | 必须包含/排除 |
| --- | --- | --- |
| `first_payload` | data connection 完成 → 首个完整有效 DATA payload 完成 | 独立首块延迟区间；可能包含读盘、排队、校验及发送等待。 |
| `payload_io` | 首个 DATA socket I/O 开始 → 最后一个完整 DATA socket I/O 结束 | 包络可包含中间 scheduling/read/checksum wait；不表示 active syscall 时间总和。可以短于 `first_payload`。 |
| `data_channel_finalize` | 最后 payload/session 状态 → 文件传输函数返回 | 包含 FIN/COMPLETE 及下载 session flush、最终校验、mtime、rename/commit 等文件级 finalize。下载本地 finalize 即使与控制面确认次序交错，也归此语义。 |
| `transfer_complete_wait` | 文件传输函数返回 → 控制面 226/最终完成确认 | 单独报告；规范允许与 `data_channel_finalize` 观测区间重叠。不得与其相加充作传输 wall。 |
| `manifest_finalize` | 树级终态记录更新操作入口 → 该次 mutex 保护的 manifest load/update/save 完成 | 只计 tree terminal record 路径；排除下载本地 mtime/rename/commit、块级 session flush、较早的 Transferring/checkpoint 更新、初始扫描/manifest 创建。 |

阶段集合还包括 `run_preflight`、`control_acquire`、`control_prepare`、`data_connect`、`interfile_idle`。每个事件必须声明适用 scope。控制复用命中记在 acquire 的实际结果中，不伪造新连接的 socket connect 时段。`interfile_idle` 是同一 worker 两个相邻 file attempt 之间的空闲跨度，不包括并发 worker 的全局 wall 空闲。

旧 `2026-09-17-directory-data-plane-profiling.md` 中相关 `first_payload <= payload_io` 验收句和旧 `manifest_finalize` 文件侧边界已由 `R2026-09-23.5` 决策替代；旧决策正文不在本任务改写。其他环境、证据与非因果限制继续有效。

## Append-only JSONL 生命周期

规范阶段行使用 `event="tree_stage_v1"`、`schema_version=1`，逐行 JSONL append；遥测设计 v2 不擅自升级该事件 schema 版本。每个 span 以唯一 `run_id + span_id` 配对，`stage`、方向、scope 与实体关联键在 start/end 两行保持一致。并发记录按单调时间和 span key 解析，不能用 JSONL 行相邻关系假设阶段串行。

| 行状态 | `start_ns` | `end_ns` | `elapsed_ns` | 规则 |
| --- | --- | --- | --- | --- |
| `in_progress` | 必填 | 必须为 JSON null | 必须为 JSON null | stage 实际执行前 append 并 flush 到用户态缓冲区；不要求逐行 fsync。 |
| `completed` / `failed` | 必填 | 必填 | 必填且精确差值 | terminal 行重复原 start 时间；完成时 `error_code="ok"`，失败使用实际失败分类。 |
| `skipped` / `not_applicable` | JSON null | JSON null | JSON null | 未启动/不适用时不伪造计时；必须带明确 reason。`skipped` 表示因流程未执行，`not_applicable` 表示语义上不适用。 |
| `incomplete` | 若已观察到 start 则引用其值，否则 null | null | null | 只能表达不完整证据，不能补估结束时间。崩溃场景通常由孤立 start 在分析/summary 中派生 `incomplete/evidence_gap`，不伪造 terminal 行。 |

Start 或 terminal 行写入失败时，不阻断/回滚传输，也不篡改完整性结果、wire accounting 或 transfer status。实现必须维护独立的 evidence partial 状态和 write-failure 计数，并通过不依赖故障 JSONL 写入的 tree summary/process result 暴露。四维结果各自独立：

| 维度 | 遥测写入失败时规则 |
| --- | --- |
| `transfer_status` | 仍由传输操作本身决定；不能因 event logger 失败改成传输失败。 |
| `integrity_status` | 仍由 checksum/hash/目标验证决定；不能被遥测状态替代。 |
| `evidence_status` | 标为 `partial`，增加日志写失败数；若 start 无法写出，该阶段成为不可观测缺口。 |
| `wire_accounting` | 保留实际协议/socket 计数；遥测错误不得增减 DATA 字节/帧。 |

分析器遇到 orphan end、重复 start、key/stage 不匹配、坏 JSON 行或未知 schema/字段时，拒绝该行或 span 并把 evidence 标为 partial；不得猜测补配。unknown schema 不作为 transfer failure。真实进程突然结束时只能报告现有 start 所能证明的未完成 span 和 evidence gap。

## Required/null 字段矩阵

下表是 PLAN-02 给实现与 QA 的最小字段映射。表中 key 名作为本设计的序列化建议；契约中明确冻结的语义优先，尚未由规范逐字指定的键名/整数宽度交 01 在实现前复核。

全行公共字段：`event`、`schema_version`、`run_id`、`direction`、`scope`、`stage`、`span_id`、`state`、`start_ns`、`end_ns`、`elapsed_ns`、`error_code`。scope 行仍保留所有关联列；没有实体时写 JSON `null`，不省略字段、不用空串、0 或猜测值代替 null。

| 字段 | 建议类型 | run scope | file_attempt scope | worker scope | stream scope |
| --- | --- | --- | --- | --- | --- |
| `run_id` | 非空字符串 | required | required | required | required |
| `file_id` | uint64 | null | required | `interfile_idle` 为 null；若 worker 阶段绑定文件则 required | required |
| `attempt_id` | uint64 | null | required | idle 时 null；绑定文件时 required | required |
| `attempt_kind` | `transfer` / `skip` | null | required | idle 时 null；绑定文件时 required | required，真实 stream 仅允许 `transfer` |
| `worker_id` | uint32 ordinal | null | 若已进入 worker 为 required；worker 外 run preflight/resume validation 为 null | required | 由 worker 创建的 stream 必填 |
| `control_id` | uint64 ordinal 或 null | null | control 尚未分配时 null；进入 control 阶段后绑定该 control | worker 无 control 时 null；reuse 时为复用连接原 ID | 已知关联 control 时填写，否则 null |
| `stream_slot` | uint32 本地 slot 或 null | null | null | null | required |
| `stream_id` | uint32 实际协议 ID 或 null | null | null | null | upload 在可证明时填写；download data_connect 可 null，绑定后 required |
| `transfer_id` | 字符串或 null | null | 实际 transfer ID 尚未知时 null，获知后填写 | null | session 的实际 effective ID；未知时 null |
| `path` | 文件相对路径字符串或 null | null | required | inter-file idle 以相邻 attempt 键关联，path null | required |

具体约束：

- `attempt_id=1` 是该文件 run 内首次真实 transfer attempt；失败重试分别为 2、3……，各自生成独立 stage spans。`attempt_kind="transfer"` 不因 retry 改义。完整 resume skip 是一次文件决策记录 `attempt_id=0, attempt_kind="skip"`；不分配 worker/control/stream ID，也不生成 `data_connect`、`first_payload`、`transfer_complete_wait` 等假阶段。
- `worker_id` 是本 run 内稳定 worker ordinal；worker 外 preflight 为 null。`control_id` 在本 run 内标识一次具体 control connection 生命周期；复用命中保留原 ID，新建/重连分配新 ID。为保证 acquire start/end 配对，建议在开始 acquire 时先保留 ordinal，包括建立失败的尝试；ID 分配时点需 01 确认。
- `stream_slot` 是 file attempt 内稳定本地数据流 slot，不是 wire stream ID；`stream_id` 必须取自实际协议 SessionInit/既有发送状态，不能由 slot 推算。`transfer_id` 只记实际用于该 attempt 的有效 ID；不能将请求值或空字符串填作已知事实。
- `file_id` 采用本次 run manifest 中稳定文件序号，`path` 用相对树根路径；这两种表示用于日志关联，不能改变 manifest、路径验证或 transfer 行为。
- `interfile_idle` 需带同一 worker 的 `previous_file_id/previous_attempt_id` 与 `next_file_id/next_attempt_id`（建议新增字段名），避免将 worker 等待时间归给单个文件。具体字段拼写由 01 确认；该 idle span 的普通 `file_id`、`attempt_id`、`path` 均 null。

## 文件与 stream 生命周期状态

| 场景 | ID 与状态 |
| --- | --- |
| 空文件 | 如果实际执行 file transfer，仍是 attempt 1+；payload 阶段为 `not_applicable`，reason=`empty_file`，不伪造首块或 payload elapsed。数据 finalize 和控制完成按实际发生情况记录。 |
| 全部 resume ranges 已完成 | `attempt_id=0`、`attempt_kind=skip` 的 file decision；没有实际 transfer attempt，因此不生成 connect、payload、FIN/226 阶段。 |
| 非空文件但某 stream 没有 missing range | 该 stream 的 payload 阶段 `not_applicable`，reason=`no_missing_range`；不能写成 `empty_file`。若整个 file 无需传输，应使用上面的完整 resume skip 语义。 |
| Retry | 每次实际重试开新 attempt ID 和新 span IDs；失败 attempt 保留 failed/已完成 stage 记录，不能和成功 attempt 合并 duration 或覆盖旧行。 |
| 首块前失败 | 若 connect 本身失败，`data_connect=failed` 并记录失败端点；其后未启动的阶段为 skipped/upstream failure 或因结构不适用而 N/A，不能写零时长成功。若 connect 成功但第一有效 payload 前失败，`first_payload=failed`；有 DATA I/O 才有可计时 payload_io，否则为 N/A/no_data_io。 |
| 并发多 stream | 每个 stream 用 `(run_id,file_id,attempt_id,worker_id,stream_slot)` 唯一定位；已绑定后再加真实 `stream_id`。不同 stream 的 first-payload/payload span 可交错重叠，不合并 start/end，不据此对比绝对 wall sum。 |

### Download stream ID 追加绑定

Download `data_connect` start/end 记录用已经分配的 `stream_slot`；`stream_id` 在 SessionInit 到达前是 JSON null，之后也不回写旧行。成功解码 SessionInit 并确定真实协议 stream ID 后，立即 append 一条 `event="stream_identity"`、`schema_version=1` 的绑定记录，含相同 run/file/attempt/worker/control/slot、真实 `stream_id`、单调 `bound_ns` 和 `error_code="ok"`。之后该 slot 的所有 stream stage 必须同时带 `stream_slot` 与 `stream_id`。绑定失败或没有有效 SessionInit 时维持 null 并标记实际失败/evidence gap；不能从 stream slot 猜 ID。该绑定事件同样受 JSONL append-only 和 evidence failure 隔离规则约束。

## uint64 纳秒 parser 验收规则

- JSON 数值必须以十进制无符号整数词法解析，值域 `0..18446744073709551615`。拒绝负值、浮点、小数、指数形式、字符串数字、超范围值及经浮点中转造成的精度丢失；解析器使用能保留 uint64 的整数路径。
- `start_ns` / `end_ns` / `elapsed_ns` 允许 null 的组合严格遵循生命周期表。已计时终态必须三个值齐全，且 `end_ns >= start_ns`、`elapsed_ns == end_ns - start_ns`。禁止先做可能溢出的加法来验证差值。
- 真实 `0` 是合法测量值（例如同一时钟 tick 的区间）；它与缺失字段、JSON null、“未启动”、`skipped`、`not_applicable` 明确不同。不得将旧事件 `elapsed_seconds=0` 转换为新纳秒零值。
- 同一 run 的时间由同一进程 steady clock 产生；parser 校验每个 span 内部一致性，不对不同 stage 的大小作一般性比较，也不跨 host/run 相减。

## 新旧 JSONL 消费兼容

旧事件名、旧字段、秒级时间与旧 `elapsed_seconds=0` 语义原样保留；新阶段行使用新增事件名并且每一行（含 `in_progress`、terminal、skip/N/A 和 `stream_identity`）都必须有 `error_code`。正常 start/completed/skip/N/A 使用 `"ok"`；真实 failed 行使用操作的真实分类码。incomplete/evidence gap 若由分析器派生，不制造一条缺失终态 JSON 行；若实现主动写可见 incomplete 记录，必须使用明确 evidence 错误码并保持时间为空。

当前 `tools/demo/run_alpha_demo.py:summarize_event_log` 会逐行解析所有非空 JSON，按 `error_code` 计数，并将缺字段行当成 `unknown_error`、非 `ok` 行设为 `first_error`。因此新行带 `error_code="ok"` 时混合日志不会因字段缺失误报；`event_count` 增加是逐行统计的自然结果，不应过滤新 schema 来维持旧计数。若后续 parser 增加 schema 处理，必须保留旧事件解析并测试混合输入：正常新旧行不出现 `unknown_error`/假 `first_error`；真实错误行仍被计数和定位。

## 后续实现与 QA-02 的最小门禁

实现只能新增本地观察：不改 wire frame、manifest/resume/checksum 顺序、scheduler、默认值或 IO backend。04 的 QA-02 至少须审查/测试：

1. JSONL start→terminal 配对、并发交错、孤立 start、orphan/duplicate/mismatch、截断/坏行和未知 schema 造成 evidence partial；验证 append-only，不回写旧行。
2. run/file_attempt/worker/stream 全部 nullability 组合，以及 control reuse 的稳定 ID、重连的新 ID。
3. 空文件、完整 resume skip/no fake stages、非空 `no_missing_range`、多次 retry 各自计数、连接失败、首块前失败和并发多个 stream。
4. download SessionInit 前 stream_id null、SessionInit 后追加 identity link、link 之后每阶段同时含 slot 和 ID。
5. uint64 边界值、越界、负数、浮点、指数/字符串、start>end、elapsed 差值不一致、真实零值与 null/缺失区分。
6. 旧事件与新 `tree_stage_v1`/`stream_identity` 混合喂给 `summarize_event_log`，校验无虚假 `unknown_error`、真实错误可见、event count 规则清楚。
7. 注入 event start/terminal 写失败，证明 `transfer_status`、`integrity_status`、wire accounting 不变，独立 evidence partial/write-failure count 可观察。
8. telemetry on/off 的真实 tree upload/download 链路对照退出码、源/目标文件与 tree hash、manifest/resume 结果、文件/字节计数及 DATA frame 逻辑内容；时间、ID 和 TCP 分段允许变化。不得用窄 parser/mock 代替真实传输链路。

上述均为后续验收入口建议；本任务没有运行任何测试。Build、CTest、smoke、传输、profile、benchmark、SSH、云端操作和清理均为 `NOT_RUN`。

## 待 01 架构复核与 QA-02 闸门

规范决策已冻结语义，但 01 仍须独立复核后续实现的精确序列化映射，特别是：`scope`/stage/key 的最终拼写；span ID 的生成及 terminal 行重复 start 字段；`control_id` 预留时点；`file_id`/`attempt_id`/`worker_id` 数值宽度和 ordinal 起点；inter-file idle 邻接键；`stream_identity.bound_ns` 与事件公共字段的映射；skipped/N/A/incomplete 的 reason/error code 和 null 时间组合；evidence partial/write-failure 计数的独立输出位置。这里列出的具体键名与宽度是 03 设计映射，需 01 确认后才能作为实现 schema，不是对规范决策缺文处作隐性改写。

当前路线状态仍为：先完成 01 架构复核；再由 04 执行 QA-02 并覆盖本报告清单。QA-02 通过前不得创建 `codex/LOWLEVEL-TREE-TELEMETRY-IMPL-01` worktree 或开始源码实现。01 若发现现有源码事实与冻结契约冲突，应回报 00 修订路线；03 不自行改变规范语义。

## 执行回执

- 实际输入/输出提交：共享 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；固定源码输入 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；写报告前后 HEAD 不变。
- 实际改动：仅新增本结果文件。未改源码、测试、runner、旧决策、BOARD/ROSTER、其他回执、worktree、云端或 Git index。
- 实际命令：`git rev-parse HEAD`（0）；`git status --short --branch`（0）；`git diff --cached --quiet`（0，index 空）；`git diff --quiet 3b0820d..HEAD -- <相关源码路径>`（0）；定向 `rg` / `Get-Content`（0）。写后 UTF-8、尾换行、尾随空白、`git diff --check`、HEAD 与 index 检查在最终验证栏记录。
- 构建、CTest、测试、smoke、传输、性能实验、SSH、云端操作、清理：全部 `NOT_RUN`，符合只读任务边界。
- 下一步：01 独立复核序列化映射；之后 04 执行 QA-02。此设计不声称实现、测试或实验通过。

## 写后验证

- 报告严格 UTF-8 解码成功、以 LF 结尾、尾随空白行数为 0；主题必需词项检查均存在。
- `git diff --check` 退出码 0；仅输出共享工作区既存文件的 LF→CRLF 提示。`git rev-parse HEAD` 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`git diff --cached --quiet` 退出码 0。目标路径 status 仅显示本结果文件为新增，summary 文件不存在。
- CLI 实际命令：`codex exec --ephemeral --output-last-message docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02-last-message.md "仅根据 docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02-result.md 生成简短中文任务摘要；不得修改或创建其他仓库文件。"`，退出码 1。CLI 报告 `state_5.sqlite` 只读、无法初始化 app-server（E_ACCESSDENIED）；没有生成 last-message 文件，也未手写替代摘要。
- 全部代码构建、CTest、实现测试、传输和性能实验保持 `NOT_RUN`。
