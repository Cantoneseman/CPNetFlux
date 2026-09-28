# LOWLEVEL-TREE-TELEMETRY-QA-02：telemetry v2 独立质量复审

路线/任务：`R2026-09-23.5 / v1`

固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`

复审日期：2026-09-23

## 结论

**总体 PARTIAL；不允许 00 现在创建 telemetry 实现任务。** PLAN-02 大体落实了新决策，且 `first_payload`/`payload_io`、retry、resume、整数边界、旧 demo parser 等设计明显比 v1 可审查。但 `data_channel_finalize`、`transfer_complete_wait` 的区间定义互相不可能同时满足其“允许重叠”条款，下载 mtime 的归属也与当前源码顺序冲突；stream identity 事件没有闭合全行 schema；决策/PLAN-02 仍明确留有 01 的序列化映射复核。修正并完成 01 复核后，应按本报告的窄清单再过 QA 门禁。此结论只是设计审查，不是实现、传输正确性、测试或性能验收。

## 输入与只读边界

- 执行时 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与文档输入基线一致；工作树有其他角色既存修改/未跟踪文件，暂存区为空。没有覆盖这些改动。
- 路线决策和 PLAN-02 结果均标 `R2026-09-23.5`；PLAN-02 task、QA-01 结果和 BOARD 均可读。BOARD 第 49–52 行将 PLAN-01 标 superseded、QA-01 标 done/PARTIAL、PLAN-02 标 ready、QA-02 标 in_progress 且注明未授权实现。
- 对本审查涉及的 tree/file transfer 源码、event log、phase stats 与 demo parser 执行 `git diff --exit-code 3b0820d..HEAD -- <paths>`，退出码 0，源码与指定固定基线相符。
- 只新增本结果文件。未改设计、决策、源码、测试、BOARD/ROSTER、共享 index 或其他角色材料。未调用 Codex CLI，因此未生成 `...-last-message.md`。

## 独立验收清单

| # | 结论 | 证据与判断 | 最小闭合条件 |
| --- | --- | --- | --- |
| 1. v1→v2 区间与慢首块例 | PASS | PLAN-02 §“v1→v2 冻结差异”及“固定阶段边界”明确撤销 `first_payload <= payload_io`。连接完成于 0 ms、读盘/调度 790 ms、首个 payload 再耗 10 ms，得 `first_payload=800 ms`、`payload_io=10 ms`，算术及区间解释成立。2026-09-17 旧 profiling 决策相应关系由 2026-09-23 telemetry 决策明确替代。 | 无设计修订；测试仍须覆盖两区间独立计算，当前未运行。 |
| 2. finalize、226、manifest 与源码顺序 | BLOCKED | 决策 §“时间区间与阶段边界”与 PLAN-02 §“固定阶段边界”都把 `data_channel_finalize` 结束点定为文件数据函数返回，把 `transfer_complete_wait` 起点定为该函数返回；按定义它们相接，不能同时“可能重叠”。源码 `tree_transfer_client.cpp:1592–1605` 先调用并等 `runFileDownloadClient` 返回，再读取控制面 226；上传也在 `runFileTransferClient` 返回后调用 `waitTransferComplete`（`:1409,1457`）。此外，PLAN-02 把下载 mtime 纳入 data finalize，但当前 `runTreeDownloadFile` 在 226 成功后才于 `:1616` 调 `setRegularFileMtime`；下载文件函数内部的 flush/verify/rename/commit 在其返回前完成（`file_download_client.cpp:665–755`）。tree Completed manifest 更新发生在后续 `updateRecord`（tree 源码 `:1624`；上传 `:1457–1469`），其责任边界总体合理。 | 由 01/00 冻结一套能对应源码的端点：说明 overlap 是真实区间嵌套还是仅阶段 wall 不可相加；将 mtime 归入实际发生区间或增加清楚的文件 finalize span；再更新决策/PLAN 的冲突表述。不能把相接区间称作可重叠。 |
| 3. append-only、崩溃/坏行与四维隔离 | PARTIAL | PLAN-02 §“Append-only JSONL 生命周期”定义 `run_id+span_id` 配对、start 与 terminal key 相同、孤立 start 派生 `incomplete/evidence_gap`、坏行/孤立 end/重复 start/错配/未知 schema 导致 evidence partial；写失败通过 tree summary/process result 独立暴露。四维表清楚规定 logger 故障只改变 `evidence_status`，不改变 transfer、integrity、wire。尚未明确并发 writer 保证整行原子追加/序列化、重复 terminal 如何拒绝；“flush 到用户态缓冲区”与突然进程退出时保留 start 的保证也需对齐实现语义。 | 冻结单文件 append 的并发写原子性；定义重复 terminal 的 validator 结果；明确 start flush 至何层可保证进程异常退出后可读，并在故障测试中验证。summary/process result 的独立输出位置由 01 复核为可观测。 |
| 4. required/null 与 ID 生命周期 | PARTIAL | PLAN-02 §“Required/null 字段矩阵”给出 run/file_attempt/worker/stream 的关联列和 null 规则，并固定 skip 为 `attempt_id=0, attempt_kind=skip`、真实 transfer 从 1 递增；run 实体字段显式 null。报告 §“待 01 架构复核”仍把 `scope/stage/key` 拼写、span ID、control ID 预留时点、数值宽度与 ordinal 起点、idle 邻接键列为待复核，故不是冻结的实现 schema。 | 01 对矩阵做机械一致性复核，明确 skip decision 的具体 scope/stage/state/span 形状、各 scope 的必填/nullable 组合、ID 分配边界及整数宽度。 |
| 5. 空/skip/no-range/retry/失败/并发状态 | PARTIAL | PLAN-02 §“文件与 stream 生命周期状态”分别描述空文件 N/A、全文件 resume skip、非空 stream `no_missing_range`、retry 新 attempt、首块前失败和并发多 stream，整体语义有区分。全文件 skip 虽有 sentinel ID，但“file decision”究竟序列化成哪条事件/阶段尚未规定；“payload 阶段”对 `first_payload` 与 `payload_io` 两者是否都出 N/A 行也未写成机械规则。 | 明确 skip decision 的单行/阶段形状及不生成的行集合；分别规定 no-range 下两个 payload span 的 state/reason；加入 setup 早退与状态互斥向量。 |
| 6. Download `stream_identity` schema | BLOCKED | PLAN-02 §“全行公共字段”称每行都必须包含 `direction/scope/stage/span_id/state/start_ns/end_ns/elapsed_ns` 等；但 §“Download stream ID 追加绑定”只给 `event=stream_identity`、`schema_version`、若干关联键、`stream_id`、`bound_ns`、`error_code`，未定义其 `scope/stage/span_id/state`、通用时间字段、`direction/path/transfer_id` 等字段是必填、null 还是有意省略。它又不是普通 stage span，却共享 schema version 1。分析器无法据此判断字段校验规则。 | 01 冻结 `tree_stage_v1` 与 `stream_identity` 各自的事件字段集合；明确 identity 是否有自己的关联/link ID、scope/state、时间字段和 nullability，并给有效/重复/错 slot/冲突 stream_id 的验收向量。 |
| 7. uint64 词法、范围与 JSON parser | PASS（规格）/ NOT_RUN（实现） | PLAN-02 §“uint64 纳秒 parser 验收规则”限定十进制无符号词法和值域 `0..18446744073709551615`，拒绝负数、浮点、小数、指数、字符串、越界及浮点精度损失，并要求精确差值。§“后续实现与 QA-02 的最小门禁”列出边界、越界、各错误 token、`start>end`、差值不等与真实 0/null 测试。足以构成测试清单，但 JSON 库尚未由实现选择，测试/原始 token 解析均未执行。 | 实现选择能保留 uint64 原始数值精度的 parser 路径；对 JSON 原始 token 执行所列正反向用例，特别覆盖 `UINT64_MAX`、`UINT64_MAX+1`、小数/指数及禁止经 double 转换。 |
| 8. `run_alpha_demo.py` 混合新旧日志 | PASS（规格）/ NOT_RUN（实现） | PLAN-02 §“新旧 JSONL 消费兼容”准确描述 `summarize_event_log` 逐行读、缺 `error_code` 变 `unknown_error`、非 `ok` 进入 `first_error`；要求所有新行（含 start、terminal、skip/N/A、identity）有 `error_code`，正常行用 `ok`，并规定混合日志验收断言。对应源码 `tools/demo/run_alpha_demo.py:267–286`。 | 执行混合旧/`tree_stage_v1`/`stream_identity` 回归，断言正常新行不生成假错误，真实失败仍可见；当前只是规格通过。 |
| 9. telemetry on/off、logger failure 与真实链路 | PASS（门禁设计）/ NOT_RUN（测试/产品正确性） | PLAN-02 §“后续实现与 QA-02 的最小门禁”分别要求 start/terminal 写失败注入、比较 transfer/integrity/wire 不变且 evidence partial 可见，以及固定输入真实 tree upload/download on/off 对照 hash、manifest/resume、计数与 DATA frame 逻辑内容；并明确不能以 parser/mock 代替真实链路。范围边界清楚。 | 实现后先做 parser/状态单测与失败注入，再做真实 tree upload/download on/off；两类证据分开记录。当前均未执行。 |
| 10. 路线、授权与 01 依赖 | PARTIAL / BLOCKED | 决策、PLAN-02、QA-02 task 和 BOARD 的版本一致为 `R2026-09-23.5`；BOARD 第 52 行写 QA-02 in_progress、尚未授权实现。PLAN-02 §“待 01 架构复核与 QA-02 闸门”仍将最终序列化映射列为 01 待复核并规定 01→QA-02→实现；决策状态也明确 01 尚待独立复核。没有 01 结果可证明这些依赖已冻结，因此当前不能给 design-gate PASS。 | 先取得 01 对 schema/ID/identity/evidence 输出映射的独立结论，并由 00 明确 QA-02 与 01 的先后；再对修订版本复审。不得据本报告宣称正确性通过。 |

## 进入实现设计门前的最小动作

1. 01/00 解决第 2 项的 stage 端点、可重叠性及下载 mtime 归属，并同步规范决策和 PLAN-02。
2. 01 冻结 `tree_stage_v1` 与 `stream_identity` 的独立字段/null 矩阵、span/decision ID 生命周期和 JSON 原始 uint64 parser 约束；完成该复核后再由 04 重审。
3. 补齐 append 并发/重复 terminal/异常退出保证与 skip/no-range 的精确事件形状；将这些作为后续 validator/fault-injection 测试断言。
4. 只在修订设计通过后，00 才可考虑下发窄 instrumentation 实现任务。实现、真实 upload/download、logger failure injection 和 telemetry on/off 验收仍是后续门禁，本报告不授权它们。

## 执行回执

- 实际输入/输出提交：输入固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；写入前后共享 HEAD 均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 实际改动：仅新增本结果文件；未创建 CLI last-message，因为未调用 Codex CLI。
- 实际命令/退出码：`git rev-parse HEAD`（0）；`git status --short --branch`（0）；`git diff --cached --quiet`（0，index 空）；源码固定输入 `git diff --exit-code 3b0820d..HEAD -- <paths>`（0）；指定文档/BOARD/source 的 `rg`、`Get-Content`（0）；写后 `git diff --check`（0，只有共享工作区既存文件的 LF→CRLF 提示）；结果严格 UTF-8 解码（有效）、尾随空白计数（0）、末尾 LF（有）。新增结果 status 为 `??`，符合仅新增白名单输出。
- Codex CLI 可用但本轮未调用；按任务约束未生成 last-message 文件。Build、CTest、单测、smoke、runner、传输、profile、实验、SSH、清理：均 `NOT_RUN`。无实现或产品正确性通过结论。
- 下一步：01/00 先裁定第 2、4、6、10 项并让 03 更新设计；随后 04 再按同一任务清单复审。当前不允许创建实现任务。
