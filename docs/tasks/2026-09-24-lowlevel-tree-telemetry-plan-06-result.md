# LOWLEVEL-TREE-TELEMETRY-PLAN-06 结果：规格验收向量与性能门

日期：2026-09-24（Asia/Shanghai）
路线/任务：`R2026-09-24.9 / v1`
状态：**四项 QA-05 阻塞已转成机械规格；实现、构建和动态验收仍未授权。**

本结果只新增固定的 schema/null 矩阵、golden JSONL、logger 故障注入契约和 telemetry on/off 性能门。它不修改 PLAN-05、决策、源码、测试、runner、BOARD/ROSTER 或 Git index。

## 输入与版本核验

已读取并核对：

- `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-05-result.md`；
- `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-05-result.md`；
- `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md`；
- `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`；
- 本任务单以及固定提交中的 tree/file transfer 源码。

输入源码 commit 为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。读取前后四份输入文件 SHA-256 未变化：PLAN-05 `09356e014CAB4CB9DF097C3728AE055FAA929EEBADF08F104B09CF4D7AAFED0A`、QA-05 `08B4EBFE0EBE8B87BB5D43A66EE0FB51B0F5FF8DCE4DE97CF483FABB6E429F81`、ARBITRATION-03 `75A3FE5DFBF82D2D3237FA0205F23B99470235D361A82FC84CCD4C2313BCC9FC`、decision `C55991CD0781B24B24E6B05C295329B41B5DFF23F6149868366FF8A8596F928C`。四份文件均为本轮实际读取输入；未见输入变化或新契约冲突。

所有新 JSONL 均使用 UTF-8、无 BOM、固定键顺序和单个 LF 行尾。实现必须按原始 JSON token 解析整数，不能先转 `double`。下文的完整行就是 validator 的 golden bytes；代码块中的每个换行均代表一个 `0x0A`。

## 1. 封闭 schema 与 scope/stage/state 矩阵

### 1.1 严格键集

`tree_stage_v1` 的键集必须**恰好**为下列 26 个键；缺键、未知键、重复键、额外键均拒绝：

```text
event, schema_version, run_id, direction, scope, stage, span_id, state,
start_ns, end_ns, elapsed_ns, file_id, attempt_id, attempt_kind, worker_id,
control_id, stream_slot, stream_id, transfer_id, path, previous_file_id,
previous_attempt_id, next_file_id, next_attempt_id, reason, error_code
```

`stream_identity` 的键集必须**恰好**为下列 18 个键：

```text
event, schema_version, run_id, direction, scope, stage, state, bound_ns,
file_id, attempt_id, attempt_kind, worker_id, control_id, stream_slot,
stream_id, transfer_id, path, error_code
```

这里按 ARCH-REVIEW-01 的逐项字段集合计数为 18；QA-05 将同一逐项列表文字称为“19 个键”是算术笔误。不能为凑 19 增加 `span_id`：ARCH-REVIEW-01 明确 `stream_identity` 不带 span/time 字段。若 04/00 要求不同字段集合，必须先形成新的架构裁定；本结果不隐式改 schema。

所有行都必须有 `event`、`schema_version` 和 `error_code`。`tree_stage_v1.schema_version=1`；`stream_identity.schema_version=1`。字符串不得用空串代替 null。整数 token 规则如下：`span_id/file_id/attempt_id/control_id/previous_* /next_*` 为 uint64；`worker_id/stream_slot/stream_id` 为 uint32；`start_ns/end_ns/elapsed_ns/bound_ns` 为 uint64。接受十进制整数 `0..UINT64_MAX`（uint32 字段为 `0..UINT32_MAX`），拒绝负数、浮点、小数、指数、字符串数字、溢出和浮点舍入后的 token。

### 1.2 状态时间矩阵

下表适用于所有 `tree_stage_v1` 行；“空”表示 JSON `null`，不是省略字段。

| state | start_ns | end_ns | elapsed_ns | reason | error_code | 配对规则 |
|---|---|---|---|---|---|---|
| `in_progress` | required，真实 steady-clock 读数 | null | null | null | `ok` | 一次 start；append 后 flush 再进入被测操作。 |
| `completed` | required | required，且 `end>=start` | required，严格等于 `end-start` | null | `ok` | 必须唯一匹配同 `(run_id,span_id)` 的 start。 |
| `failed` | required | required，且 `end>=start` | required，严格等于 `end-start` | 该 stage 允许的失败 reason | 非 `ok` 的实际错误分类 | 必须唯一匹配 start；不把 logger 错误伪装成传输错误。 |
| 普通 `skipped` | null | null | null | stage 允许的 skip reason | `ok` | 单行；表示流程没有执行。 |
| 普通 `not_applicable` | null | null | null | stage 允许的 N/A reason | `ok` | 单行；表示语义不适用。 |
| paired `not_applicable` | 复制唯一 start 的 start_ns | null | null | 仅两个窄集合 | `ok` | 只允许 `first_payload` 或 `interfile_idle`；不得回写 start。 |
| `incomplete` | 已有 start 时复制其值，否则 null | null | null | `evidence_gap` | `incomplete` | 仅实现主动写出时接受；崩溃通常由 validator 从孤立 start 派生，不造 JSON terminal。 |

`stream_identity` 不带 span 或 elapsed：`state=bound` 要求 `bound_ns`、真实 stream_id、error_code=`ok`；`state=failed` 要求 `bound_ns=null`、`stream_id=null` 和非 ok 实际错误。每个 `(run_id,file_id,attempt_id,worker_id,stream_slot)` 至多一条 identity；同一 attempt 的真实 stream_id 不得绑定两个 slot。

### 1.3 scope、stage、字段与 reason 矩阵

所有 26 个键始终出现。下表的 “required” 是必须非 null；“null” 是必须为 JSON null；未列出的非空字段也属于 forbidden。`direction`、`run_id`、`span_id`、`state`、`error_code` 在每一行均 required。

| scope / stage | 允许 state | required（除公共键） | 必须 null / forbidden 非空 | reason 枚举与 attempt/ID 规则 |
|---|---|---|---|---|
| `run/run_preflight` | in_progress, completed, failed, incomplete | direction、stage、span_id | file_id、attempt_id、attempt_kind、worker_id、control_id、stream_slot、stream_id、transfer_id、path、previous_*、next_* | completed reason null；failed `manifest_error|io_error|upstream_failure`；所有 attempt/实体 ID null。 |
| `worker/control_acquire` | in_progress, completed, failed | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、control_id、path | stream_slot、stream_id、previous_*、next_* | failed `control_error|io_error|protocol_error|upstream_failure`；control_id 在 start 前预留，retry 复用同一真实 control 时不换 ID。 |
| `worker/control_prepare` | in_progress, completed, failed | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、control_id、path | stream_slot、stream_id、previous_*、next_* | failed `control_error|io_error|protocol_error|upstream_failure`；同一 control 的 retry 只新建 prepare span。 |
| `stream/data_connect` | in_progress, completed, failed, skipped | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、stream_slot、path | previous_*、next_* | control_id 若该 tree attempt 已关联则 required；upload 的 stream_id 必须是真实值，download 在 identity 前必须 null；failed `io_error|tls_error|protocol_error|upstream_failure`，skipped 仅 `upstream_failure`。 |
| `stream/first_payload` | in_progress, completed, failed, paired not_applicable, skipped | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、stream_slot、path | previous_*、next_* | start/normal terminal 的 control/transfer/stream ID 按实际；paired N/A reason 仅 `no_missing_range|empty_file`，且 terminal 只允许 download stream_id null→真实值一次；failed `io_error|protocol_error|checksum_error|upstream_failure`，skipped `upstream_failure`。 |
| `stream/payload_io` | in_progress, completed, failed, not_applicable, skipped | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、stream_slot、path | previous_*、next_* | N/A reason 仅 `no_missing_range|empty_file`，三时间全 null；failed `io_error|protocol_error|checksum_error|upstream_failure`，skipped `upstream_failure`；无 DATA I/O 不得有 start。 |
| `file_attempt/data_channel_finalize` | in_progress, completed, failed, skipped | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、path | stream_slot、stream_id、previous_*、next_* | control_id/transfer_id 按实际关联；failed `io_error|checksum_error|protocol_error|manifest_error|upstream_failure`；skipped 仅 `upstream_failure`。空/all-no-range 从已证明 `payload_work_closed` 计时。 |
| `file_attempt/transfer_complete_wait` | in_progress, completed, failed, skipped | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、path | stream_slot、stream_id、previous_*、next_* | failed `control_error|protocol_error|io_error|upstream_failure`；skipped `upstream_failure`；从 file function return 到完整 226。 |
| `file_attempt/download_mtime_finalize` | in_progress, completed, failed, skipped | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、path | stream_slot、stream_id、previous_*、next_* | failed `io_error|manifest_error|upstream_failure`；skipped 仅 `upstream_failure`；仅 download 226 后出现。 |
| `file_attempt/manifest_finalize`（active） | in_progress, completed, failed | file_id、attempt_id>=1、attempt_kind=`transfer`、worker_id、path | stream_slot、stream_id、previous_*、next_* | failed `manifest_error|io_error|upstream_failure`；只包实际终态 `updateRecord`，不包 `updateRecordForTransfer`。 |
| `file_attempt/file_attempt_decision` | skipped | file_id、attempt_id=0、attempt_kind=`skip`、worker_id、path | control_id、stream_slot、stream_id、transfer_id、previous_*、next_*、start/end/elapsed | reason 仅 `manifest_completed`；表示入口不启动 DATA、进入 gate，不宣告 gate 成功。 |
| `file_attempt/manifest_finalize`（Changed exception） | in_progress, completed, failed | file_id、attempt_id=0、attempt_kind=`skip`、worker_id、path | stream_slot、stream_id、previous_*、next_*；control/transfer ID 仅实际关联时可非 null | 仅源码实际执行 Changed 终态 `updateRecord`；completed reason null，failed reason `manifest_error|changed|upstream_failure`。 |
| `worker/interfile_idle` | in_progress, completed, failed, paired not_applicable | worker_id、previous_file_id、previous_attempt_id | file_id、attempt_id、attempt_kind、control_id、stream_slot、stream_id、transfer_id、path | start 时 next_* 必须 null；continuous completed terminal 才补真实 next_file_id/next_attempt_id；paired N/A reason 仅 `no_next_file|cancelled`；failed 仅 `upstream_failure`。 |

`scope`、`stage`、`state` 只能取本表封闭集合；任何“至少包括”的未定义值、未知 reason、未知组合或额外 scope 均拒绝。`direction` 对 stream identity 固定为 download；upload 不产生 identity。

### 1.4 生命周期 ID 规则

- `run_id` 每次 tree run 唯一；`file_id` 是 manifest 稳定序号；`attempt_id=1` 是首次实际 transfer，retry 递增且失败消耗 ID 不复用。`attempt_id=0` 只允许上表两种形状：入口 decision 和实际 Changed manifest finalize。
- 每个 stage/span 预留新的 `span_id`，start 与 terminal 不重复分配；attempt 1 的任何行不能被 attempt 2 覆盖。每个 retry 取得新 span 和新 stream slot，但沿用实际复用的 control_id；只有真实 control teardown/reconnect 才新建 control 生命周期。
- `worker_id` 在 worker 创建时固定；control_id 表示真实 ControlClient 生命周期；stream_slot 在 attempt 内预留后即消耗，即使 connect 失败或 no-range 也不复用；stream_id 只能来自协议事实。download SessionInit 前为 null，identity 后同 slot 的后续阶段必须同时含 slot 和真实 ID；只有 first_payload paired terminal 允许一次性补实 stream_id。
- `transfer_id` 只能记录实际 effective ID，未知为 null；不得将请求值、空字符串或未采用的 retry 返回值臆造成新 ID。`path` 是相对树根路径，run/worker idle 为 null。

## 2. 完整 golden JSONL fixtures

以下每个 code fence 都是独立 UTF-8 JSONL 文件，固定键顺序和 LF。`valid` 表示 validator 必须接受并产生表中预期；反例通过对指定 token 做一次修改后必须拒绝，并只将 evidence 标为 partial，不改变 transfer/process/integrity/wire。

### Fixture U-NORMAL：upload 正常 active attempt

预期：16 行全部接受；8 个唯一 span 各有 start/terminal 配对；attempt=1、control_id=10、stream_slot=0；可进入性能资格（还需独立 hash、wire 和 summary 完整）。

```jsonl
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"worker","stage":"control_acquire","span_id":1,"state":"in_progress","start_ns":100,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"worker","stage":"control_acquire","span_id":1,"state":"completed","start_ns":100,"end_ns":140,"elapsed_ns":40,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"worker","stage":"control_prepare","span_id":2,"state":"in_progress","start_ns":150,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"worker","stage":"control_prepare","span_id":2,"state":"completed","start_ns":150,"end_ns":190,"elapsed_ns":40,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"stream","stage":"data_connect","span_id":3,"state":"in_progress","start_ns":200,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":0,"stream_id":0,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"stream","stage":"data_connect","span_id":3,"state":"completed","start_ns":200,"end_ns":250,"elapsed_ns":50,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":0,"stream_id":0,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"stream","stage":"first_payload","span_id":4,"state":"in_progress","start_ns":250,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":0,"stream_id":0,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"stream","stage":"first_payload","span_id":4,"state":"completed","start_ns":250,"end_ns":350,"elapsed_ns":100,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":0,"stream_id":0,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"stream","stage":"payload_io","span_id":5,"state":"in_progress","start_ns":300,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":0,"stream_id":0,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"stream","stage":"payload_io","span_id":5,"state":"completed","start_ns":300,"end_ns":900,"elapsed_ns":600,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":0,"stream_id":0,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"file_attempt","stage":"data_channel_finalize","span_id":6,"state":"in_progress","start_ns":900,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"file_attempt","stage":"data_channel_finalize","span_id":6,"state":"completed","start_ns":900,"end_ns":950,"elapsed_ns":50,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"file_attempt","stage":"transfer_complete_wait","span_id":7,"state":"in_progress","start_ns":950,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"file_attempt","stage":"transfer_complete_wait","span_id":7,"state":"completed","start_ns":950,"end_ns":1000,"elapsed_ns":50,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"file_attempt","stage":"manifest_finalize","span_id":8,"state":"in_progress","start_ns":1010,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000101","direction":"upload","scope":"file_attempt","stage":"manifest_finalize","span_id":8,"state":"completed","start_ns":1010,"end_ns":1050,"elapsed_ns":40,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":10,"stream_slot":null,"stream_id":null,"transfer_id":"tr-u1","path":"f.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
```

反例：删除 `next_attempt_id`（缺键）、将 span 7 的 `elapsed_ns` 改为 49（差值错误）、将 stream stage 的 `stream_id` 改为 `"0"`（字符串）、或把 manifest `updateRecordForTransfer` 误记成 span 8；均应拒绝/标 evidence partial。

### Fixture D-LATE-NORANGE：download late stream ID、identity 和 payload_io N/A

预期：6 行接受。data_connect 先有完整 start/terminal；随后 first_payload 保留 `stream_id=null`，identity 绑定真实 ID，paired N/A terminal 只允许 stream_id 从 null 变为 7；最后是普通 payload_io N/A，时间三元组全 null。该 attempt 是 `attempt_id=1`，不是 skip attempt 0。

```jsonl
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000202","direction":"download","scope":"stream","stage":"data_connect","span_id":40,"state":"in_progress","start_ns":900,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":null,"transfer_id":"tr-d2","path":"a.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000202","direction":"download","scope":"stream","stage":"data_connect","span_id":40,"state":"completed","start_ns":900,"end_ns":980,"elapsed_ns":80,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":null,"transfer_id":"tr-d2","path":"a.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000202","direction":"download","scope":"stream","stage":"first_payload","span_id":41,"state":"in_progress","start_ns":1000,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":null,"transfer_id":"tr-d2","path":"a.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"stream_identity","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000202","direction":"download","scope":"stream","stage":"stream_identity","state":"bound","bound_ns":1040,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":7,"transfer_id":"tr-d2","path":"a.bin","error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000202","direction":"download","scope":"stream","stage":"first_payload","span_id":41,"state":"not_applicable","start_ns":1000,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":7,"transfer_id":"tr-d2","path":"a.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":"no_missing_range","error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000202","direction":"download","scope":"stream","stage":"payload_io","span_id":42,"state":"not_applicable","start_ns":null,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":7,"transfer_id":"tr-d2","path":"a.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":"no_missing_range","error_code":"ok"}
```

反例：删除 identity 行、把 first_payload terminal 的 stream_id 改为 8、再写一次 identity、把 payload_io `start_ns` 改为 1000，或将 attempt_id 改为 0；均拒绝。

### Fixture EMPTY：空文件 paired N/A

预期：first_payload 只接受 paired N/A `empty_file`；payload_io 接受普通单行 N/A。连接已经完成且空文件事实已从握手确认，故仍为 attempt 1。

```jsonl
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000303","direction":"upload","scope":"stream","stage":"first_payload","span_id":51,"state":"in_progress","start_ns":1100,"end_ns":null,"elapsed_ns":null,"file_id":4,"attempt_id":1,"attempt_kind":"transfer","worker_id":1,"control_id":3,"stream_slot":0,"stream_id":0,"transfer_id":"tr-e3","path":"empty.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000303","direction":"upload","scope":"stream","stage":"first_payload","span_id":51,"state":"not_applicable","start_ns":1100,"end_ns":null,"elapsed_ns":null,"file_id":4,"attempt_id":1,"attempt_kind":"transfer","worker_id":1,"control_id":3,"stream_slot":0,"stream_id":0,"transfer_id":"tr-e3","path":"empty.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":"empty_file","error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000303","direction":"upload","scope":"stream","stage":"payload_io","span_id":52,"state":"not_applicable","start_ns":null,"end_ns":null,"elapsed_ns":null,"file_id":4,"attempt_id":1,"attempt_kind":"transfer","worker_id":1,"control_id":3,"stream_slot":0,"stream_id":0,"transfer_id":"tr-e3","path":"empty.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":"empty_file","error_code":"ok"}
```

反例：`empty_file` 使用 attempt 0、first_payload terminal 没有 start、或将 `elapsed_ns=0`；均拒绝。若 data connection 在空文件事实确认前失败，应改用 data_connect failed，不能使用本 fixture。

### Fixture IDLE：连续、尾部、取消和 handoff 错误

预期：连续行是 completed timed pair；tail/cancel 是两个受限 paired N/A；handoff 是真实 failed timed pair。worker idle 的普通 file/attempt/control/stream/transfer/path 永远为 null。

```jsonl
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000404","direction":"upload","scope":"worker","stage":"interfile_idle","span_id":300,"state":"in_progress","start_ns":5000,"end_ns":null,"elapsed_ns":null,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":2,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":7,"previous_attempt_id":1,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000404","direction":"upload","scope":"worker","stage":"interfile_idle","span_id":300,"state":"completed","start_ns":5000,"end_ns":5300,"elapsed_ns":300,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":2,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":7,"previous_attempt_id":1,"next_file_id":8,"next_attempt_id":1,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000404","direction":"upload","scope":"worker","stage":"interfile_idle","span_id":301,"state":"in_progress","start_ns":6000,"end_ns":null,"elapsed_ns":null,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":2,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":8,"previous_attempt_id":1,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000404","direction":"upload","scope":"worker","stage":"interfile_idle","span_id":301,"state":"not_applicable","start_ns":6000,"end_ns":null,"elapsed_ns":null,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":2,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":8,"previous_attempt_id":1,"next_file_id":null,"next_attempt_id":null,"reason":"no_next_file","error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000404","direction":"upload","scope":"worker","stage":"interfile_idle","span_id":302,"state":"in_progress","start_ns":7000,"end_ns":null,"elapsed_ns":null,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":2,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":9,"previous_attempt_id":1,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000404","direction":"upload","scope":"worker","stage":"interfile_idle","span_id":302,"state":"not_applicable","start_ns":7000,"end_ns":null,"elapsed_ns":null,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":2,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":9,"previous_attempt_id":1,"next_file_id":null,"next_attempt_id":null,"reason":"cancelled","error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000404","direction":"upload","scope":"worker","stage":"interfile_idle","span_id":303,"state":"in_progress","start_ns":8000,"end_ns":null,"elapsed_ns":null,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":2,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":10,"previous_attempt_id":1,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000404","direction":"upload","scope":"worker","stage":"interfile_idle","span_id":303,"state":"failed","start_ns":8000,"end_ns":8100,"elapsed_ns":100,"file_id":null,"attempt_id":null,"attempt_kind":null,"worker_id":2,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":null,"previous_file_id":10,"previous_attempt_id":1,"next_file_id":null,"next_attempt_id":null,"reason":"upstream_failure","error_code":"io_error"}
```

反例：tail/cancel terminal 使用 `completed`、补一个猜测的 next ID、把 handoff error 写成 `not_applicable`、或把 idle elapsed 加入 run wall；均拒绝或不计入性能样本。前一文件 `processFile` 失败时不存在新的 idle start。

### Fixture R-SAME-CONTROL：两次 attempt、同一 control

预期：8 行接受（4 个 span，各一对 start/terminal）；attempt 1/2、span 和 stream slot 均隔离；control_id=5 在两次 attempt 中保持不变；attempt 1 的 connect failure 不被 attempt 2 覆盖。两个 attempt 都不具备性能资格。

```jsonl
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000505","direction":"upload","scope":"worker","stage":"control_prepare","span_id":120,"state":"in_progress","start_ns":2100,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":null,"stream_id":null,"transfer_id":"tr-r5","path":"retry.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000505","direction":"upload","scope":"worker","stage":"control_prepare","span_id":120,"state":"completed","start_ns":2100,"end_ns":2150,"elapsed_ns":50,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":null,"stream_id":null,"transfer_id":"tr-r5","path":"retry.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000505","direction":"upload","scope":"stream","stage":"data_connect","span_id":121,"state":"in_progress","start_ns":2200,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":0,"stream_id":null,"transfer_id":"tr-r5","path":"retry.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000505","direction":"upload","scope":"stream","stage":"data_connect","span_id":121,"state":"failed","start_ns":2200,"end_ns":2300,"elapsed_ns":100,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":0,"stream_id":0,"transfer_id":"tr-r5","path":"retry.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":"io_error","error_code":"io_error"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000505","direction":"upload","scope":"worker","stage":"control_prepare","span_id":122,"state":"in_progress","start_ns":2400,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":2,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":null,"stream_id":null,"transfer_id":"tr-r5","path":"retry.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000505","direction":"upload","scope":"worker","stage":"control_prepare","span_id":122,"state":"completed","start_ns":2400,"end_ns":2450,"elapsed_ns":50,"file_id":3,"attempt_id":2,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":null,"stream_id":null,"transfer_id":"tr-r5","path":"retry.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000505","direction":"upload","scope":"stream","stage":"data_connect","span_id":123,"state":"in_progress","start_ns":2500,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":2,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":0,"stream_id":0,"transfer_id":"tr-r5","path":"retry.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000505","direction":"upload","scope":"stream","stage":"data_connect","span_id":123,"state":"completed","start_ns":2500,"end_ns":2550,"elapsed_ns":50,"file_id":3,"attempt_id":2,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":0,"stream_id":0,"transfer_id":"tr-r5","path":"retry.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
```

反例：attempt 2 使用 control_id=6 但没有 reconnect、attempt 2 重用 span 120、把 retry prepare 标成 control_acquire、或将 attempt 1 wire 合并到 attempt 2；均拒绝/取消性能资格。

### Fixture A0-EXCEPTIONS：attempt-0 两个允许形状

预期：decision 单行接受；Changed finalize 仅在真实 `updateRecord(Changed)` 发生时接受。两行均为同一 file 的 attempt 0、worker=4；decision 的 control/stream/transfer 全 null，Changed finalize 的 control/transfer 也只有源码实际关联时才可非 null。本 fixture 采用 null。

```jsonl
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000606","direction":"download","scope":"file_attempt","stage":"file_attempt_decision","span_id":600,"state":"skipped","start_ns":null,"end_ns":null,"elapsed_ns":null,"file_id":12,"attempt_id":0,"attempt_kind":"skip","worker_id":4,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":"changed.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":"manifest_completed","error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000606","direction":"download","scope":"file_attempt","stage":"manifest_finalize","span_id":601,"state":"in_progress","start_ns":9000,"end_ns":null,"elapsed_ns":null,"file_id":12,"attempt_id":0,"attempt_kind":"skip","worker_id":4,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":"changed.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","schema_version":1,"run_id":"00000000-0000-4000-8000-000000000606","direction":"download","scope":"file_attempt","stage":"manifest_finalize","span_id":601,"state":"completed","start_ns":9000,"end_ns":9050,"elapsed_ns":50,"file_id":12,"attempt_id":0,"attempt_kind":"skip","worker_id":4,"control_id":null,"stream_slot":null,"stream_id":null,"transfer_id":null,"path":"changed.bin","previous_file_id":null,"previous_attempt_id":null,"next_file_id":null,"next_attempt_id":null,"reason":null,"error_code":"ok"}
```

反例：decision 带 control_id、Changed finalize 在未调用 `updateRecord` 时出现、attempt 0 产生 data_connect、或把 Changed finalize 改成 attempt 1；均拒绝。

## 3. Logger 故障注入契约

实现任务必须提供仅测试可用的 deterministic sink seam；生产默认路径不改变。建议测试环境变量/fixture 参数统一为 `CPNETFLUX_TELEMETRY_FAULT=<case>:<ordinal>`，在单进程单 run 中只触发一次，并在测试报告记录 case、ordinal、errno、原始 event-log 前缀 SHA-256。四个注入点及断言如下。基线 `B` 是同一 binary/config/input 的无注入 run；注入 run 的 transfer/process/integrity/wire/frame/hash 结果必须逐项等于 B，只有 evidence 轴及 evidence write-failure 计数可变化。

| case | 精确注入点与方式 | 应保留的文件/退出码 | 五轴断言 |
|---|---|---|---|
| `start_write` | recorder 生成第一条实际 `in_progress` 行后，在一次 append/write 返回前强制 `EIO`；不跳过被测传输，仅丢该 start。 | 已存在的旧 event-log 前缀 byte-for-byte 保留；后续正常行可继续追加；summary 可读；退出码等于 B（成功仍 0）。 | transfer/process/integrity/wire 与 B 相同；evidence=`partial`，`logger_write_failures=1`，缺 start 的阶段进入 evidence gap，不得造成功 duration。 |
| `terminal_write` | 选定一个已成功 start 的 span，在 terminal append/write 返回前强制 `EIO`；start 行必须已落盘。 | start 行及此前 prefix 保留；terminal 缺失；summary/stderr 至少一处可读；退出码等于 B。 | transfer/process/integrity/wire/frame/hash 与 B 相同；evidence=`partial`，validator 派生 `incomplete/evidence_gap`，不补 end/elapsed。 |
| `append_poison` | 先正常 `O_APPEND` 打开并写入 `N` 行，再令底层 write 返回 `ENOSPC`（或等价注入 errno）；recorder 进入 poison 状态并停止后续 append，不能重试写出伪行。 | event-log 保留前 `N` 行的精确 bytes；summary 可读并带失败/抑制计数；退出码等于 B。 | 五轴中的 transfer/process/integrity/wire/frame/hash 与 B 相同；仅 evidence=`partial`、`logger_write_failures>=1` 和 `suppressed_event_count` 增加。 |
| `summary_write` | summary 文件完成计算后、write/rename 返回前强制 `EIO`；event log 不注入；stderr 正常。另运行 `stderr_write` 子变体，在 stderr write 返回前强制 `EIO`，summary 正常。 | `summary_write` 保留 event-log 全部 bytes，summary 可缺失/不完整但 stderr fallback 可读；`stderr_write` 保留完整 summary，stderr 可缺失；两者退出码均等于 B。 | transfer/process/integrity/wire/frame/hash 与 B 相同；可读的另一 sink 报 evidence=`partial`、失败计数；若两 sink 都同时失败则只允许报告 evidence gap，仍不得修改其它轴或退出码。 |

禁止把 logger 注入转换为传输失败、hash mismatch、wire=0 或性能资格通过。注入 run 不进入 on/off overhead 样本；它们只验证隔离。

## 4. telemetry on/off 等价性与性能门

### 4.1 固定样本、配置和随机交错

使用同一固定提交、同一编译器/二进制 SHA-256、同一 loopback server (`127.0.0.1`)、同一端口分配规则和同一配置：`file_parallelism=2`、每文件 `connections=1`、`control_reuse=worker`、`scheduler=off`、`compression=off`、`checksum=crc32c`、POSIX file backend、data TLS off。唯一变量是 telemetry recorder `on` 或 `off`；不改变 wire/protocol/resume/default 行为。

三种数据集均含 8 个文件 `f00.bin`…`f07.bin`，每文件 4,194,304 bytes，总 fresh logical bytes 为 33,554,432。文件内容由固定 UTF-8 seed `CPNetFlux-telemetry-plan-06`、小端 file index 和小端 64 KiB block index 做 SHA-256 counter stream，取 digest bytes 循环填充；生成器必须把 seed/index/block 记录在 case manifest，避免随机源漂移。

| case | 初始状态与运行方式 | payload/eligibility |
|---|---|---|
| `fresh` | 新 source/destination 和新 tree manifest；resume off；所有 8 文件实际传输。 | logical denominator=33,554,432；无 retry、证据完整时可进入 `performance_eligible`。 |
| `no_range` | 与 fresh 相同的 manifest，但预先写入完整目标/verified ranges；resume on；每 stream 在握手后确认 no missing range。 | DATA logical=0，握手/FIN/226 wire 仍可能非零；不进入 payload goodput，wall overhead 仍可比较。 |
| `resume_partial` | 每文件固定前 1,048,576 bytes 已 verified，缺失区间为 `[1,048,576,4,194,304)`；resume on。 | 新 logical denominator=25,165,824；只以本轮 missing bytes 计算 goodput。 |

upload 和 download 各测上述三 case。每个 case/direction 进行 10 个 paired blocks（20 runs），共 60 blocks/120 runs；每 block 恰有一个 off 和一个 on。pair 内 on/off 顺序以及 block 顺序由固定 seed `0x20260924` 的 PCG32 Fisher–Yates 产生，并把 `pair_id, order_seed, order_bit` 写入 runner evidence。每次 run 清理并重新生成可再生 payload/manifest；同一 pair 不共享可变目标状态。两模式使用相同 logical input、端点、parallelism 和 server 状态。

### 4.2 等价性、失败排除和计时定义

每个 paired block 必须满足下列等价性，否则该 block 标为 invalid 并从性能统计排除，同时报告排除原因：

1. exit code 相同且为预期值；成功 case 为 0。telemetry fault、logger 注入、server/client error、被中断 run 不进入 overhead 样本。
2. 每个文件 SHA-256 和 canonical tree hash 完全相同；源/目标 byte count、file count、DATA logical frame 序列（frame type、header fields、payload bytes，按 stream slot 对齐）完全相同。允许 TCP segmentation、时间戳和 telemetry span/ID 不同。
3. manifest canonical projection 相同：按 relative path 排序比较 `status,size,mtime,error,transfer_id validity,completed_ranges,checksum policy`；仅排除明确标记的 run timestamps（`createdAtUnixNanos/updatedAtUnixNanos`）和 telemetry log path。transfer ID 的值若 loopback 实际可稳定得到则必须相同；若不稳定，pair 直接 invalid，不能静默归一化。
4. resume decision、skip/transfer 分类、missing ranges 和 `wire_accounting` 的 logical DATA/control/FIN/COMPLETE/226 计数相同；计数器缺失、evidence partial 或 wire unknown 都使 block invalid。no_range 的 DATA logical=0 不能替代总 wire 计数。

`wall_ns` 是 runner 包住 tree operation 的 monotonic client wall；不把 case setup、payload generation、独立 hash runner 或 cleanup 算入。`cpu_seconds` 是 client process CPU；`jsonl_bytes` 是 on 模式 event log 的实际 byte length（含 LF）。每个 valid block 计算：

```text
wall_delta_pct  = 100 * (wall_on - wall_off) / wall_off
cpu_per_gib     = cpu_seconds / (logical_bytes / 2^30)
cpu_delta_pct   = 100 * (cpu_per_gib_on - cpu_per_gib_off) / cpu_per_gib_off
log_bytes_per_gib = jsonl_bytes_on / (logical_bytes / 2^30)
```

`wall_off`、`cpu_per_gib_off` 必须大于 0；否则 block invalid。`no_range` 的 logical bytes 为 0，因此 `cpu_per_gib` 和 `log_bytes_per_gib` 记 N/A，不用 0，只有 wall gate 适用。每个 cell 至少 8/10 个 valid pairs，否则该 cell 为 BLOCKED，不能用剩余样本宣称通过。percentile 使用升序数据的线性插值，位置 `(n-1)*q`；median 为 q=.50，p95 为 q=.95。

### 4.3 固定 pass/fail 门

分别对 upload/download 的 fresh、no_range、resume_partial 报告 valid pair 数、invalid 原因、wall median/p95、CPU/GiB median/p95、JSONL bytes/GiB median/p95。门限为：

| 指标 | fresh / resume_partial | no_range |
|---|---:|---:|
| wall overhead（on 相对 off）median | ≤ 5% | ≤ 5% |
| wall overhead p95 | ≤ 10% | ≤ 10% |
| CPU/GiB overhead median/p95 | median ≤ 5%，p95 ≤ 10% | N/A（logical=0） |
| JSONL bytes/GiB（on 绝对量）median/p95 | median ≤ 4 MiB/GiB，p95 ≤ 8 MiB/GiB | N/A（logical=0） |

所有 cell 都达到 valid pair 数、等价性和门限才是 telemetry overhead `PASS`。任何 hash/manifest/DATA frame/wire mismatch、退出码差异、evidence partial、logger 注入或 valid pair 不足均为该 cell `BLOCKED/FAIL`，不通过平均值掩盖。no_range 的 wall 结果只说明控制/握手路径 overhead，不能写成 payload throughput 或网络性能结论。

## 5. 后续实现文件白名单与测试入口

仅在本规格经 04 复审 PASS 后，另发 implementation task 并创建独立 worktree。预期修改边界：

- `src/core/io/tree_transfer_client.cpp`、`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_client.cpp` 的既定插点；
- 直接对应 telemetry recorder/JSON serializer 的窄 `include/cpnetflux/core/...`、`src/core/metrics/...`；
- 专属 parser/validator golden fixture 与 logger fault-injection 单元测试；
- 明确的 CMake 测试注册。

不得改 wire protocol、manifest/resume/checksum、scheduler/compression 默认值、IO backend、runner 语义或旧事件字段。实现后的测试顺序应为：raw-token parser/golden → paired lifecycle/ID validator → logger 四类注入 → mixed old/new consumer → loopback tree upload/download fresh/no-range/resume → telemetry on/off equivalence → overhead gate。所有这些在本任务均 `NOT_RUN`。

## 执行回执

- 实际输入/输出 commit：HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；固定源码输入为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；只新增本结果文件，未提交。
- 实际改动：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-06-result.md`；未修改 PLAN-05、QA-05、ARBITRATION-03、decision、源码、测试、runner、BOARD/ROSTER 或 index。
- 实际只读命令：`git rev-parse HEAD`、`git status --short --branch`、`git diff --cached --name-only`、输入文件 SHA-256/定向 `Get-Content` 均成功；四份输入读取后 SHA-256 为 PLAN-05 `09356E014CAB4CB9DF097C3728AE055FAA929EEBADF08F104B09CF4D7AAFED0A`、QA-05 `08B4EBFE0EBE8B87BB5D43A66EE0FB51B0F5FF8DCE4DE97CF483FABB6E429F81`、ARBITRATION-03 `75A3FE5DFBF82D2D3237FA0205F23B99470235D361A82FC84CCD4C2313BCC9FC`、decision `C55991CD0781B24B24E6B05C295329B41B5DFF23F6149868366FF8A8596F928C`；复核哈希一致。固定源码相对 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 的路径差异检查为 clean。报告内 JSONL 文档 fixture lint 首次退出码 1（检查脚本误把普通 `payload_io` N/A 当作 paired N/A），修正规则后重跑退出码 0：6 个 block、44 行，26/18 键集、键顺序、JSON 语法和 paired start/terminal 静态一致性通过；这不是运行态 parser 测试。
- 阻塞/未运行项：没有发现新契约冲突；实现闸门仍等待 04 对本结果独立复审。CMake、CTest、parser/validator、logger 故障注入、真实 tree upload/download、resume/hash/frame、on/off、overhead、SSH、云端和清理全部 `NOT_RUN`。
- 下一步：04 逐项复审完整矩阵、全部 golden bytes、四类故障注入和 60 paired blocks/门限；QA PASS 后 00 才能派发实现 worktree。

## 写后文档门禁

- 结果文件严格 UTF-8、无 BOM、纯 LF、以 LF 结尾、逐行尾随空白为 0。
- 写后扫描：UTF-8 严格解码、无 BOM、纯 LF、尾随空白 0、文件以 LF 结尾，退出码 0（文件 49184 bytes）。
- `git rev-parse HEAD` 退出码 0，仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`git diff --cached --name-only` 退出码 0 且无输出。
- 固定三源码 `git diff --quiet 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp` 退出码 0。
- `git diff --check` 退出码 0；提示仅为共享工作区既有文档的 LF→CRLF 转换。没有运行任何动态测试、构建、传输、性能实验或远端动作。
