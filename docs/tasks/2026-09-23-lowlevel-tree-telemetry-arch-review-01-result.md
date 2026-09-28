# LOWLEVEL-TREE-TELEMETRY-ARCH-REVIEW-01：架构裁定

路线版本：R2026-09-23.5
任务版本：v1
固定源码输入：3b0820dab6dc149f549bd3e81ef403ea7953c4e9
资料 HEAD：a076c532640ba06de016ed7ed20f7d2a6d48a0a7
日期：2026-09-23；角色：01 架构与需求；交付：00 总指挥

## 结论与证据边界

本回执给出唯一建议映射，供 00 收口后同步到 03 的 PLAN-03。架构映射与 QA-02 的 schema、ID、生命周期、logger 和 parser 阻塞可在设计层闭合；但源码时序证明冻结决策中 mtime 归属及“两个阶段可重叠”的说法不成立。因此第 1/2 项仍需 00 接受本回执的窄语义修订并更新决策，之后 03 同步 PLAN-03、04 重审前不得派实现。此为明确的路线门，不是 01 单方面批准实现。

区分事实与裁定：本轮实读任务指定的决策、PLAN-02、QA-02、ARCH-REVIEW-01、协作入口资料及相关源码；总控任务给定 HEAD。本人核得当前 HEAD 为 a076c532640ba06de016ed7ed20f7d2a6d48a0a7、分支 main、暂存区为空。工作树含其他角色文档修改/未跟踪文件，本回执不覆盖。实际执行固定源码差异检查：git diff --exit-code 3b0820d..HEAD -- src include tools/demo/run_alpha_demo.py，退出码 0，故以下静态映射以指定固定源码为准。未运行测试、构建、runner、传输、profile、实验、SSH 或 CLI；未作实现验收结论。

## 调用顺序与阶段端点裁定

源码事实：tree upload 调用 runFileTransferClient 返回后，成功路径才 waitTransferComplete（读到 226），再 updateRecord(Completed)；compression fallback 路径还可能在 retry 前先等第一次控制完成。tree download 调用 runFileDownloadClient 返回后等待 226，随后 setRegularFileMtime，再 updateRecord(Completed)。download 文件函数返回前已完成流收尾、session flush/验证、临时文件 rename/commit。updateRecord 持 mutex 更新并保存 tree manifest；updateRecordForTransfer 的 Transferring 中间态不是终态 manifest 阶段。

裁定的时间事件如下，所有时间戳取同一个 CPNetFlux 进程 steady_clock：

| 阶段 | 起点 → 终点 | 适用与排除 |
| --- | --- | --- |
| data_channel_finalize | 本 attempt 最后一个完整有效 DATA payload 完成事件（取所有有数据 stream 的最大完成时刻）→ runFileTransferClient / runFileDownloadClient 返回 | 包含数据协议 FIN/COMPLETE 和文件函数内的 flush、最终验证、rename/commit；不含控制面 226。空文件以所有 stream 明确进入零 payload/终态的 payload_work_closed 作为起点；无实际数据函数时记 skipped/N/A，不造零。 |
| transfer_complete_wait | 上述文件函数返回 → 控制连接完整解析到 226/最终完成回复；回复失败则到失败返回 | 与同一 attempt 的 data_channel_finalize 相接，不能重叠。upload retry 若源码实际等待失败 attempt 的控制回复，每次等待单独归属对应 attempt；未调用等待则写 skipped/upstream_failure。 |
| download_mtime_finalize（需新增阶段名） | waitTransferComplete 成功返回后、调用 setRegularFileMtime 前 → 该函数返回 | 仅 download。失败写 failed；此前函数或 226 失败则写 skipped/upstream_failure。它是 tree 层文件元数据收尾，不能塞入已结束的 data_channel_finalize。 |
| manifest_finalize | 调用终态 updateRecord（Completed/Failed/Changed）入口 → mutex 内状态更新和 saveManifest 返回 | 包括该次终态保存成功/失败；排除 updateRecordForTransfer、初始 scan/manifest 建立及块级 session manifest flush。download 的顺序是 mtime 阶段结束后再进入；upload 是 226 后进入。 |

源码中 mtime 确实发生在文件函数返回和 226 成功之后，故旧决策“data_channel_finalize 含下载 mtime”违反调用顺序；旧决策同时称两个相接区间可能重叠也不成立。建议 00 只做上述窄修订：把 mtime 移出 data_channel_finalize 并增加 download_mtime_finalize；明确同一 attempt 的 finalize/wait 不重叠。若 00 不接受新增阶段，本项保持 BLOCKED，不可通过把 mtime 错归给其他 span 来绕过。

跨文件并发、多 stream、其他 worker 之间的 span 可以真实交叠；interfile_idle 只表示同一 worker 相邻文件之间的等待。first_payload 与 payload_io 是不同端点的包络，彼此可重叠且可有 first_payload 大于 payload_io。各阶段 sum、median、p95、max 都是分布报告值，sum 不能冒充 wall 分解；run wall 必须由独立 run start/end 计算。任何跨文件/stream 汇总都不得相加推导 wall。

## tree_stage_v1 严格 JSON 字段契约

schema_version=1 的 tree_stage_v1 必须恰有以下键；字段不得省略或另加。未来字段/语义变更升为 schema_version=2（或新 event 名），v1 不做静默扩展。

字段及类型：
- event 固定字符串 tree_stage_v1；schema_version 整数 1；run_id 非空 UUID 字符串；direction 为 upload 或 download。
- scope 为 run、file_attempt、worker、stream；stage 为 run_preflight、control_acquire、control_prepare、data_connect、first_payload、payload_io、data_channel_finalize、transfer_complete_wait、download_mtime_finalize、manifest_finalize、interfile_idle、file_attempt_decision 之一。
- span_id 为 run 内 uint64，范围 1..UINT64_MAX；state 为 in_progress、completed、failed、skipped、not_applicable、incomplete 之一。
- start_ns、end_ns、elapsed_ns 均为 JSON uint64 数字或 null；不接受字符串数值、负值、浮点、小数、指数或超范围。
- file_id、attempt_id、worker_id、control_id、stream_slot、stream_id 分别为 uint64、uint64、uint32、uint64、uint32、uint32 或 null。attempt_kind 为 transfer、skip 或 null；transfer_id/path 为非空字符串或 null。
- previous_file_id、previous_attempt_id、next_file_id、next_attempt_id 为 uint64 或 null；reason 为非空字符串或 null；error_code 为非空 snake_case 字符串。
- 所有键始终输出；无关联实体必须 JSON null，不能以缺省、空串、0 代替。

scope/null 规则：
- run/run_preflight：run_id、direction、scope、stage、span_id、state 和 error_code 必填；file/attempt/worker/control/stream/transfer/path 及四个邻接键全 null。
- file_attempt：file_id、attempt_id、attempt_kind、path 必填；worker/control 在分配后必填，否则 null；stream 字段 null。attempt_kind=transfer 当且仅当 attempt_id>=1；完整 skip 仅允许 attempt_id=0、attempt_kind=skip、stage=file_attempt_decision。
- worker/control_acquire、control_prepare：绑定文件时 file_id/attempt_id/path/worker_id 必填，control_id 必须在 start 前已确定；stream 字段 null。复用保留实际连接 control_id，新建或重连使用新 ID。
- stream/data_connect、first_payload、payload_io：file/attempt/path/worker_id/stream_slot 必填，attempt_kind=transfer；已取得的 control_id 必填；download 的 stream_id 在身份绑定前可 null，绑定后所有后续 stream 行必填。upload 仅当源码事实已知时填真实协议 ID，否则 null；绝不由 slot 推算。
- worker/interfile_idle：worker_id 必填；file_id、attempt_id、attempt_kind、control/stream/transfer/path 均 null；start 行 previous_* 必填、next_* 为 null，terminal 行只允许把 next_* 从 null 补成实际相邻 file/attempt。此二字段是唯一允许 start/end 延后补值的键；没有后继任务的 worker 尾部空闲不记 interfile_idle。
- file_attempt_decision 的完整 resume skip 必须有 file_id/path、attempt_id=0、attempt_kind=skip，所有 worker/control/stream/transfer 与邻接键为 null。

事件状态与时间组合唯一规则：
- in_progress：start_ns 必须为整数，end_ns/elapsed_ns 必须为 null，error_code=ok；写成功并 flush 后才进入被测操作。
- completed/failed：必须匹配唯一 start 且复制相同 start_ns 与关联键；end_ns 为整数且 >= start_ns；elapsed_ns 精确为 end_ns-start_ns。completed 的 error_code=ok；failed 必须为实际非 ok 错码。
- skipped/not_applicable：独立一行，三个时间均 null，需 reason；前者为流程未执行，后者为语义不适用；error_code=ok。
- incomplete 若能在有序退出时写出，必须匹配 start、复制 start_ns，end/elapsed 为 null，error_code=incomplete；进程崩溃留下孤立 start 时由 validator 派生 incomplete/evidence_gap，不伪造 terminal 时间。
- 每个 (run_id,span_id) 只能有一个 start 和至多一个 terminal；skip/N/A 是单行终态，不能再有 start/terminal。stage、direction、scope 和除 interfile_idle next_* 外所有关联键必须配对一致。completed/failed、skip、N/A、incomplete 互斥。

## stream_identity 独立事件契约

stream_identity 不是 duration span，单独采用 event=stream_identity、schema_version=1，严格字段集合如下（不能带 span_id/start_ns/end_ns/elapsed_ns，也不能漏掉 direction、scope、stage、path、transfer_id、error_code）：

event, schema_version, run_id, direction, scope, stage, state, bound_ns, file_id, attempt_id, attempt_kind, worker_id, control_id, stream_slot, stream_id, transfer_id, path, error_code。

固定值为 event=stream_identity、schema_version=1、scope=stream、stage=stream_identity、direction=download；state 仅 bound 或 failed。file_id/attempt_id>=1/path/worker_id/stream_slot 必填，attempt_kind=transfer，control_id/transfer_id 按已知关联填写，其他字符串不得为空。bound 时 bound_ns 是 uint64（0 有效）、stream_id 是实际协议 uint32、error_code=ok；failed 时 bound_ns 和 stream_id 必须 null、error_code 为实际非 ok。每个 (run,file,attempt,worker,stream_slot) 最多一条 identity 结果；bound 必须对应先前 data_connect start 的 slot。相同 attempt 内一个 stream_id 不能绑定两个 slot。成功解码 SessionInit 后立即 append，旧 data_connect 行保持 stream_id=null，不回写；失败/未获 SessionInit 仅在该 slot 已启动连接时写 failed 身份结果，不推测 ID。

可机械验证的最小有效样例（字段顺序无语义）：

~~~json
{"event":"tree_stage_v1","schema_version":1,"run_id":"550e8400-e29b-41d4-a716-446655440000","direction":"download","scope":"run","stage":"run_preflight","span_id":1,"state":"in_progress","start_ns":0,"end_ns":null,"elapsed_ns":null,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":null,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"stream_identity","schema_version":1,"run_id":"550e8400-e29b-41d4-a716-446655440000","direction":"download","scope":"stream","stage":"stream_identity","state":"bound","bound_ns":42,"file_id":0,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":1,"stream_slot":0,"stream_id":7,"transfer_id":"tr-1","path":"a.bin","error_code":"ok"}
~~~

## ID 作用域、分配与失败行为

| ID | 类型/作用域/起始 | 预留时点与唯一性 |
| --- | --- | --- |
| run_id | UUID 字符串；一次 tree client run | 校验 direction/基本参数后、run_preflight 前生成；跨 run 唯一。 |
| file_id | uint64；run 内 manifest 文件 ordinal，0 起 | manifest/planning 固定顺序即确定；retry/resume 不变。 |
| attempt_id | uint64；每个 file_id 独立 | 首次实际 file transfer attempt=1；每次实际 retry 递增；0 保留为唯一完整 resume skip sentinel。不得在同 file 同时出现 0 与 >=1。 |
| worker_id | uint32；run 内 worker slot，0 起 | worker pool 创建时固定；任务重用该 slot ID，不按文件递增。worker 外 preflight 为 null。 |
| control_id | uint64；run 内具体控制连接生命周期，1 起 | 新连接对象/连接尝试开始前分配，失败也消耗该值且永不复用；worker reuse 命中时记录该现存连接 ID。acquire span start 前须已知将关联的 control ID；不能等返回后改写配对键。 |
| stream_slot | uint32；file attempt 内逻辑计划 slot，0 起 | 对每个计划数据流先按确定顺序预留，再判定 missing range；连接失败或该 slot 无 range 均消耗且不复用。未计划的流不造 slot/span。 |
| stream_id | uint32；协议实际 stream/session ID | 不由本地分配；收到/构建协议事实时绑定。attempt 内各 slot 唯一；失败时 null。 |
| span_id | uint64；run 内全局，1 起 | 每个计时 span 或 skip/N/A 决策行写入前预留；不因 JSONL 写失败或 retry 回收；run 内唯一。溢出只置 evidence partial，不影响传输。 |
| idle adjacency | file/attempt uint64；worker 相邻关系 | previous 在 idle start 前已知，next 在唤醒并接到下一文件时补入 terminal；仅此键允许终态补值。 |

transfer_id 始终是实际有效传输 ID 的非空字符串或 null，不由请求参数臆造。control acquire 无法在 start 前确定复用目标时，必须先解决连接选择/注册与观测边界；不得先写 null 后在 terminal 偷换实际 control_id。若实现不能满足该条件，报告 evidence partial 并在实现设计返回 00，不放宽 parser。

## 固定生命周期行形状

- 全文件 resume 已完成：一条 file_attempt_decision，scope=file_attempt、state=skipped、attempt_id=0/kind=skip、reason=resume_complete、时间全 null、worker/control/stream null；不产生 data_connect、first_payload、payload_io、data_channel_finalize、transfer_complete_wait、download_mtime_finalize。若没有实际终态 updateRecord，也不产生 manifest_finalize。
- 空文件但仍实际执行 transfer：attempt=1+，各真实连接照常记录 data_connect；每个已预留 stream_slot 的 first_payload 与 payload_io 均为 not_applicable/reason=empty_file、时间 null。文件级 finalize/wait/下载 mtime/终态 manifest 按实际调用写 measured span；没有执行的阶段记 skipped/upstream，不写虚构耗时。空文件不是 resume skip。
- 非空文件的某计划 stream 没有 missing range：该逻辑 stream_slot 已在范围判定前预留；若尚未连接，data_connect、first_payload、payload_io 都是 not_applicable/reason=no_missing_range；若连接已真实发生，data_connect 按实测记录，两个 payload stage 为 not_applicable/no_missing_range。未计划的 stream 不分配 slot、不写行。若整个文件没有任何 missing range，则使用完整 resume skip 行，不能同时产生 transfer attempt 或 stream 行。
- setup 早退：已开始的 run_preflight 以 failed 终止，只有该阶段行，不分配 file/worker/control/stream ID，也不造文件/数据阶段。若在已分配 worker 的 file attempt 内失败，实际失败 stage 写 failed；未进入的下游阶段可各写一条 skipped/upstream_failure（全 null 时间，分配唯一 span_id），但绝不写 in_progress/成功计时 span。data_connect 未调用就没有其 timed span；文件函数未调用就没有 data_channel_finalize。
- retry：新 attempt_id 与新 span_id；前一 attempt 的失败/成功终态不可覆盖、拼接或聚合成一个 elapsed。cancel/进程崩溃时保留可写的 start，孤立 start 由 validator 标 incomplete。

## Append-only logger、错误隔离与结果暴露

- 每个 run 独占一个 logger/path；禁止另一个进程并发写同一文件。先在内存编码完整 UTF-8 JSON 加换行（单行上限 4096 bytes），再持有 logger mutex，以 O_APPEND 单次系统调用追加整行；同一 logger 的并发线程不会交错。若短写/系统错误，计一次 write_failure、把 logger 标 poisoned，后续行不再追加到可能损坏的尾部；证据状态转 partial。不能以追加空行或覆盖修复。
- start 行成功交给内核页缓存后才允许进入被测 stage；terminal 同样在返回上层前交给内核。保证级别是用户态缓冲已提交到内核，不承诺 fsync、掉电或 OS 崩溃持久性。孤立 start 只代表当时 start 写成功，不能推导 stage 结束时间。
- logger 状态机拒绝重复 start、重复/矛盾 terminal、未知 span；不写第二个 terminal。此类逻辑/validator rejection 增 record_rejection_count，和 OS write_failure_count 分开；任一非零令 evidence_status=partial。stage 行错误不能改变 transfer_status、integrity_status、wire_accounting 或协议字节/帧。
- tree summary 与进程最终 JSON 结果须在独立输出通道暴露 evidence_status、telemetry_write_failure_count、telemetry_record_rejection_count、orphan_span_count。进程 exit/result 的 transfer 语义保持原判定；遥测错误只使 evidence partial。若 JSONL sink 失败，不能依赖该 JSONL 自报失败，应由内存结果对象/最终 summary 与 stderr 诊断暴露计数。summary 若也不可写，只能 best-effort stderr，必须留为结果交付缺口。
- 旧 demo 兼容边界：run_alpha_demo.py 逐行 json.loads 并统计 error_code；所以每个新 stage/identity 行都必须有 error_code，正常行严格为 ok，失败保留实际错码，旧 event/字段/elapsed_seconds=0 原样可读。完整合法的混合旧/新 JSONL 不会因新字段缺省制造 unknown_error；summary 的 event_count 增长是预期。该旧消费者不容忍截断坏 JSON 行；发生 poisoned/torn tail 时必须先由新 validator 标 partial 并隔离，不把坏文件直接交给旧 demo。旧消费者兼容不等于遥测写入已验收。

## Parser/validator 最小正反向向量

1. 接受 raw JSON 整数 0；接受 start=0/end=0/elapsed=0 的 completed span。接受 UINT64_MAX 精确十进制 token；例如 start=end=18446744073709551615、elapsed=0 合法。拒绝 UINT64_MAX+1、负数、1.0、1e0 和字符串 "1"；必须按原始 JSON 数字词法读入 uint64，禁止经 double。
2. 接受 in_progress 的 start_ns=0、end_ns=null、elapsed_ns=null；拒绝用缺省字段替代 null。对 skipped/N/A/incomplete 组合按上表判定；null 不等于 0，in_progress/failed 不能伪装成 N/A。
3. 接受同 key 的 start + 一个 terminal 且 elapsed=end-start；拒绝孤立 terminal、重复 start、重复 terminal、start/end stage 或关联键不匹配、end<start、elapsed 错值、坏 JSON 行、未知字段、未知 schema。孤立 start 可解析，但派生 incomplete 并令 evidence partial。
4. 接受上列 stream_identity 样例，前提是先有同 run/file/attempt/worker/slot 的 data_connect start。拒绝同 tuple 第二条 identity、identity 指向不存在/越界 slot、bound identity 的 stream_id=null、failed identity 带 stream_id/bound_ns、相同 attempt 中两个 slot 抢同一 stream_id，以及后续 stream stage 对应 slot 或 stream_id 与绑定表冲突。
5. 生命周期互斥：file 的唯一 attempt 形态为“一个 attempt_id=0 skip”或“attempt_id 从 1 开始的 transfer/retry 序列”，不能混合；skip 不得带 worker/control/stream 行。stream slot 每 attempt 至多一个 data_connect lifecycle、一个 first_payload lifecycle、一个 payload_io lifecycle；每 span 只允许 in_progress→单一 completed/failed/incomplete，或单行 skipped，或单行 not_applicable。completed/failed/skipped/N/A/incomplete 不可共存。worker interfile_idle 的 start 必须有 previous，终态才有 next；最终 worker shutdown 不伪造 next。

## QA-02 条目逐项关闭状态

| QA-02 条目 | 本裁定 |
| --- | --- |
| #1 first_payload/payload_io | 架构 PASS；撤销大小不等式。实现/测试未运行。 |
| #2 finalize/226/mtime/manifest | BLOCKED；上文给出唯一修正建议，须 00 接受新增 download_mtime_finalize 并同步决策/PLAN-03，随后 04 复核。 |
| #3 append-only、重复 terminal、flush、四维隔离 | 规格闭合；实现与 crash/write-failure 注入未运行，故仅设计关闭。 |
| #4 ID/null 矩阵 | 规格闭合；file ordinal、各 ID 类型、起始值和预留时点已固定；control 目标必须在 start 前可知。 |
| #5 skip/no-range/空文件/retry/setup 早退 | 规格闭合；单行决策、N/A 与未启动阶段规则已列。实现调用形态仍需按规格自测。 |
| #6 stream_identity | schema 设计闭合；独立字段集合、状态、时间、关联键和重复/冲突规则已列；验证未运行。 |
| #7 uint64 parser | 规格 PASS；严格 raw-token 要求和边界向量已列，parser 未运行。 |
| #8 alpha demo 兼容 | 对合法混合 JSONL 规格 PASS；坏尾行需先隔离；兼容回归未运行。 |
| #9 telemetry on/off、真实链路及 failure injection | 维持实现/QA 门禁；未运行、未验收。 |
| #10 01→03→04 闸门 | BLOCKED 至 00 收口、03 PLAN-03 修订和 04 明确 PASS；不得派 implementation。 |

## 下一步与写后核验

请 00 对 download_mtime_finalize 及同 attempt 区间不重叠这一窄修订作决定；接受后同步决策和 PLAN-03，再派 04 独立复核以上字段、ID、状态、logger 与 parser 向量。只有 QA 明确 PASS 才能创建实现任务。本回执不授权实现或实验。

写入范围仅为 docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-01-result.md。写入前确认资料 HEAD a076c532640ba06de016ed7ed20f7d2a6d48a0a7、index 为空、固定源码至 HEAD diff 为空；写后须再次核对 HEAD/index/source diff，检查 UTF-8、末尾 LF、无尾随空格及 git diff --check。下面的执行结果仅记录实际完成的命令，不扩大为实现验收。
