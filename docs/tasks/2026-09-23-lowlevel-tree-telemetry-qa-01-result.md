# LOWLEVEL-TREE-TELEMETRY-QA-01：telemetry 契约独立质量复审

路线/任务：`R2026-09-23.4 / v1`
复审日期：2026-09-23

## 结论

**总体 PARTIAL；目前不允许 03 创建 telemetry 实现任务。** 03 的插点与大量边缘状态已足以作为设计基础，且原则上坚持不改变传输语义；但契约还与既有 2026-09-17 profiling 决策有两处直接冲突，并未把 JSONL 突然中断、download stream ID 后知、run-scope nullability 和旧消费者行为冻结为可机械验收规则。先由 01/00 裁定并冻结这些契约，再由 03 更新设计结果和实现任务输入。

## 输入核验与范围

- 任务指定源码基线为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；共享文档 HEAD 执行前后均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。对本次引用的 tree/file/event-log/phase-stats 源码执行 `git diff --exit-code 3b0820d..HEAD -- <paths>`，退出码 0，故源码与固定基线一致。
- 执行前 BOARD 将 `LOWLEVEL-TREE-TELEMETRY-PLAN-01` 标为 review、QA-01 标为 ready；03 结果、00 stage-profile supplement、既有设计 QA 和目录源码映射结果均存在且可读。
- 共享工作区在执行前已有文档修改和未跟踪文件，index 为空。本轮只新增本结果与摘要文件；不改源码、测试、BOARD/ROSTER、既有任务或 Git index。

## 分项判断

| 检查项 | 结论 | 依据与缺口 |
| --- | --- | --- |
| Additive schema、version 与 JSON null | PARTIAL | 03 定义 `tree_stage_v1`、`schema_version=1`，保留旧 event 名/字段，并要求可空值为 JSON `null`，方向正确（结果第 29–48、80、86–90 行）。但 file/attempt 等字段在 run scope 的 nullability 未定义；`attempt_kind=skip` 被使用但未列入必需字段；未知 schema version 和严格分析器应拒绝、保留 unknown 还是降为 evidence partial 未冻结。`uint64` 纳秒以 JSON 整数输出还须要求消费者使用精确整数解析，避免 IEEE-754 double 丢失大整数精度。 |
| steady clock 与跨主机边界 | PASS（原则）/ PARTIAL（机械验证） | 同一 run 内线程共享 `steady_clock` 域、仅同域相减、禁止跨进程/主机比较，避免把 UTC 用作计时；并明确并发 span 不可相加当 wall（第 35、44、48 行）。仍需 validator 断言 `elapsed_ns=end-start`、无负值/溢出及 state 对 null 起止值的完整映射。 |
| run/file/attempt/worker/stream/control/transfer 关联 | PARTIAL | 关联键设计细致，含 control reuse ordinal、worker 外的 null、download 先 slot 后 stream_id 和有效 transfer ID（第 35–46 行）。但 download 的 `data_connect` 结束时尚未收到 SessionInit，append-only JSONL 不能之后回写同一 span 来补 `stream_id`；须冻结为“connect 事件保持 stream_id=null、靠 stream_slot 关联”，或定义独立 link 事件/缓冲写入规则。resume skip 被同时称为非 transfer attempt 的 file decision 与 `attempt_kind=skip`，应明确不伪造传输 attempt 的 ID 规则。另需覆盖非空文件但某 stream 因 range 已完成而没有 payload 的 N/A 原因，不能误记为 `empty_file`。 |
| upload/download 阶段边界 | PARTIAL（等待 01 裁决） | `control_acquire`、`control_prepare`、`data_connect`、首 payload、payload envelope、run prep 与 inter-file idle 均明确区分方向和作用域；`payload_io` 明确是首至末 DATA socket 调用的 envelope，可能包含中间 read/checksum/scheduling gap（第 54–64 行）。但当前名称容易被读成纯 active I/O，`active_payload_io_ns` 又只是可选。另有旧决策冲突，见下方 blocker。 |
| 226 与 data-channel/local finalize 嵌套 | PARTIAL / BLOCKER | 新契约把 download 的 session flush/final verify/rename/commit 归入 `data_channel_finalize`，并嵌在从末 payload 到 226 的 `transfer_complete_wait` 内；tree manifest Completed 另在 226 后计入 `manifest_finalize`（第 59–61 行），这在源码顺序上可解释。但 2026-09-17 决策对 `manifest_finalize` 的边界还包含 manifest flush、mtime/rename/finalize；且旧验收写 `first_payload <= payload_io`，新约定要求二者可交叠、不强制 elapsed 大小关系。01/00 必须明确替代/修订旧验收口径，不能让实现与分析器自行选边。 |
| 空文件、单 payload、resume skip、失败、retry、并发 | PASS（规格方向）/ PARTIAL（测试断言） | 03 为空文件、首块前失败、压缩 raw retry、并发重叠、取消和全部 Completed skip 定义了适用/失败/未开始状态（第 70–77 行）。应补上 per-stream 无 missing range、skip decision 的 ID 规则与首次 manifest/tree setup 失败。建议测试向量完整，但还未实现/执行。 |
| 突然退出、日志丢失 | BLOCKED | 第 78 行称“开始但无 end 的 span”为 `incomplete`，同时第 29 行定义每条记录是单条 stage span；若完整 span 只在阶段结束后 append，进程突然退出时 start 根本未落盘，分析器只能发现 evidence gap，无法知道哪个 span 已开始。须选定两阶段 start/end event、开始时写 durable/incomplete 初始记录后追加终态，或把无记录统一标为 evidence gap（RAII 只能覆盖正常栈退出，不能覆盖进程崩溃）。写失败不得改变 transfer status 的规则正确，但 `evidence_status=partial` 不能只写回同一个已失败的 event log；须指定可独立观察的 summary/退出证据/计数路径，并加可注入写失败测试。 |
| 旧 event 与工具兼容 | PARTIAL | 保留旧 JSONL event 形状和 `elapsed_seconds=0` 原语义是必要的（第 29、94 行）。但现有 `tools/demo/run_alpha_demo.py:267-286` 的 `summarize_event_log` 会逐行读取所有 event，若新 stage 行缺少 `error_code`，会把该行默认为 `unknown_error` 并记为 first error；因此“旧行不变、忽略未知 event”不足以证明现有消费者兼容。需测试旧消费者，或约定新行携带兼容字段/更新消费者。 |
| telemetry on/off 不干扰传输 | PASS（验收设计）/ NOT_RUN | 固定源码/binary、数据和配置对照 transfer status、源/目标 SHA/tree hash、files/logical bytes/DATA frames/wire-byte counters，并明确 TCP 分段、ID、时间戳不必相等（第 92 行），是合适验收方法。尚无实现、测试或运行证据；还应将 telemetry 写失败作为单独故障注入，断言 transfer/integrity/wire 不变且 evidence 状态可观察。 |

## 需要 01/00 冻结的阻塞项与最小修订

1. **解决 2026-09-17 旧验收冲突。** 旧决策 `docs/DECISIONS/2026-09-17-directory-data-plane-profiling.md:19-31,46-53` 将 `manifest_finalize` 定义为含 flush、mtime/rename/finalize，并留下 `first_payload <= payload_io` 验收关系。新契约将 file-session 本地 finalize 独立为 `data_channel_finalize`、把 tree terminal record 独立为 `manifest_finalize`，且明确 first-payload 与 payload envelope 可重叠。01/00 应确认新边界替代旧术语/不等式，或给出可同时满足的精确定义和 validator 断言；04 不代改决策。
2. **冻结完整 JSONL 生命周期协议。** 选择能区分正常终态、已开始但未终态和完全缺证据的 append 格式；写失败标 partial 的信号须有独立于故障 event stream 的观察路径。明确并发写的单行原子性/序列化及分析器按 monotonic 时间而非文件行序解释重叠事件。
3. **补齐可验证 schema。** 冻结每个 `scope/state/stage` 的字段必填/nullable 矩阵、stage 与 state 枚举、`attempt_kind`/skip ID、未知 schema 版本处理、`uint64` 精确整数消费、elapsed 差值规则，以及 download stream_id 的 append-only 关联方式。
4. **补一组最小边缘测试契约。** 除计划中的正常/失败/retry/并发外，加入非空文件某 stream 无缺失 range、setup 早退、日志无法写入、突然中断/截断行、旧 `summarize_event_log` 消费新旧混合日志；在事件写入失败时分列 transfer、integrity、evidence、wire accounting。

完成以上决策与设计修订后，03 才可创建窄 telemetry 实现任务；实现必须只增加本地观察，不改 transfer、checksum/resume、manifest 更新顺序、wire encoding 或 scheduler 行为。实现交付后再由 04 审查代码并执行真实 loopback upload/download 与 telemetry on/off 门禁。当前没有批准实现或实验。

## 执行回执

- 输入/输出提交：共享文档 HEAD 前后均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；源码基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 与所读固定源码路径一致。
- 仅新增：本结果文件与独立摘要文件。未改 BOARD/ROSTER、源码、测试、决策、既有结果、云端或 Git index。
- 实际命令与退出码：`git rev-parse HEAD`（0）、`git status --short --branch`（0）、`git diff --cached --quiet`（0，空）、固定源码 `git diff --exit-code 3b0820d..HEAD -- <paths>`（0）、`git diff --check`（0，只有共享文档既有 LF→CRLF 提示）、定向 `rg`/`Get-Content`（0）；结果文件写后另作严格 UTF-8、末尾换行和尾随空白扫描。
- Build、CTest、单元测试、smoke、传输、profile、benchmark、SSH、云端、清理：均 `NOT_RUN`。未调用 Codex CLI；last-message 文件是本轮独立摘要，不伪称 CLI 自动输出。
- 下一步：01/00 裁决旧阶段定义冲突并冻结 schema/lifecycle 决策；03 据此修订 telemetry 契约，再由 00 派实现任务。未经该门禁不得把结果写成“允许实现”。
