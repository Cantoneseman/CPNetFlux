# LOWLEVEL-TREE-TELEMETRY-PLAN-04 结果：目录阶段 telemetry 实现规格

状态：**BLOCKED：download 首 payload span 的 stream ID 配对规则未冻结；当前不是完整实现规格**

路线版本、任务版本：`R2026-09-24.8` / `v1`

发起人：00 总指挥；执行角色：03 核心实现（既有聊天续接失败后由当前总控完成文档交付）；验收角色：00，随后 04

日期：2026-09-24（Asia/Shanghai）

## 1. 范围、输入和边界

本结果把固定提交上的目录阶段观测收敛为可逐项实现和审查的规格。它只规定事件、计时端点、生命周期、状态轴、解析器和验证门禁；不改变协议、传输、resume、manifest、checksum、scheduler、compression、backend、连接或 worker 默认值。

非目标是源码、测试、runner、CMake、worktree、构建、CTest、smoke、benchmark、SSH、云端实验、lookahead、sendv、manifest 优化以及任何真实 tree upload/download。所有动态项目在本结果中均为 `NOT_RUN`。

输入：

- 固定源码：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。
- 资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 决策：`docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`。
- 前序结果：PLAN-03、QA-02、ARCH-REVIEW-01、ARCH-REVIEW-02。
- 裁定：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-02.md`。

本文件是 PLAN-03 的替代规格，不覆盖或删除 [PLAN-03 BLOCKED 结果](2026-09-23-lowlevel-tree-telemetry-plan-03-result.md)。固定源码至资料 HEAD 的实现路径差异要求为空；当前资料 HEAD 的其他协作文档修改保持原样。

## 2. 事件类型和严格键集

### 2.1 `tree_stage_v1`

`event=tree_stage_v1`、`schema_version=1` 时键集必须**恰好**如下，既不能省略也不能增加：

```text
event, schema_version, run_id, direction, scope, stage, span_id, state,
start_ns, end_ns, elapsed_ns, file_id, attempt_id, attempt_kind, worker_id,
control_id, stream_slot, stream_id, transfer_id, path, previous_file_id,
previous_attempt_id, next_file_id, next_attempt_id, reason, error_code
```

类型和词法：

- `event`、`direction`、`scope`、`stage`、`state`、`attempt_kind`、`transfer_id`、`path`、`reason`、`error_code` 是字符串或 JSON `null`，按下表限制取值；非空字符串不得用空串代替 `null`。
- `schema_version` 是 JSON 整数 `1`。
- `run_id` 是非空 UUID 字符串。`direction` 只能是 `upload` 或 `download`。
- `span_id`、`file_id`、`attempt_id`、`control_id`、`previous_file_id`、`previous_attempt_id`、`next_file_id`、`next_attempt_id` 是十进制 JSON 无符号整数或 `null`；`worker_id`、`stream_slot`、`stream_id` 是十进制 JSON `uint32` 或 `null`。`start_ns`、`end_ns`、`elapsed_ns` 是十进制 JSON `uint64` 或 `null`。
- 所有整数按原始 JSON token 解析，允许 `0` 和 `18446744073709551615`，拒绝负数、浮点、小数、指数、字符串、超过 `UINT64_MAX` 的值及经 `double` 舍入的值。`uint32` 字段另外拒绝超过 `UINT32_MAX`。
- `scope` 只能是 `run`、`file_attempt`、`worker`、`stream`。`stage` 只能是 `run_preflight`、`control_acquire`、`control_prepare`、`data_connect`、`first_payload`、`payload_io`、`data_channel_finalize`、`transfer_complete_wait`、`download_mtime_finalize`、`manifest_finalize`、`interfile_idle`、`file_attempt_decision`。
- `state` 只能是 `in_progress`、`completed`、`failed`、`skipped`、`not_applicable`、`incomplete`。`attempt_kind` 只能是 `transfer`、`skip` 或 `null`。
- `error_code` 在每一行都必填且为非空 snake_case 字符串；正常 start、completed、skipped、not_applicable 使用 `ok`，failed 使用实际非 `ok` 错码，崩溃后派生的 `incomplete` 使用 `incomplete`。

时间和配对：

- `in_progress` 是 start 行：`start_ns` 有值，`end_ns=null`、`elapsed_ns=null`、`error_code=ok`；写入并 flush 到内核页缓存后才进入被测操作。
- `completed` 或 `failed` 必须与唯一 start 配对，复制相同的 `run_id/span_id/direction/scope/stage` 和实体键，`end_ns >= start_ns`，且 `elapsed_ns=end_ns-start_ns`；不得把不同并发 span 相加当作 wall time。
- `skipped` 和 `not_applicable` 是独立单行，三个时间字段均为 `null` 且 `reason` 必填。`skipped` 表示流程未执行，`not_applicable` 表示语义不适用；二者不表示真实零时长。
- `incomplete` 若在有序退出时可写，复制 start 的 `start_ns`、保持 `end_ns/elapsed_ns=null`；进程在 terminal 前退出时 validator 从孤立 start 派生 `incomplete/evidence_gap`，不伪造结束时间。
- 每个 `(run_id, span_id)` 只能有一个 start、至多一个 terminal；重复 start、重复 terminal、孤立 terminal、错配键、未知状态和终态并存均拒绝。`interfile_idle` 只有 `next_*` 允许在 terminal 时从 `null` 补成真实相邻文件；其他字段禁止回写。

### 2.2 `stream_identity`

`event=stream_identity`、`schema_version=1` 是独立的非计时事件，键集必须恰好如下：

```text
event, schema_version, run_id, direction, scope, stage, state, bound_ns,
file_id, attempt_id, attempt_kind, worker_id, control_id, stream_slot,
stream_id, transfer_id, path, error_code
```

固定值和类型：

- `direction=download`、`scope=stream`、`stage=stream_identity`；`state` 只能是 `bound` 或 `failed`。
- `file_id`、`attempt_id`、`worker_id`、`stream_slot` 必填，`attempt_id>=1`、`attempt_kind=transfer`；`control_id`、`transfer_id` 按实际关联填写，不能由请求参数臆造。
- `bound` 时 `bound_ns` 为 `uint64`（`0` 合法）、`stream_id` 为实际协议 `uint32`、`error_code=ok`；`failed` 时 `bound_ns=null`、`stream_id=null`、`error_code` 为实际错误。
- identity 不带 `span_id/start_ns/end_ns/elapsed_ns`。每个 `(run_id,file_id,attempt_id,worker_id,stream_slot)` 至多一条 identity；相同 attempt 内一个 `stream_id` 不能绑定两个 slot。
- `bound` 必须紧跟在对应 `data_connect` 已启动且 SessionInit 已成功解码之后。失败或未获得 SessionInit 的已启动 slot 写 `failed`，不推测 stream ID；旧 `data_connect` 行保持 `stream_id=null`，不回写。

### 2.3 scope、stage 和 null 矩阵

| scope/stage | 必填关联键 | 必须为 `null` | 特别规则 |
|---|---|---|---|
| `run/run_preflight` | `run_id,direction,scope,stage,span_id,state,error_code` | `file_id,attempt_id,attempt_kind,worker_id,control_id,stream_slot,stream_id,transfer_id,path,previous_*,next_*` | 覆盖 manifest load/create、扫描/枚举、run setup；不把文件 worker 阶段塞入 run。|
| `worker/control_acquire`、`worker/control_prepare` | `file_id,attempt_id,attempt_kind=transfer,worker_id,path`；实际连接已知时 `control_id` 必填 | `stream_slot,stream_id` | `control_id` 在 start 前预留；`acquire` 包含新建、失败、复用选择/lease，不能只解释为 socket connect。|
| `stream/data_connect` | `file_id,attempt_id,attempt_kind=transfer,worker_id,stream_slot,path`；已知控制连接时 `control_id` 必填 | `previous_*,next_*` | `stream_id` 在 upload 仅按真实协议事实填写，download 在 identity 前可为 `null`。|
| `stream/first_payload`、`stream/payload_io` | 同 `data_connect` | `previous_*,next_*` | 只对实际数据 slot 计时；空文件或 no-range 用 N/A 单行。|
| `file_attempt/data_channel_finalize`、`transfer_complete_wait`、`download_mtime_finalize` | `file_id,attempt_id,attempt_kind,path`；真实 worker 必填 | `stream_slot,stream_id,previous_*,next_*` | `control_id` 按实际关联；同 attempt 的 finalize 与 wait 相接、不重叠。|
| `file_attempt/manifest_finalize` | `file_id,attempt_id,attempt_kind,path,worker_id`（正常 transfer 或裁定的 attempt-0 exception） | `stream_slot,stream_id,previous_*,next_*` | 只包 `updateRecord` 终态保存，不包 `updateRecordForTransfer`、初始 manifest 或块级 flush。|
| `file_attempt/file_attempt_decision` | `file_id,path,attempt_id=0,attempt_kind=skip,worker_id`、`state=skipped` | `control_id,stream_slot,stream_id,transfer_id,previous_*,next_*`；时间全 `null` | `reason=manifest_completed` 表示入口决定不启动 DATA、进入 gate，不表示 gate 已成功。|
| `worker/interfile_idle` | `worker_id`；start 时 `previous_file_id/previous_attempt_id` 必填 | 文件、attempt、control、stream、transfer、path 全 `null` | start 的 `next_*` 为 null；只有唤醒并领取下一文件后 terminal 才补 `next_*`；worker 尾部没有后继文件不造 idle。|

`attempt_id=0,attempt_kind=skip` 只允许两个形状：入口 `file_attempt_decision`，以及下面 3.3 的 Changed `manifest_finalize` 精确例外。任何真实 data attempt 都从 `attempt_id=1` 开始，retry 递增且失败消耗 ID 不复用；同一文件不能混用 0 与 `>=1`。

## 3. 固定源码的阶段端点和控制顺序

以下映射以固定 `3b0820d` 为准；行号用于复审定位，不是运行结果。

### 3.1 upload

`processUploadFile` 在 `tree_transfer_client.cpp:1315–1358` 先建立文件上下文、stat 和 resume 元数据检查，再调用 `controlForFile:1340–1343`。Completed 记录随后执行远端 SIZE 校验 `:1346–1358`：成功则 skip，变化则 `markChanged/updateRecord(Changed)` 后返回。active 分支才依次执行 acquire slot `:1361–1363`、EPSV `:1365`、可选 REST `:1369–1374`、STOR `:1376–1385`、`updateRecordForTransfer`，然后进入 `runFileTransferClient:1409`。

`runFileTransferClient` 的每个 stream 先建 data socket、SessionInit/ResumeResponse、再按 missing ranges 发送 DATA/FIN/Complete；因此 `data_connect` 和 no-range 判断均发生在 attempt 已开始之后。文件函数返回是 `data_channel_finalize` 的终点；其内部包含 FIN/Complete、flush、最终验证及 commit。若 compression candidate 的首次文件函数失败，源码在 `:1414–1424` 可能先调用该 attempt 的 `waitTransferComplete`，然后在 `:1426–1447` 建立 retry；retry 是新的 attempt/span 集。最终成功路径在 `:1457` 等待 226/最终回复，随后 `updateRecord(Completed):1468`。

### 3.2 download

`processDownloadFile` 在 `tree_transfer_client.cpp:1492–1521` 先 `controlForFile`，Completed 记录校验本地 regular-file、size/mtime 后成功 skip，变化则 `markChanged` 返回。active 分支在 `:1524–1540` 查询远端 SIZE/MDTM 并检查 resume metadata，随后 acquire slot、创建父目录、EPSV、可选 REST、RETR、`updateRecordForTransfer`（`:1542–1571`），最后调用 `runFileDownloadClient:1592`。

下载 receiver 的每个 stream 在 `file_download_client.cpp:374–425` 先连接、读取 SessionInit 得到真实 `stream_id`，追加 `stream_identity` 后准备 missing ranges/ResumeResponse；数据收尾、resize、manifest flush、全量/verified-chunks verify、rename/commit 在 `runFileDownloadClient:665–758` 返回前完成。返回后 `waitTransferComplete:1605` 接收 226；成功后专用 `download_mtime_finalize` 包住 `setRegularFileMtime:1616`；再由 `updateRecord(Completed):1624` 进入 `manifest_finalize`。mtime 不属于已结束的 data finalize。

### 3.3 finalize、226、mtime、manifest 的不变量

- `data_channel_finalize`：本 attempt 所有有数据 stream 的最后一个完整有效 DATA payload 完成时刻的最大值 → 文件传输函数返回。空文件或全 no-range 使用已证明的 `payload_work_closed` → 文件函数返回；非空且首 payload 前失败不造零时长 finalize。
- `transfer_complete_wait`：同一 attempt 的文件函数返回 → 控制面完整解析 226/最终完成回复，失败则到失败返回。两段相接、不重叠；不同文件、worker、stream 的 span 可并发重叠，统计不得相加为 wall。
- `download_mtime_finalize`：download 的 226 成功返回 → `setRegularFileMtime` 返回；226/文件函数失败时写 `skipped/upstream_failure` 或不写，不能伪造成功 span。
- `manifest_finalize`：实际调用终态 `updateRecord(Completed|Failed|Changed)` 的入口 → mutex 内状态更新和 `saveManifest` 返回。`updateRecordForTransfer`、初始 scan/manifest 建立和块级 session flush 排除在外。

### 3.4 preflight 和 `resume_validation` 的范围

- upload 的 manifest load/create、local tree scan、resume source metadata preflight 是 `run_preflight`；download 的初始控制连接、remote enumeration、resume SIZE/MDTM 及本地 Completed metadata preflight 也是 run/preflight 路径（`tree_transfer_client.cpp:2379–2430,2433–2513,1212–1305`）。run preflight 失败只结束 run，不造文件数据阶段。
- worker 领取 Completed 文件后，源码仍可能先执行 `controlForFile`。这段实际 control/local/remote 校验声明为排除的 `resume_validation` scope：不能编码为零时长 `control_acquire/control_prepare`，也不能声称没有控制活动。active 文件实际调用的 acquire/prepare 只记录其自身边界，不回填 gate 时间。
- `resume_validation` 不是 `tree_stage_v1.stage` 值；它在 summary/evidence coverage 中作为 excluded 范围声明。若 gate 终态、Changed 保存或必要验证事实没有独立 summary，`evidence_status=partial`。

## 4. attempt-0 裁定和生命周期向量

### 4.1 两个 attempt-0 规则

1. **Completed gate decision**：worker 领取 manifest 已为 Completed 的文件时立即追加唯一行：`scope=file_attempt, stage=file_attempt_decision, state=skipped, attempt_id=0, attempt_kind=skip, reason=manifest_completed`，`worker_id` 必填，control/stream/transfer ID 为 null，时间全 null。该行只表示不启动 DATA、进入 gate；不得等 gate 结果再写、不得回写或删除。gate 成功和 Changed 结果由 summary/后续真实 span 表达。
2. **Changed manifest finalize exception**：decision 行之后若固定源码实际调用 Changed 终态 `updateRecord`，允许同一 `file_id,attempt_id=0,attempt_kind=skip` 写 `scope=file_attempt,stage=manifest_finalize` 的真实 start/terminal span。`worker_id` 必填；`control_id` 仅在该连接与保存明确关联且已知时填写，否则 null；所有 stream 字段 null。保存失败写 failed；control 获取失败且未调用终态保存不造 manifest span。不得把 decision 行改为 failed，也不得冒用 attempt 1 或 run scope。

### 4.2 完整生命周期表

下表是实现和 QA 必须逐行覆盖的向量。`process_status` 保留现有文件函数/控制面 result、error、exit code；表中的 `completed/failed/blocked` 是摘要分类，不覆盖原始值。

| 向量 | 必须保留/禁止的事件 | `transfer_status` / `process_status` | `integrity_status` / `evidence_status` | 性能资格与 logical/wire 分母 |
|---|---|---|---|---|
| 正常 active、有新 payload、226/终态成功 | worker control acquire/prepare、每 slot data_connect、payload、finalize、wait、（download）mtime、终态 manifest；不把 `updateRecordForTransfer` 当 manifest_finalize | `completed` / result `ok`, exit 0 | 独立全文件 hash 相等才 `pass`；缺 hash `unknown`；事件、summary、logger 完整才 `complete` | 可进 payload 样本；logical denominator 是本 attempt 实际 missing logical bytes，wire 是独立实际计数，不能用 file size 代替 |
| Completed gate 成功 | 一条 attempt-0 decision；无 data_connect、payload、finalize、wait、mtime、manifest span；gate control 属 excluded `resume_validation` | `skipped` / 文件 result `ok`（entry skip） | hash 三值规则；excluded gate 不自动 partial，缺 gate/summary/事件或 logger 错误为 `partial` | 不进 payload throughput；logical/wire 为 `N/A`，已发生的 gate 控制流量不得伪报为 0 |
| Completed gate 判定 Changed | decision；若调用 `updateRecord(Changed)`，一组 attempt-0 Changed `manifest_finalize`；未调用则不造；不造 transfer attempt | `skipped` / `failed` 或 raw result `Changed`、原 error/exit code 保留 | metadata/stat/SIZE/mtime 变化不判内容 mismatch；无独立 hash 为 `unknown`；保存 span 和 summary 完整才 complete，否则 partial | 性能排除；control/wire 若不在声明的计数范围为 out-of-scope，不能填 0 |
| Completed gate control/setup 失败 | decision；`resume_validation` 失败事实在 summary；无成功 control span、无 manifest span（若未调用 updateRecord） | `blocked` / control error、非零或对应 raw result | 独立 hash 通常 `unknown`；若 gate 结果或 summary 缺失则 `partial` | 排除；无可适用 payload denominator，wire 为 `unknown/N/A`，不能造 0 |
| active attempt 全 streams no-range | attempt>=1；保留真实 control、EPSV/STOR/RETR、每 slot data_connect、download identity；每无 range slot 的 first_payload/payload_io 各一条 N/A；证明 payload_work_closed 后 finalize、wait、mtime/manifest 按实际调用 | 新 DATA work `skipped`；文件函数/226 全成功则 process `completed`，否则保留 failed raw result | hash 缺失 `unknown`；事件/identity/terminal/summary 完整且无 logger 错误才 complete；缺 payload_work_closed 或 identity 为 partial | 不进 payload goodput；logical payload denominator=0（仅当 no-range 事实完整），handshake/FIN/226 wire 可非零；无计数证据为 `unknown/N/A`，绝不写 wire=0 |
| active attempt 部分 stream no-range、部分有 range | 同一 attempt；无 range slot 两个 N/A，有 range slot 真实 payload；identity 只绑定真实 download stream | 按整文件函数和 226 结果 `completed` 或 `failed`；不能因一个 slot N/A 把全文件标 skipped | hash 三值规则；所有 slot/terminal 完整才 complete，否则 partial | 仅实际新 missing logical bytes作 denominator；>0 且证据完整才可 goodput；wire 单独按实际计数 |
| 空文件 active transfer | attempt>=1、实际 data_connect；每已预留 slot 的 first_payload/payload_io 为 N/A `reason=empty_file`；payload_work_closed 后 finalize/wait/mtime/manifest 按实际调用 | 新 payload `skipped` 或 0-work 分类；file/226 成功则 process completed | hash 仍须独立全文件比较；没有 hash 为 unknown；logger/terminal 缺失为 partial | 不进 goodput；logical denominator=0；空文件的控制/FIN wire 可非零，缺计数不补 0 |
| data connection 失败 | 已启动 slot 的 data_connect failed；未启动下游为 skipped/upstream_failure；download 无 SessionInit 的 slot 写 identity failed，stream_id 不猜；不写 payload/finalize 成功 | `failed` / connect error、raw result 保留 | hash 通常 unknown；日志完整可 complete，缺终止/写失败为 partial | 排除；无有效 payload denominator；已有实际控制/连接 wire 单独计，缺证据 unknown |
| SessionInit 前或首 payload 前失败 | 连接成功但 SessionInit/ResumeResponse/首有效 DATA 前失败；若 payload I/O 未真正开始则 payload_io skipped，若已开始则 failed；无 valid payload 时 finalize skipped/upstream_failure | `failed` / 文件函数 error | hash unknown；孤立 start 派生 incomplete/evidence_gap | 排除；不能用 0 代替未发生 payload；实际已测 wire 只能按计数器记 |
| upload retry | 首 attempt 失败行不覆盖；若源码实际调用 `waitTransferComplete`，写该 attempt 的 wait failed/completed；新 attempt_id、新 control/stream/span；最终 attempt 完整记录 | 最终 attempt 成功则 transfer completed，首失败保留 raw/process retry 事实；最终失败则 failed | 每 attempt 证据分别检查，hash 只按最终文件独立比较 | 默认排除性能资格；每 attempt logical/wire 分开，不能把 retry wire 当一次 payload 分母 |
| setup/run preflight early return | 已开始的 run_preflight failed；只写真实开始的 run stage，不造 file/worker/data 行；若已分配 worker 的 file stage，按实际失败/下游 skipped | 无可判定 DATA 时 `blocked` / run setup error、exit code 保留 | hash unknown；run summary 可读且 logger 完整才 complete，否则 partial | 排除；logical/wire 均 N/A/unknown，不能造零 |
| manifest finalize/save 失败 | 真实 start 后 terminal failed；不把之前成功的 transfer/hash 改成 failed；未调用的下游不造 | transfer 保留真实 DATA 结果；process failed/save error | hash 独立判；manifest/logger failure 只使 evidence partial，不能推导 integrity fail | 排除本次样本；wire 只按实际计数 |
| worker interfile idle | 前一文件 terminal 后 start；领取下一文件时 terminal 补 next；无下一文件不写尾部 idle | 不改变任一文件 transfer/process | idle 行缺失只影响声明的阶段 evidence，不改文件状态 | 仅可分析 worker idle；不进入 payload/wire denominator |

`resume_validation` gate 的控制流量和文件校验不等于零；如果报告的 wire 范围只覆盖 DATA payload，则必须明确 `out_of_scope` 或 `N/A`。所有状态轴独立，不能从 `transfer_status=skipped` 推出 `integrity=pass`，也不能从 logger failure 推出 transfer failure。

## 5. 四个结果轴与 wire accounting

摘要必须同时保留以下五列，禁止以一个 `result` 覆盖：

| 轴 | 唯一含义 | 允许值/判定 |
|---|---|---|
| `transfer_status` | 本次新 DATA payload work 的结果 | `completed`、`failed`、`skipped`、`blocked`；Completed gate 成功/Changed 和 all-no-range 均可 `skipped`，gate/setup 无法判定是否可传才 `blocked`。|
| `process_status` 与 raw result | 文件函数、控制面 226、mtime、manifest 保存及进程 exit/error 的真实结果 | 原始 result/error/exit code 必须保留；摘要可分 `completed`、`failed`、`changed`、`blocked`，不覆盖 transfer。|
| `integrity_status` | 独立 source/destination 全文件内容 hash | 两端 hash 存在且相等 `pass`；均存在但不同 `fail`；任一缺失或只有 stat/SIZE/mtime/manifest/ResumeResponse/chunk checksum `unknown`。hash 缺失绝不判 mismatch。|
| `evidence_status` | 声明的 telemetry/验证范围是否完整可读 | `complete` 需必需 span/identity、gate/process summary、JSONL 可解析且无 write/rejection/orphan gap；excluded `resume_validation` 本身不使 partial；缺行、坏尾、未知 schema、logger 错误或 summary 丢失为 `partial`。|
| `wire_accounting_status` | 已声明 wire 计数器的适用性和完整性 | 有适用计数且覆盖声明范围为 `complete`，无计数或范围不明为 `unknown`，不适用为 `N/A`；no-range 只令 DATA logical bytes 为 0，不令握手/FIN/226 wire 为 0。|

性能资格是第五个派生列而非状态轴：

- Completed gate、Changed、control/setup failure、empty、all-no-range、retry、首 payload 前失败和 run early return 均 `performance_eligible=false`。
- 部分 no-range 只有实际新 logical bytes>0、hash/evidence/wire 分母完整时才可进入 payload goodput；全 no-range 只能进入握手/finalize 分析。
- stage 分布可以报告 sum/median/p95/max，但不把并发 sum 当 run wall；run wall 由独立 run start/end 计算。

## 6. Append-only logger 与错误隔离

- 每个 run 独占 JSONL sink。先编码完整 UTF-8 单行（建议上限 4096 bytes，含换行），持有 logger mutex 以单次 `O_APPEND` 追加；同一 logger 的线程不能交错，不能另进程共享。
- start/terminal 交给内核页缓存后才继续/返回；不承诺 fsync、掉电或 OS 崩溃持久性。短写、系统错误或编码失败使 sink `poisoned`，递增 `telemetry_write_failure_count`，后续不向可能损坏的尾部追加；不写空行、不覆盖修复。
- 逻辑拒绝（重复 start、重复/矛盾 terminal、未知 span、identity 冲突、未知字段）递增独立的 `telemetry_record_rejection_count`。`orphan_span_count` 由 validator/summary 暴露。任一计数非零都令 evidence partial。
- tree summary 和进程最终 JSON 结果必须在独立内存/输出通道暴露上述计数、`evidence_status`、孤立 span 和 gate/process 结果；JSONL sink 失败不能靠 JSONL 自报。summary 也失败时只能 best-effort stderr，并把结果交付记为 evidence gap。
- logger/validator 错误只影响 `evidence_status` 和证据计数，不改 transfer、process、integrity、wire 或协议帧。

## 7. 确定性 parser/validator 向量

### 7.1 必须接受的正向向量

1. `run_preflight` start：完整 26 键，`start_ns=0`，其他时间/实体按 run null 矩阵。
2. completed span：`start_ns=0,end_ns=0,elapsed_ns=0`；以及 start/end/elapsed 为 `UINT64_MAX,UINT64_MAX,0` 的精确 raw token。
3. `in_progress` start：start 为任意合法 uint64，end/elapsed 明确为 null。
4. `file_attempt_decision`：worker 必填、attempt 0/skip、reason manifest_completed、时间全 null、其余未关联 ID null。
5. Changed exception：decision 后同 file/attempt 0 的 `manifest_finalize` start+terminal，worker 必填、control 按实际已知或 null、stream 全 null、reason=changed；bundle summary 表明实际调用 Changed updateRecord。
6. download `data_connect` start 后的合法 `stream_identity(bound)`，真实 `stream_id` 和 `bound_ns=0` 也合法；后续 stream 行使用相同 slot/ID。
7. no-range/empty 的 N/A 单行：时间全 null、reason 分别为 no_missing_range/empty_file；active no-range 的 attempt>=1。
8. interfile_idle start 有 previous，terminal 只补 next；混合旧事件、合法 `tree_stage_v1` 和 `stream_identity` 的 JSONL 可解析。

### 7.2 必须拒绝的反向向量

- 缺任一严格键、未知键、未知 event/schema/stage/scope/state、空 required string、字符串数值、负数、浮点、小数、指数、`UINT64_MAX+1`、超 `uint32`、`end<start` 或 elapsed 不相等。
- start 缺 end/elapsed null 规则、孤立 terminal、重复 start/terminal、配对 stage/direction/scope/实体键不一致；N/A/skipped 带任何时间数值；in_progress 带 terminal 时间。
- run 行带 file/worker/stream 键；worker/stream/file 行违反 null 矩阵；active stream 缺 slot/control；attempt 0 带 data_connect/payload/identity；no-range 用 attempt 0；empty_file 与 no_missing_range 混用。
- `stream_identity` 缺先前 data_connect、slot 越界/重复、failed 带 ID/bound_ns、bound 缺真实 ID、两个 slot 抢同一 stream ID、后续 stream 行与 binding 冲突。
- Changed attempt-0 manifest span 没有对应实际 Changed updateRecord/bundle summary，或把 control failure 分支伪造成 manifest span；普通 attempt-0 stage 不是上述两个允许形状。
- `first_payload <= payload_io` 作为全局不等式、缺 hash 判 mismatch、no-range 缺 wire 计数补写 `0`、logger failure 改写 transfer/process/integrity/wire。

### 7.2.1 ARCH-REVIEW-01 正反例逐项索引

| ARCH-REVIEW-01 向量 | PLAN-04 的对应规则 | 当前状态 |
|---|---|---|
| run `tree_stage_v1` 样例：完整键集、run scope、`start_ns=0`、实体键全 null | §2.1、§2.3、§7.1-1 | 规格保留，未运行 |
| download `stream_identity` 样例：先有 data_connect，`bound_ns=42`、真实 `stream_id=7`、slot/attempt 绑定 | §2.2、§7.1-6 | 规格保留，未运行 |
| raw `0`、`UINT64_MAX` 及 completed `start=end=0,elapsed=0` | §2.1、§7.1-2 | 规格保留，未运行 |
| raw `UINT64_MAX+1`、负数、`1.0`、`1e0`、字符串 `"1"` | §2.1、§7.2-1 | 规格保留，未运行 |
| `in_progress` start 的 end/elapsed 必须 null；缺省字段不能替代 null | §2.1、§7.1-3、§7.2-2 | 规格保留，未运行 |
| 合法 start+一个 terminal，且 elapsed 精确差值 | §2.1、§7.1-2、§7.2-1 | 规格保留，未运行 |
| 孤立 terminal、重复 start/terminal、end<start、elapsed 错值、错配关联键、坏 JSON、未知字段/schema | §2.1、§7.2-1/2、§7.3 | 规格保留，未运行 |
| stream identity 重复、错 slot、冲突 stream ID、后续行与绑定表冲突 | §2.2、§7.1-6、§7.2-3 | 规格保留，未运行 |
| lifecycle 互斥：attempt 0 skip 或 attempt>=1 transfer/retry；每 slot 至多一组 lifecycle；idle 只允许 terminal 补 next | §2.3、§4.1、§7.2-4 | attempt-0 已按 arbitration 冻结；no-range N/A/start 仍见 §10.1 冲突 |

### 7.3 JSONL 故障策略

解析器逐行读取，保留前面合法记录；坏 JSON、未知 schema/键、重复/错配记录写入隔离诊断（行号、错误码、行 hash），不改写原文件，`evidence_status=partial`。截断尾行不得被旧消费者直接消费；先由新 validator 隔离。合法旧事件和 v1 新事件可混合，旧事件的旧字段/`elapsed_seconds=0` 仍可读。若 summary 丢失、必需 start/identity/terminal 缺失或 logger sink poisoned，证据只能 partial；这些错误不自动改变 transfer/process/integrity/wire。

## 8. telemetry overhead 和 on/off 验收办法

该观测必有非零 CPU、锁、编码和写入开销，验收必须测量而不是假定为 0。实现后在同一固定提交、同一构建、同一输入、同一 loopback 环境进行随机交错的 telemetry off/on：预热 5 次，每模式至少 30 个有效样本；记录 run wall、CPU、RSS、JSONL bytes/records、系统错误计数和 per-file overhead，报告 median、p95、bootstrap 95% CI。

预注册计算：`overhead_wall=(median_on-median_off)/median_off`，并分别给出 CPU/GiB 和记录写入 bytes/GiB；不把日志写入时间并入 payload stage 的语义。QA-04 应在实现前冻结数字预算；本计划默认门为 wall median 增幅不超过 5%、p95 不超过 10%，超门必须由 00/04 重新记录裁定，不能事后改分母。`off` 不产生新 v1 行，`on` 记录完整 span；两组均须通过以下等价性门：退出码、source/destination file/tree hash、manifest 状态与顺序、resume 结果、文件/字节计数、DATA frame 逻辑内容相同；允许时间、ID、TCP segmentation 和 JSONL 记录数变化。

写失败注入必须单独运行：验证 transfer、process、integrity、wire 不被 sink failure 改写，summary/stderr 能暴露 evidence partial 和计数。以上门禁当前全部 `NOT_RUN`。

## 9. QA-02 十项到本计划的索引和实现门禁

| QA-02 | PLAN-04 位置 | 后续实现/测试门禁 |
|---|---|---|
| #1 first_payload / payload_io | §2.1、§3.3、§7 | 两个独立区间、慢首块例、非负/差值；不测试大小不等式。|
| #2 finalize/226/mtime/manifest | §3.1–3.3、§4.2 | upload/download 固定顺序、retry wait、失败/Changed manifest 只按真实调用；mtime 专用 span。|
| #3 append-only、terminal、flush、四维隔离 | §6 | 并发整行、poison/write failure、重复 terminal、summary counters；状态轴不联动。|
| #4 ID/null | §2.1–2.3、§4.1 | 类型/起点/预留、worker decision、Changed exception、null matrix、唯一性。|
| #5 skip/no-range/empty/retry/failure/early return | §4.2 | 全部生命周期 fixture；不造零、不回写、不混 attempt 0/1。|
| #6 stream_identity | §2.2、§4.2、§7 | bound/failed、先 data_connect、重复/错 slot/冲突 ID、后续一致性。|
| #7 uint64 raw token | §2.1、§7.1–7.2 | 0、UINT64_MAX、越界、负数、浮点、指数、字符串和精确 elapsed。|
| #8 alpha demo 兼容 | §7.3 | 合法旧/新混合回归；坏尾先隔离，不让旧 demo 误报 unknown_error。|
| #9 on/off、故障注入、真实链路、overhead | §8 | parser/logger 先行，随后真实 tree upload/download on/off 与 hash/manifest/resume/frame 对照；开销按预注册门。|
| #10 01→03→04 闸门 | §10、§10.1 | §10.1 冲突裁定并形成新规格后才准 QA-04；其 PASS 前不得 implementation/lookahead/build/experiment。|

### PLAN-03 supersede 关系

PLAN-03 的 BLOCKED 原因是固定源码在 Completed gate 和 all-no-range 两处都已发生 control/data 活动，而旧 ARCH 形状要求单条无关联 skip 行；它还未闭合 attempt-0 state、Changed manifest span 和独立状态轴。本 PLAN-04 由 arbitration-02 冻结入口 decision、Changed exception、no-range attempt>=1 和 `resume_validation` excluded 语义，故 PLAN-03 标为 `superseded`，原文和阻断证据必须保留，不得改写成通过或实现输入。

## 10. QA-04 复审门槛、剩余风险和交接

### 10.1 必须先冻结的 download `first_payload` 关联键

固定决策把 `first_payload` 定义为 data connection 完成 → 首个完整有效 DATA payload 完成。下载固定源码在 `file_download_client.cpp:378–382` 建立连接，随后 `:388–395` 才读取/解码 SessionInit 并取得真实 `stream_id`；ARCH-REVIEW-01 §“stream_identity 独立事件契约”规定 SessionInit 成功后追加 identity，旧 `data_connect` 不回写，绑定后的后续 stream 行携带真实 ID。于是同一 `first_payload` span 的 start 必须在连接完成时写出且 `stream_id=null`，terminal 在 payload 完成时 identity 已建立且按后续行规则需要真实 `stream_id`。ARCH-REVIEW-01 又要求 span start/terminal 的所有关联键一致，故当前 strict pairing 同时要求 terminal 的 `stream_id=null` 和 `stream_id=<实际值>`，不可满足。

不能延迟 start 到 SessionInit，否则 `first_payload` 不再包含已裁定的连接后 SessionInit/ResumeResponse 等待时长；不能回写 start，也不能臆造或预先推算协议 ID。当前 arbitration-02 只裁定 attempt-0 decision、Changed finalize、结果轴和 no-range attempt>=1，没有改变此 span 关联键规则。no-range 另有明确的 N/A 单行语义，不要求为不适用阶段先写 start；该点不构成这里的阻塞。

需由 00/01 冻结最小 schema 例外：允许此 `first_payload` span 的 `stream_id` 在 start 为 null、terminal 为真实 bound ID（并明确 validator 配对时该键例外），或规定其他仍保持 append-only、严格字段集且保留时间端点的方式。若不接受任何关联键例外，就必须由 00/01 修订 `first_payload` 的适用起点/观测范围。此选择影响 v1 validator 和 QA 正反向向量，03 不自行决定；路线版本应递增，保留当前 BLOCKED 文件，再据新裁定修订。

因此本文件的字段、状态向量和门禁是已确认部分的实施草案，**不能声称全部验收标准已满足**。在上述裁定并形成无冲突新版规格前，不派 QA-04 做实现准入审查，也不建实现 worktree。

QA-04 必须独立确认：

1. 两个严格键集、类型、uint64 原 token 规则、null/scope/stage/state 矩阵无矛盾；attempt-0 只有裁定的两个形状；并按 00/01 对 §10.1 的裁定验证 first_payload start/terminal stream ID 配对。
2. upload/download 固定源码顺序、finalize/226、download mtime、manifest finalize、preflight、excluded `resume_validation` 和 interfile idle 均可逐项定位。
3. 每个向量有明确事件、五状态轴、性能资格及 logical/wire denominator；缺 hash=unknown，no-range 不造 wire 0。
4. parser 正反向向量、JSONL 隔离、logger failure 和 overhead 门可直接转为实现/测试任务；没有把 NOT_RUN 说成通过。
5. QA-02 十项均有索引；PLAN-03 supersede、剩余风险和下一步责任清楚；源码、协议和实验非目标未越界。

剩余风险：遥测 logger、validator、summary counters、`stream_identity` 和阶段插点尚未实现；服务端内部/runner 外 hash 不在本 tree client scope；resume gate 为 excluded 范围，若后续需要它的 wire 或阶段性能必须另立契约；wire counter 可能只覆盖 DATA payload；实际 overhead、动态生命周期、写失败和真实 on/off 等均未测。完成本规格不构成 correctness、performance 或 implementation readiness。

QA-04 明确 PASS 后，00 才能派发 `codex/LOWLEVEL-TREE-TELEMETRY-IMPL-01` 独立 worktree。下一角色可直接执行：04 读取本结果及 PLAN-03/QA-02/ARCH 两份结果，按上述五项门槛做独立 QA-04；PASS 前不创建实现 worktree。

## 11. 执行回执

- 实际输入 commit：固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 实际输出：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-04-result.md`；未修改 decision、BOARD、ROSTER、架构结果、源码、tests、tools、CMake 或 Git index。
- 03 既有聊天续接尝试：`codex exec resume 01a0af8d-b8fd-7c21-ada2-98637bda893 -`，退出码 1；CLI 报 `failed to initialize in-process app-server client: 拒绝访问。 (os error 5)`，未启动角色轮次，不能报告为已派发或已完成。该命令未使用 `--output-last-message`，没有覆盖任何 last-message 文件。
- 只读核对：`git rev-parse HEAD`、任务资料读取、固定源码调用链读取、固定源码至资料 HEAD 路径差异检查均执行；云端/SSH/构建/CTest/runner/传输/benchmark/动态 parser/真实 tree on/off/故障注入/overhead 全部 `NOT_RUN`。
- transfer/integrity/evidence/wire accounting：已在 §4–§5 按生命周期独立列出；未生成任何运行状态或性能结论。
- 失败/跳过/阻塞：派单 CLI 因本机 app-server 访问权限失败；PLAN-03 BLOCKED 作为 superseded 历史保留；download `first_payload` stream ID 配对尚无裁定；QA-04 未执行，规格/实现准入仍阻塞。
- 下一步：00/01 先裁定 §10.1 的 schema/关联键规则并更新路线版本；03 按新裁定修订后再交 04 QA-04。只有 QA-04 明确 PASS 才能建立 telemetry implementation worktree。

## 12. 写后核验记录

写后必须确认：目标文件 UTF-8、纯 LF、无尾随空白且末尾有 LF；`git diff --check` 通过；HEAD/index 不变；固定源码相对资料 HEAD 的实现路径差异为空。下列命令结果由本轮写入后补录，不把工作区其他既有协作文档改动归入本文件。

- `git rev-parse HEAD`：退出码 0，`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- `git diff --cached --name-only`：退出码 0、无输出；写后 `.git/index` SHA-256 仍为 `B242B58AA3D07B1F065530EEDE81ADEEBC496B1625789008CDE904C193FF57C4`。
- `git diff --name-only 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 a076c532640ba06de016ed7ed20f7d2a6d48a0a7 -- src include tests tools CMakeLists.txt`：退出码 0、输出 0 行，固定源码至资料 HEAD 的实现路径差异为空。
- 结果文件字节核验脚本：退出码 0；严格 UTF-8 解码成功、无 BOM、无 CRLF、末尾为 LF、尾随空白行 0；最终文件大小由下方本轮最后核验输出确认。
- `git diff --check`：退出码 0。输出仅是既有工作树 Markdown 的 LF→CRLF 提示，不是 whitespace error；没有构建、CTest、runner、parser、传输或实验命令。
