# LOWLEVEL-TREE-TELEMETRY-PLAN-05 结果：按 ARBITRATION-03 收口的实现规格

日期：2026-09-24（Asia/Shanghai）
路线/任务：`R2026-09-24.9 / v1`
状态：**规格已按 ARBITRATION-03 重写；实现闸门仍待 04 对本结果独立复审 PASS。**

本文件只把已冻结的生命周期裁定整理成可机械实现、解析和验收的规则。它替代 PLAN-04 作为下一次 QA 输入，不修改 PLAN-04、决策、源码、测试或任务板；没有实现 telemetry，也没有解除 implementation gate。

## 输入、版本和边界

已读取：

- 本任务单；
- `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`（唯一规范来源，已含 `R2026-09-24.9` 的生命周期裁定）；
- `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md`；
- `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-04-result.md`；
- `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-04-result.md`；
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`；
- 固定提交中的 `src/core/io/tree_transfer_client.cpp`、`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_client.cpp`。

输入源码 commit 为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`，资料 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。旧 PLAN-04/QA-04 文件仍标 `R2026-09-24.8`，而本任务、决策和 ARBITRATION-03 为 `.9`；本结果不回写旧文件。QA-04 报告明确只审查过更早的 PLAN-04 快照，之后文件又变化，因此其 BLOCKED 结论不能作为本结果的逐项 PASS/FAIL 复用，必须由 04 对本稳定输入重新复审。

固定三源码相对 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 无差异。本任务只新增本结果文件；不创建 worktree、不切分支、不改共享 index，不构建、不运行 parser/CTest/传输/性能实验、不 SSH。

## PLAN-04 → PLAN-05 的差异

| 主题 | PLAN-04 的阻塞/旧口径 | PLAN-05 的机械规则 |
|---|---|---|
| first payload N/A | 固定起点要求与“no-range 单行 N/A”冲突。 | data connection 完成时先 append `first_payload/in_progress`；SessionInit/ResumeResponse 后确认 `no_missing_range` 或 `empty_file` 时，追加同一 span 的配对 `not_applicable` terminal，复制 `start_ns`，`end_ns/elapsed_ns=null`。这只是两类允许的 paired N/A；不回写、不补零。 |
| payload I/O N/A | 可能被误认为有连接起点。 | 没有 DATA I/O 起点时只追加普通单行 `payload_io/not_applicable`，三个时间字段全 null；reason 与实际 no-range/empty 对应。 |
| download stream ID | start 与 terminal 关联键可能不一致。 | data-connect/first-payload start 可为 `stream_id=null`；SessionInit 成功后追加独立 `stream_identity`；仅 `first_payload` 的 paired terminal 允许一次性由 null 变真实 ID，按 `(run,file,attempt,worker,slot,stage,span)` 配对；不回写 start。其他 stage 不得回填。 |
| retry control | 旧文本要求每个 retry 新 control，与固定代码同一 ControlClient 重用冲突。 | retry 新 `attempt_id`、新 span、 新 stream slot；沿用实际复用的 `control_id`。只有观察到 control 对象真实 teardown/reconnect 才分配新 control 生命周期。retry 的 control_prepare/等待、wire 和结果独立记账，默认 `performance_eligible=false`。 |
| interfile idle | 末文件、取消和 handoff 错误没有 append-only 终态。 | 上一文件终态后先 append start；连续领取下一文件用 completed 补 `next_*`；尾部/取消用配对 N/A `no_next_file`/`cancelled`；handoff 错误用 failed；不回写、不用零时长。 |
| 兼容与门禁 | 旧 QA 快照尚未覆盖上述裁定。 | 增加 paired-N/A、late-ID、same-control retry、三类 idle fixture、golden JSON bytes、四/五状态轴、wire/eligibility 和 logger failure 门禁；动态项仍 NOT_RUN。 |

## 目标 JSONL 状态机

### 严格事件形状

`tree_stage_v1` 的 `schema_version=1` 键集必须恰好为：

```text
event, schema_version, run_id, direction, scope, stage, span_id, state,
start_ns, end_ns, elapsed_ns, file_id, attempt_id, attempt_kind, worker_id,
control_id, stream_slot, stream_id, transfer_id, path, previous_file_id,
previous_attempt_id, next_file_id, next_attempt_id, reason, error_code
```

整数按原始 JSON token 解析：`span_id/file_id/attempt_id/control_id/previous_* /next_*` 为 uint64，`worker_id/stream_slot/stream_id` 为 uint32，时间和 elapsed 为 uint64；拒绝负数、浮点、小数、指数、字符串数字、超范围值和 double 舍入。`run_id` 为非空 UUID，`direction` 为 `upload|download`，scope 为 `run|file_attempt|worker|stream`，stage 至少包括 `run_preflight`、`control_acquire`、`control_prepare`、`data_connect`、`first_payload`、`payload_io`、`data_channel_finalize`、`transfer_complete_wait`、`download_mtime_finalize`、`manifest_finalize`、`interfile_idle`、`file_attempt_decision`。`error_code` 每行必填、非空 snake_case。

`stream_identity` 是独立的非计时事件，键集必须恰好为：

```text
event, schema_version, run_id, direction, scope, stage, state, bound_ns,
file_id, attempt_id, attempt_kind, worker_id, control_id, stream_slot,
stream_id, transfer_id, path, error_code
```

它固定为 `direction=download, scope=stream, stage=stream_identity`；state 仅 `bound|failed`。bound 要有真实 uint32 `stream_id`、uint64 `bound_ns` 和 `error_code=ok`；failed 的 stream_id/bound_ns 为 null、error_code 为真实错误。每个 slot 至多一个 identity，stream_id 不能绑定两个 slot。

### 通用状态和配对

所有 stage 行由同一进程 `steady_clock` 产生 uint64 纳秒。每个实际开始的 span 先追加并 flush `state=in_progress`，`start_ns` 有值，`end_ns/elapsed_ns=null`，`error_code=ok`；terminal 再追加，不能覆盖原行。`completed|failed` 必须与唯一 start 配对，复制所有关联键，`end_ns>=start_ns` 且 `elapsed_ns=end_ns-start_ns`。每个 `(run_id,span_id)` 只能有一个 start、至多一个 terminal。

普通 `skipped`、普通 `not_applicable` 是单行，时间三元组全 null 且 reason 必填；不代表真实零时长。唯一两个 paired N/A 例外如下：

1. `stage=first_payload` 且 reason 仅为 `no_missing_range|empty_file`。start 在 data-connect 完成时写；terminal 复制 start 的关联键和 `start_ns`，`end_ns/elapsed_ns=null`。download 仅此 terminal 允许 `stream_id` 从 null 变成 SessionInit 得到的真实 ID，且只能一次。
2. `stage=interfile_idle` 且 reason 仅为 `no_next_file|cancelled`。start 在前一文件成功终态后写；terminal 复制 start 和 previous 键，`next_*` 保持 null，`start_ns` 复制，`end_ns/elapsed_ns=null`。

其他 N/A 必须是单行全 null；paired terminal 没有唯一 start、重复 terminal、reason 越界、把 null 改成 0、改变非允许关联键，均为 parser/validator 拒绝并将 evidence 标为 partial。进程崩溃留下孤立 start 时只派生 `incomplete/evidence_gap`，不伪造 end/elapsed；有序退出若主动写 `incomplete`，同样不补时间。

### 固定阶段边界

- `first_payload`：data connection 完成到首个完整有效 DATA payload 完成。没有 DATA 前失败则已有 start 以真实失败终止；若 connect 失败，根本不产生 first-payload start。
- `payload_io`：首个 DATA socket I/O 开始到最后一个完整 DATA socket I/O 结束的包络。无 DATA I/O 时普通 N/A；不能用连接完成时间代替起点。
- `data_channel_finalize`：最后 payload/session 状态（空文件或全 no-range 用已证明的 `payload_work_closed`）到文件传输函数返回；包含 FIN/COMPLETE、download flush/最终验证/rename/commit，不含 FTP 226。
- `transfer_complete_wait`：文件传输函数返回到控制面完整 226/最终回复结束；按同一 attempt 的普通成功路径与 finalize 相接、不重叠。跨文件、worker、stream 的 span 仍可重叠，任何阶段 sum 都不得冒充 wall。
- `download_mtime_finalize`：download 226 成功返回后到 `setRegularFileMtime` 返回；失败或上游未成功则 failed/skipped/upstream_failure，不造成功时长。
- `manifest_finalize`：实际终态 `updateRecord(Completed|Failed|Changed)` 的 mutex 保护状态更新和 `saveManifest` 返回；排除 `updateRecordForTransfer`、初始 manifest、块级 session flush。attempt-0 Changed 只在源码实际调用该终态保存时按窄例外记录。
- `interfile_idle`：上一文件 terminal 后、scheduler handoff/下一文件领取前。global scheduler 的 complete/dispatch/capacity wait 在该 span 内；不把 worker 并发总 wall 复制给每个 idle。

## 生命周期向量与正反例

### no-range、empty 和首块前失败

固定源码中 upload `sendStream`（`file_transfer_client.cpp:390-416`）和 download `receiveStream`（`file_download_client.cpp:374-425`）均先建 data socket，再经过 SessionInit/ResumeResponse 才知道 missing ranges。故 no-range 永远是 `attempt_id>=1, attempt_kind=transfer`，不能回写为 attempt 0。

- no-range stream：保留真实 `data_connect`、SessionInit/ResumeResponse 和（download）identity；`first_payload` 使用 paired N/A `no_missing_range`，`payload_io` 使用普通 N/A；不伪造 DATA。所有 stream no-range 时，只有在实际能证明 `payload_work_closed` 后才记录 finalize；不能证明则 evidence partial。
- 空文件：仍是实际 active attempt；每个已预留 slot 的 first_payload paired N/A、payload_io 普通 N/A，reason=`empty_file`。连接、FIN/COMPLETE、finalize、226、mtime/manifest 按实际调用记录。首个连接未完成即失败时不得伪造 empty N/A。
- 首块前失败：连接成功后在 SessionInit/ResumeResponse/首个有效 DATA 前失败，first_payload 的已写 start 以真实 failed terminal 结束；没有 DATA I/O 的 payload_io 为 `skipped/upstream_failure`，data finalize 也不能写成功零时长。连接本身失败只写 data_connect failed，未启动的下游阶段 skipped/upstream_failure；download 没 SessionInit 的 slot 写 identity failed，不猜 stream ID。

### attempt、span、slot 与 retry 隔离

- 完整 manifest Completed gate：worker 领取时 append 唯一 `file_attempt_decision`，`attempt_id=0, attempt_kind=skip, state=skipped, reason=manifest_completed`，worker 必填，control/stream/transfer ID null，时间全 null。它仅表示“不启动 DATA、进入 validation gate”，不宣告 gate 成功；gate 结果在 summary 中表达，禁止回写。
- 若 gate 后固定源码实际调用 Changed `updateRecord`，才允许同一 attempt 0 写真实 `manifest_finalize` start/terminal；未调用则不造。control 获取失败且没有终态保存也不造 manifest span。
- 首次实际 transfer 是 attempt 1；retry 递增且失败消耗 ID 不复用。每个 attempt 取得新 span ID 和新 stream slot；attempt 1 的任何 start/terminal、wire、error 不得被 attempt 2 覆盖或合并。
- retry 复用固定源码同一 `ControlClient`，所以沿用 `control_id`；retry 的 EPSV/REST/STOR 是新的 `control_prepare`，不是新的 acquire。只有对象真实重建才发新 acquire/control ID。`effectiveTransferId` 只填实际可知的同一 ID；不能因未采用 `retryTransfer` 返回值臆造新 transfer ID。retry 默认 `performance_eligible=false`，每 attempt wire/logical bytes 分开。
- stream slot 预留后即消耗；连接失败、no-range 也不复用 slot。download 只有 SessionInit 成功才产生 bound identity；identity 前 stream_id 为 null，之后同 slot 的 stage 同时含 slot 和真实 ID。

### interfile idle 三类 fixture

`interfile_idle` scope 必为 worker，worker_id 必填，普通 file/attempt/control/stream/transfer/path 全 null；使用 previous/next 键关联相邻文件。

1. **连续文件：** 上一文件最后 terminal 后写 start，`next_*` null；下一文件实际领取前追加 completed terminal，只补真实 `next_file_id/next_attempt_id`（下一文件 gate 为 0，active 为 1），计算真实 end/start 差。
2. **worker 尾部：** start 已写、确认无后继时追加 paired N/A `reason=no_next_file`，next 仍 null，复制 start，end/elapsed null；不进 idle duration 统计。
3. **取消：** start 已写但 stop/cancel 阻止下一文件领取时追加 paired N/A `reason=cancelled`；取消结果在 process/run summary 保留。
4. **handoff 错误：** `completeGlobalDispatch`/`nextGlobalDispatch` 等 handoff 实际报错时追加 failed terminal，真实 end/elapsed、error_code 和 `reason=upstream_failure`；不得伪装 N/A。
5. **前一文件失败：** `processFile` 返回失败时不启动新的 idle；若其他 worker 在 idle 已启动后触发 stop，则按取消 fixture 闭合。

正例必须满足上述 start→terminal、关联键、reason 和时间规则。反例包括：尾部孤立 start、尾部单行 N/A（未有 start）、取消用 completed、handoff error 用 N/A、连续 terminal 猜 next ID、把 idle elapsed 加到 run wall。

## 四/五状态轴、wire 和 eligibility

summary/process result 必须独立输出以下轴，不能相互推导：

| 轴 | 规则 |
|---|---|
| `transfer_status` | 描述 DATA work：预先 Completed gate 是 skipped；active no-range/empty 的新 payload work 可 skipped；实际数据协议/文件函数错误为 failed；不得因无 DATA 把实际成功的文件函数改为 failed。 |
| `process_status` | 保留文件函数、226、mtime、manifest/save 的实际结果、原 error 和 exit code；不被 transfer_status 覆盖。 |
| `integrity_status` | 仅独立源/目标全文件 hash 相等为 pass、两者均有且不等为 fail/mismatch；任一缺失或只有 metadata/ResumeResponse/chunk 局部证据为 unknown。 |
| `evidence_status` | 仅表示声明观测范围、事件配对、summary、parser 和 logger 证据完整度；孤立/坏行/写失败/缺 gate 结果为 partial。 |
| `wire_accounting_status` | 独立记录已观测 logical DATA、控制握手、FIN/COMPLETE/226 wire；no-range/empty 的 DATA logical 可为 0，但握手 wire 可能非零；计数范围外为 unknown/N/A，绝不补 0。 |
| `performance_eligible` | 派生字段。预先 skip、全 no-range、empty、任何 retry、首块前失败和 idle 均 false；仅无 retry、有效 payload、证据完整且 logical denominator>0 的 active attempt 才可 true。 |

并发 span 可重叠；不能把各 stream 或阶段 sum 当 wall。payload goodput denominator 只使用本 attempt 实际 missing logical bytes，不能用文件总大小代替。retry wire 不并入第一次 attempt 的 denominator。

Logger start/terminal 写失败、poison sink、未知 schema/字段拒绝只影响 evidence/evidence write-failure count，不能修改 transfer/process/integrity/wire/frame。summary/process JSON 或 stderr 必须独立尽力暴露 evidence partial 和计数；若该独立路径也失败，结果至少保留 process exit 和可见 evidence gap，不能声称 evidence complete。telemetry on/off 后续真实链路必须比较退出码、双端 file/tree hash、manifest/resume、文件/字节计数、DATA frame logical content；时间、ID、TCP 分段可不同。

## 源码插点与文件边界

以下是后续实现任务的窄插点；本任务没有修改它们。

| 插点 | 固定源码事实与 telemetry 责任 |
|---|---|
| `tree_transfer_client.cpp:2231-2279,2334-2359` | worker/global scheduler 领取、complete/dispatch/capacity wait；固定 worker_id，启动/闭合 interfile_idle，隔离前一文件失败和 stop/cancel。 |
| `tree_transfer_client.cpp:1151-1179` | `controlForFile` 新建/worker reuse/parallelism 调整；control_id 生命周期注册，retry 仅在实际重建时换 ID。 |
| `tree_transfer_client.cpp:1068-1092` | `updateRecord` 与 `updateRecordForTransfer` 分开；只在前者实际终态保存包 `manifest_finalize`。logger 失败不能改现有状态。 |
| `tree_transfer_client.cpp:1307-1476` | upload file decision、Completed gate、active control/REST/STOR、retry、226 wait、终态更新；attempt 0 decision/Changed exception 和 retry attempt 插入此边界。 |
| `tree_transfer_client.cpp:1479-1630` | download 对称路径、Completed gate、file client return、226、mtime、manifest；`download_mtime_finalize` 独立于 data finalize。 |
| `file_transfer_client.cpp:390-487,549-565` | upload 每个 stream 的 data connect、SessionInit/ResumeResponse、missing range、DATA/FIN/Complete；no-range 与每 attempt stream slot 的证据源。 |
| `file_download_client.cpp:211-245,374-425,438-625,632-758` | SessionInit 解码真实 stream_id、identity late binding、missing ranges、接收 DATA/FIN、flush/verify/rename/commit；download first_payload paired ID 例外的唯一来源。 |

未来实现修改白名单（需另发 implementation task，不能在本任务执行）：上述三份 `.cpp`、与 telemetry recorder/event JSON API 直接对应的 `include/cpnetflux/core/...` 和 `src/core/metrics/...` 窄文件、专属 parser/validator 单测文件及其明确 CMake 注册。未来测试白名单限于：完整 `tree_stage_v1`/`stream_identity` parser golden fixture、logger failure injection、scheduler idle/retry 生命周期单测，以及既有 tree upload/download/resume/checksum/TLS smoke 的必要注册。禁止借机改协议、manifest/resume/checksum、scheduler/default/backend、runner 或目录优化；未由新任务逐文件批准的路径不得改。

## Parser golden bytes 与正反例门禁

实现任务必须把下列内容作为原始 UTF-8 JSONL golden bytes，而不是只比较对象结构。示例使用 `\n` 作为行结束；实现可选择固定键顺序，但 golden 文件必须冻结实际输出顺序并逐字节比较。

有效的 upload first-payload start（完整 26 键）应保持所有字段存在：

```json
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000001","direction":"upload","scope":"stream","stage":"first_payload","span_id":41,"state":"in_progress","start_ns":1000,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":7,"transfer_id":"tr-3","path":"a.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
```

有效的 download late-ID paired terminal 必须逐字保留 start 时间，仅允许 stream_id 一次性补实：

```json
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000002","direction":"download","scope":"stream","stage":"first_payload","span_id":42,"state":"not_applicable","start_ns":1000,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":7,"transfer_id":"tr-3","path":"a.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":"no_missing_range","error_code":"ok"}
```

另有一条有效 `stream_identity` golden：

```json
{"event":"stream_identity","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000002","direction":"download","scope":"stream","stage":"stream_identity","state":"bound","bound_ns":1040,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":7,"transfer_id":"tr-3","path":"a.bin","error_code":"ok"}
```

必须拒绝的 golden 变体至少包括：删除任一 required key 或增加未知 key；`start_ns=-1`、`1.0`、`1e3`、`"1000"`、`UINT64_MAX+1`；`end_ns<start_ns` 或 elapsed 不等差；paired N/A 无 start、重复 terminal、reason=`upstream_failure` 用在 no-range；download terminal 把 stream_id 改两次、未有 SessionInit 却写 bound identity；retry 改 control_id 但无 reconnect；idle tail 用 completed/零时长；no-range 写 attempt 0；logger 失败后 transfer/integrity/wire 被改为 failed/zero。所有拒绝只影响 evidence/validator 结果，不改生产传输状态。

## 停止条件、测试顺序和当前未运行项

后续实现遇到下列任一情况必须停止并回报 00/01：

1. 源码控制流无法证明 `payload_work_closed`、真实 control reconnect、next-file handoff 或 terminal `updateRecord` 的边界；不得补估时间或伪造 ID。
2. 需要改变 wire、manifest/resume/checksum、scheduler/default/backend、公开协议或旧日志语义才能满足本规格。
3. 不能在 append-only 下同时保留 paired N/A 的唯一 start、late stream ID 和关联键；不得回写、延迟起点或改为 attempt 0。
4. logger/summary 独立故障路径会改变 transfer/process/integrity/wire，或 parser 无法保留 uint64 原始 token。
5. 发现固定源码、输入 commit、路线版本或本结果指定 schema 与新决策冲突；保留证据并标 BLOCKED，不沿旧 PLAN-04 实现。

建议的后续顺序是：先由 04 对本结果做静态 QA-05（严格键集、golden bytes、状态向量、paired N/A、late ID、same-control retry、idle fixtures）；通过后在固定 commit 创建独立 implementation worktree；先完成 parser/recorder 单测，再实现 scheduler/tree/file 插点；随后运行 logger failure injection、旧 consumer mixed-log 回归、真实 tree upload/download/resume/checksum/TLS smoke；最后做 telemetry on/off bytes/hash/wire/eligibility 对照。以上构建、测试、传输和实验在本任务全部 `NOT_RUN`。

## 执行回执

- 实际输入/输出 commit：输入源码固定为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；输出仍是同一源码 HEAD，新增结果文件未提交。
- 实际改动：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-05-result.md`；未修改 PLAN-04、QA-04、decision、BOARD、ROSTER、源码、测试、runner、worktree、云端或 index。
- 实际命令与退出码：`git rev-parse HEAD`（0）；`git diff --cached --name-only`（0，无输出）；`git status --short --branch`（0）；固定三源码 `git diff --quiet 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp`（0）；定向 `rg`/`Get-Content`（0）。写后 UTF-8、LF、尾随空白、HEAD/index 和 `git diff --check` 将在收口核验中记录。
- 失败/跳过/阻塞：QA-04 旧报告因 PLAN-04 快照变化而不能作为本版本 PASS；实施闸门保持 BLOCKED，等待 04 对 PLAN-05 独立复审。没有新的契约冲突需要把本设计改为不可满足。
- 下一角色可直接执行：04 依据本文件重新做 QA，重点是 paired N/A、late stream ID、same-control retry、四/五状态轴、golden raw bytes 和 logger failure；QA PASS 后才派发实现任务。所有 parser、build、CTest、故障注入、真实传输、hash/resume、性能、SSH 和云端动作本轮均 `NOT_RUN`。

## 写后文档门禁

- 结果文件严格 UTF-8 解码成功、LF 结尾、尾随空白行数为 0。
- `git diff --check` 退出码 0；输出的 LF→CRLF 提示来自共享工作区既有文档，不是本结果文件。
- `git rev-parse HEAD` 退出码 0，仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- `git diff --cached --name-only` 退出码 0、无输出；未操作共享 index。
- 固定三源码 `git diff --quiet 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp` 退出码 0。
- 没有运行 CMake、CTest、parser/validator、Python 回归、logger failure injection、真实上传/下载、resume/hash/frame 对照、性能、SSH 或云端操作；均为 `NOT_RUN`。
