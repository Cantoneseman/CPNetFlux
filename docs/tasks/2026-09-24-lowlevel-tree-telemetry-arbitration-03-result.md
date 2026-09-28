# LOWLEVEL-TREE-TELEMETRY-ARBITRATION-03 结果

状态：**条件收口；当前实现闸门仍 BLOCKED，待 03 更新 PLAN-04、04 独立复审 PASS 后再实现**
路线/任务：任务单为 `R2026-09-24.9 / v1`；本轮读取的 BOARD、decision、PLAN-04、QA-04 仍标 `R2026-09-24.8`。这是资料版本不一致，不由本结果修改 BOARD/decision。
固定源码：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`

本结果只裁定 no-range、retry control 生命周期和 interfile idle 三项。固定源码与当前资料的实现路径没有变化；不改源码、测试、runner、CMake、decision、BOARD、ROSTER、云端或 index。以下是静态调用顺序和架构建议，不是实现或动态验收。

## 一、结论和取舍

三项可以用两个局部的 tree_stage_v1 生命周期例外和一个 retry 映射修正同时满足：

1. 保留 `first_payload` 的固定起点“data connection 完成”，保留 JSONL append-only；当 no-range/empty 只能在该起点之后确认时，允许该 span 先写 `in_progress`，再追加一个配对的 `state=not_applicable` terminal。该 N/A terminal 复制 start 的关联键和 `start_ns`，`end_ns/elapsed_ns` 为 null，带确定 reason；它不是把时间改写成零。`payload_io` 没有 DATA I/O 起点，仍使用一条普通 N/A 行。这样只放宽 first_payload 的 N/A 单行规则，避免回写或丢失连接到握手的观察起点。
2. retry 是新 `attempt_id`、新 span 和新 stream slot 的逻辑尝试，但沿用固定源码实际复用的 `control_id`；只有观测到控制连接对象真实重建时才分配新 control ID。不得把 retry 写成每次新建 control。
3. interfile idle 在上一文件成功终态后先追加 start。连续文件在下一文件实际领取时追加 completed terminal 并只补 `next_*`；worker 尾部或取消在已写 start 后追加配对 N/A terminal，分别使用 `no_next_file`/`cancelled`；交接本身出错则追加真实 failed terminal。不得删除、回写或伪造零时长。

上述两个配对 N/A 例外是严格可验证的窄规则：只适用于 `stage=first_payload` 的 `reason in {no_missing_range,empty_file}` 和 `stage=interfile_idle` 的 `reason in {no_next_file,cancelled}`；其他 N/A 仍为单行、全时间 null。若 00 不接受这两个局部例外，四项要求（固定起点、append-only、晚知 no-range、N/A 单行）不可同时满足，应保持 BLOCKED，不得改用回写或延迟起点。

## 二、固定源码事实

### no-range 的 upload/download 顺序

上传 `file_transfer_client.cpp:390-416` 的 `sendStream` 先完成 data socket 连接，再发送 SessionInit 并读取 ResumeResponse；`423-487` 随后按 missing ranges 发送 DATA，最后发送 FIN 并等待 Complete。无 missing range 仍会走 FIN/Complete 和文件函数返回。下载 `file_download_client.cpp:378-425` 先连接、读取 SessionInit（`388-395` 才得到线上 `stream_id`），再准备缺失范围、发送 ResumeResponse；`438-445` 仍接收 FIN，`625` 发送 Complete，`690-758` 还会检查缺失范围、验证/flush，并执行 rename/commit。因而 no-range 的事实只能在连接完成后、SessionInit/ResumeResponse 之后确定，且它始终是已开始的 `attempt_id>=1, attempt_kind=transfer`。

tree 层 active 调用顺序是 upload 的 `controlForFile`、EPSV/REST/STOR、`runFileTransferClient`（`tree_transfer_client.cpp:1341-1409`），返回后再 `waitTransferComplete`（`1456-1457`）和终态 `updateRecord`（`1468`）。download 是 `controlForFile`、SIZE/MDTM/EPSV/REST/RETR、`runFileDownloadClient`（`1493-1592`），返回后 `waitTransferComplete`（`1604-1605`）、`setRegularFileMtime`（`1616`）和终态 `updateRecord`（`1624`）。全 no-range 的 `data_channel_finalize` 起点是所有 stream 明确进入 `payload_work_closed` 的时刻，终点仍是文件函数返回；不把 226、mtime 或 manifest 保存塞入该 span。

空文件与 no-range 共享“没有 DATA payload”的结果轴，但 reason 不可混用：在每个 stream 的连接完成后，以协议握手/会话事实确认空文件时，first_payload 采用配对 N/A `reason=empty_file`，payload_io 采用普通 N/A `reason=empty_file`；空文件的 control、data connect、FIN/Complete 和 finalize 仍按实际记录。若 connection 在完成前失败，不能伪造 empty/no-range N/A 来掩盖失败。

### retry 的 control 生命周期

固定 upload retry 在 `tree_transfer_client.cpp:1414-1447`：第一次 `runFileTransferClient` 失败后，代码在同一 `control.value()` 上等待第一次控制回复（`1416`），再调用同一对象的 EPSV、REST、STOR，并再次调用 `runFileTransferClient`（`1426-1440`）。因此 control connection 生命周期与 data attempt 生命周期不同。`controlForFile` 的 worker reuse/新建边界在 `1151-1178`；worker scheduler 取得文件的循环在 `2340-2359`，global scheduler 的 dispatch/complete 在 `2231-2279`。

retry 不产生新的控制连接证据，除非实现确实观察到原连接对象 teardown/reconnect；固定路径应沿用原 `control_id`。新的 retry 仍要分配新的 `attempt_id`、span ID 和 attempt 内 stream slots；连接失败消耗 slot/ID，不复用。`effectiveTransferId` 仍是该文件的既有 transfer ID；不能因 `startTransfer` 的 retry 返回值未被代码采用就臆造新的 `transfer_id`。每个 attempt 的 DATA wire、控制 wait 和失败/成功终态分别记账，最终 process 状态保留 retry 原始结果，含 retry 的文件默认不具备性能资格。

### interfile idle 的源码边界

普通 scheduler 在 `processFile` 返回后回到 worker 循环，再检查 `state.stop`/`nextIndex` 并领取下一文件（`2341-2357`）。global scheduler 在前一 `processFile` 返回后还会执行 `completeGlobalDispatch`，随后通过 `nextGlobalDispatch`，可能等待 capacity（`2233-2279`）。推荐把 idle 起点放在上一文件成功的最后一个 file/manifest terminal 之后、下一轮 scheduler handoff 之前；终点放在下一文件实际被领取、即将进入 `processFile` 之前。global 的 complete/dispatch/capacity 等待属于该 worker handoff interval。

若上一文件本身失败，不开启新的 idle span；其失败 stage 和 run stop 已表达原因。若 idle 已开始后另一个 worker 触发 stop、取消或交接失败，按下节终态规则结束该 idle 候选，不回写 start。worker 尾部“没有下一个文件”不是可报告的等待区间，但为了不留下孤立 start，使用配对的 N/A terminal `reason=no_next_file`；该行不进入 idle 时长分析。

## 三、唯一推荐的事件和状态契约

### 3.1 first_payload / payload_io 的 no-range 和首块前失败

在每个实际启动的 stream 上：

1. data connection 成功完成时立即追加 `first_payload` start，`state=in_progress`，`start_ns=t_connect_done`，其余时间 null。upload 可填写源码已知的真实协议 stream ID；download 此时 `stream_id=null`，因为 SessionInit 尚未解码。此 start 在进入后续握手前写入并 flush。
2. 若 SessionInit/ResumeResponse 后发现该 stream no-range，追加同 span 的 `state=not_applicable` terminal：`reason=no_missing_range`，复制 `start_ns`，`end_ns=null, elapsed_ns=null`，不补零。download terminal 可带 SessionInit 绑定后的实际 `stream_id`；validator 对此单一场景以 `(run_id,span_id,file_id,attempt_id,worker_id,stream_slot,stage)` 配对，允许 `stream_id: null -> bound` 一次；不回写 start。若是空文件，完全相同但 reason=`empty_file`。
3. no-range 的 `payload_io` 不存在 start，因为没有首个 DATA socket I/O；在 ResumeResponse/no-work 确认后追加一条普通 `state=not_applicable`，三项时间全 null，reason 为 `no_missing_range` 或 `empty_file`。因此 first_payload 的连接→决策区间不会被当成 payload 时长，payload_io 仍满足 N/A 单行。
4. 若连接已完成但在 SessionInit、ResumeResponse 或首个有效 DATA 前失败，first_payload 的已有 start 追加 `state=failed` terminal，`end_ns` 是真实失败时刻，`elapsed_ns` 为精确差值；没有 DATA I/O 的 payload_io 写 `state=skipped, reason=upstream_failure` 单行，不伪造 N/A/no-range。data_channel_finalize 也为 skipped/upstream_failure；attempt transfer_status=`failed`，integrity 通常 `unknown`，evidence 取日志完整度。
5. 若 data connection 本身失败，写实际 `data_connect=failed`；first_payload 没有 start，不生成其 measured span；已启动但未进入的 payload 阶段可写 skipped/upstream_failure。download 已启动 slot 没有 SessionInit 时追加 `stream_identity(state=failed, stream_id=null)`，不推测 ID。

这是唯一推荐的取舍：保留固定 first_payload 起点和 append-only，放宽 first_payload N/A 只能单行的旧约束；不接受“ResumeResponse 后才写 first_payload start”，因为那会丢掉固定定义要求观察的连接完成到握手决策等待；不接受“直接写单行 N/A 并丢弃先前 start”，因为那需要删除/回写或留下孤立 start；不接受把 no-range 回改为 attempt 0，因为真实连接和握手已经属于 attempt>=1。

最小示例（其余键必须按 v1 全量补齐；这里只展示决定性字段）：

```json
{"event":"tree_stage_v1","scope":"stream","stage":"first_payload","span_id":41,"state":"in_progress","start_ns":1000,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"stream_slot":0,"stream_id":null,"reason":null,"error_code":"ok"}
{"event":"stream_identity","schema_version":1,"direction":"download","scope":"stream","stage":"stream_identity","state":"bound","bound_ns":1040,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"control_id":2,"stream_slot":0,"stream_id":7,"transfer_id":"tr-3","path":"a.bin","error_code":"ok"}
{"event":"tree_stage_v1","scope":"stream","stage":"first_payload","span_id":41,"state":"not_applicable","start_ns":1000,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"stream_slot":0,"stream_id":7,"reason":"no_missing_range","error_code":"ok"}
{"event":"tree_stage_v1","scope":"stream","stage":"payload_io","span_id":42,"state":"not_applicable","start_ns":null,"end_ns":null,"elapsed_ns":null,"file_id":3,"attempt_id":1,"attempt_kind":"transfer","worker_id":0,"stream_slot":0,"stream_id":7,"reason":"no_missing_range","error_code":"ok"}
```

Validator 正例是上述 start + paired N/A；反例包括：N/A terminal 没有对应 start、first_payload N/A terminal 改写 start、payload_io N/A 带时间、no-range 使用 attempt 0、download first_payload 的 `stream_id` 从 null 直接改变两次、无 SessionInit 却写 bound identity。普通 completed first_payload 终态仍要求 start/end/elapsed 精确配对；`first_payload <= payload_io` 仍不是全局约束。

### 3.2 retry 的 attempt/span/stream/control 向量

第一次 active attempt 使用 `attempt_id=1`、`control_id=5`、span IDs 100–110、stream slots 0…N-1。若第一次 data function 失败且源码进入 retry：

- attempt 1 的 transfer/data/finalize/wait 行保持原状；若 `waitTransferComplete` 在 `1416` 实际调用，其 span 按真实结果为 completed/failed。不得覆盖或合并到 attempt 2。
- retry 分配 `attempt_id=2`，新的 span IDs 和新的 stream slots（slot 从该 attempt 的 0 起，但键含 attempt_id）；`control_id=5` 继续沿用，除非实现证明 control 对象真实重建，此时才新分配 control ID 并有新的 acquire span。retry 的 EPSV/REST/STOR 是使用既有 control 的 `control_prepare`，不是新的 control acquire。
- retry 的 file options 传入既有 `effectiveTransferId`；telemetry 只能填写实际可知的 transfer ID。源码没有保存 `retryTransfer` 返回值，不能据此臆造第二个 transfer ID；若将来在协议观测点确实得到不同的线上 ID，才按实际值记录，并不改变同一 control 生命周期。每个 attempt 独立记录 DATA logical/wire bytes；重试的 wire 不并入第一次 payload denominator，也不将同一文件标为 performance eligible。
- retry setup 在新 data connection 之前失败时，attempt 2 仍以 transfer attempt 记录实际 control_prepare failed、未进入的 data stages skipped/upstream_failure；若新 data function 启动则记录真实 streams。最终 `transfer_status/process_status` 同时保留每 attempt 和聚合结果：attempt 1 failed、attempt 2 completed 时聚合 process 可 completed，但 `performance_eligible=false`。

示例（决定性键）：

```json
{"event":"tree_stage_v1","scope":"file_attempt","stage":"control_prepare","span_id":120,"state":"in_progress","file_id":3,"attempt_id":2,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":null,"stream_id":null,"start_ns":2100,"end_ns":null,"elapsed_ns":null,"error_code":"ok"}
{"event":"tree_stage_v1","scope":"stream","stage":"data_connect","span_id":121,"state":"in_progress","file_id":3,"attempt_id":2,"attempt_kind":"transfer","worker_id":0,"control_id":5,"stream_slot":0,"stream_id":null,"start_ns":2200,"end_ns":null,"elapsed_ns":null,"error_code":"ok"}
```

反例：attempt 2 强制新 control ID、把同一 control 的 retry setup 写成 control_acquire、复用 attempt 1 span/stream、把 retry wire 加入一次 payload、把未执行的 retry data stage 写成零时长 completed。固定源码不支持这些形状。

### 3.3 interfile idle 的连续、尾部、取消和错误向量

`interfile_idle` 为 `scope=worker`，`worker_id` 必填；file/attempt/control/stream/transfer/path 全 null。start 在上一文件最后一个 terminal 后、scheduler handoff 前追加，`previous_file_id/previous_attempt_id` 必填，`next_*` 全 null。其 terminal 必须使用同一 span 和 previous 键，只有 `next_*` 可在此时从 null 填实。

- **连续文件：** 下一文件实际领取/dispatch 后追加 `state=completed` terminal，`next_file_id` 和 `next_attempt_id` 填实际值（下一文件是 Completed gate 时 attempt 0，否则首次 active 为 1），`end_ns` 为领取前时刻，`elapsed_ns=end-start`。随后才进入下一 `processFile`。普通 scheduler 的领取点在 `nextIndex++`；global scheduler 的 complete/dispatch/capacity 等待包含在该 span。
- **worker 尾部：** start 已写但检查到 `state.nextIndex >= files.size()` 或 global `hasPlan=false` 时，追加 `state=not_applicable` terminal，`reason=no_next_file`，`next_*` 保持 null，复制 start_ns，end/elapsed null。它是“候选 idle 无后继”的证据，不是零时长 idle，也不进入 idle 时间统计。
- **取消/停止：** idle start 后 `state.stop` 或外部取消阻止下一文件领取，追加 `state=not_applicable`，`reason=cancelled`，next null，复制 start_ns，end/elapsed null；取消结果由 run/process summary 保留。若 `completeGlobalDispatch`/`nextGlobalDispatch` 在 handoff 中实际返回错误，追加 `state=failed`，end 为真实错误时刻、elapsed 精确、error_code 为实际错误，reason=`upstream_failure`；不能将真实错误伪装成 N/A。
- **前一文件错误：** `processFile` 返回失败时不启动新的 idle span；前一文件的失败 stage/manifest 状态和 run stop 是充分证据。若另一个 worker 的错误使本 worker 已开始 idle 后停止，则按“取消/停止”终止候选。
- **进程崩溃：** 已写 idle start 而无 terminal 时由 validator 派生 `incomplete/evidence_gap`，不补 end/elapsed、不回写原文件。logger 写失败只改 evidence 计数，不改文件 transfer/process/integrity/wire。

示例：

```json
{"event":"tree_stage_v1","scope":"worker","stage":"interfile_idle","span_id":300,"state":"in_progress","worker_id":2,"previous_file_id":7,"previous_attempt_id":1,"next_file_id":null,"next_attempt_id":null,"start_ns":5000,"end_ns":null,"elapsed_ns":null,"reason":null,"error_code":"ok"}
{"event":"tree_stage_v1","scope":"worker","stage":"interfile_idle","span_id":300,"state":"completed","worker_id":2,"previous_file_id":7,"previous_attempt_id":1,"next_file_id":8,"next_attempt_id":1,"start_ns":5000,"end_ns":5300,"elapsed_ns":300,"reason":null,"error_code":"ok"}
```

尾部/取消终态仍配对同一 span，但 `state=not_applicable`、next null、`reason=no_next_file|cancelled`、`start_ns` 复制 start、end/elapsed null。此配对 N/A 与 first_payload 的配对 N/A 一样是唯一例外；普通 `not_applicable` 仍为单行全 null。放弃“只在已知后继时才写 start”是因为它无法覆盖等待期间的取消/崩溃；放弃“尾部不写任何 terminal”是因为会留下孤立 start；放弃回写 next 是因为违反 append-only。

## 四、PLAN-04 必须逐条修改

03 更新 PLAN-04 时应保留严格键集、字段类型、attempt-0 两种形状、四/五状态轴及 logger 规则，只做下列窄改：

1. 删除 §10.1 的 no-range BLOCKED 文案，新增“first_payload paired N/A”规则：connection-complete 写 start；ResumeResponse 后 no-range/empty 写配对 N/A terminal，保留 start_ns、end/elapsed null；payload_io 仍 N/A 单行；首块前失败按 failed/skipped/upstream_failure；列出 upload/download 和 stream_id late binding 例外。
2. 在 `tree_stage_v1` 状态机中新增两个受限配对允许项：`first_payload` 的 `not_applicable`（reason 仅 no_missing_range/empty_file）和 `interfile_idle` 的 `not_applicable`（reason 仅 no_next_file/cancelled）。两者的 terminal 必须对应唯一 in_progress start、复制 start_ns、不得产生 elapsed；其他 N/A 仍单行三时间 null。validator 反例须拒绝无 start 的配对 terminal、重复 terminal、reason 越界和以 0 代替 null。
3. 修订 first_payload 配对键：普通 stage 关联键必须相同；仅 download `first_payload` 允许 start `stream_id=null`、SessionInit 后 terminal 为真实 stream ID，一次性按 stream_slot 配对；data_connect 原 start/terminal 不回写，identity 仍是独立事件。
4. 把 §4.2 upload retry 改为“new attempt/span/stream，same control_id unless actual reconnect”；retry 的 control_prepare、wait 失败/成功、transfer_id、每 attempt wire denominator、performance false 逐项列出。删除“新 control”断言。
5. 把 §2.3/§4.2 interfile idle 改为三类 fixture：continuous completed with next IDs；tail paired N/A/no_next_file；cancel paired N/A/cancelled；handoff error paired failed/upstream_failure；processFile 前一文件失败不造 idle。明确 global capacity wait 包含在 idle。
6. 更新 §7 正反向 parser/validator：增加 paired N/A、late stream ID、retry same control/different attempt、idle tail/cancel/error；保留 `UINT64` raw token、重复 terminal、孤立 start、unknown/mismatch 隔离规则。增加完整的 `tree_stage_v1` 和 `stream_identity` golden JSON bytes，当前不得把规格列举说成测试已运行。
7. 更新 §4/§5 的状态和 eligibility 表：no-range/empty 的 transfer work 可 skipped，process status 保留实际文件函数/226/mtime/manifest 结果；全 no-range/empty/任何 retry/首块前失败/idle 均不进 payload goodput；wire accounting 区分 DATA logical 0 与握手/FIN/226 实际 bytes。
8. 在文档头把任务/资料版本差异交 00；由 00 将 decision/BOARD/PLAN/QA 统一到新路线版本后，再交 04 独立复审。PLAN-04 未更新、QA-04 未明确 PASS 前不解除 implementation gate。

## 五、被放弃方案及理由

| 方案 | 放弃原因 |
| --- | --- |
| ResumeResponse 后才写 first_payload start | 丢失固定“data connection 完成起点”以及握手等待观察；不能回答原 profiling 问题。 |
| no-range 只写一条 N/A、丢弃先前 start | append-only 无法抹除已经写出的 start；会留下孤立 span 或要求回写。 |
| no-range 回改 attempt 0 | 实际 data connection、SessionInit/ResumeResponse、FIN/Complete 已发生，破坏 attempt 生命周期和证据。 |
| 每次 retry 新建 control | 与 `control.value()` 同一对象上的 EPSV/REST/STOR 固定源码冲突，会重复计 control 建连。 |
| retry 合并到原 attempt | 丢失失败/重试边界、重复 wire 和每次 stream 的证据；不能审计最终结果。 |
| 只在后继文件已知后写 idle start | 看不到 prior terminal 到领取的 idle，也无法记录取消/崩溃；延迟写不是回写但会改变观察范围。 |
| worker 尾部不写 terminal 或回填 next | 前者留下孤立 start，后者违反 append-only；paired N/A 是最小可验证闭合。 |
| 用 `elapsed=0` 表示 no-range/尾部 N/A | 把未发生或不适用阶段伪装成真实零时长，污染 profiling。 |

## 六、验收状态、命令与交接

本轮实际只做静态核验和文档写入；动态工作均 `NOT_RUN`：无 build/CTest、parser/validator、logger 故障注入、真实 upload/download、resume/hash/frame 对照、overhead profile、SSH、云端或清理。

预期写后执行并记录的门禁：

```text
git -C D:\Project\CPNetFlux rev-parse HEAD
git -C D:\Project\CPNetFlux diff --cached --name-only
git -C D:\Project\CPNetFlux diff --check
git -C D:\Project\CPNetFlux diff --quiet 3b0820d..HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp
```

并用 UTF-8 严格解码、LF/最终换行和逐行尾随空白扫描检查本结果；不 stage、不 commit。实际写后门禁结果：`rev-parse HEAD` 退出码 0，仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`diff --cached --name-only` 退出码 0 且无输出；`git diff --check` 退出码 0；`git diff --quiet 3b0820d..HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp` 退出码 0（`clean`）；UTF-8/最终 LF/逐行尾随空白扫描首次发现并已去除本文件第 3–5 行 Markdown 换行空格，复核退出码 0。当前资料已核实固定三源码 clean，其他共享文档改动保留。

**交 00 的闸门结论：** 这份裁定给出了唯一可实现形状，解除的是架构歧义，不是实现授权。由于当前 PLAN-04 仍有 no-range §10.1 BLOCKED、retry control 文案和 idle 尾部矛盾，且任务/资料路线版本相差一个版本，当前仍不进入 implementation。03 下一步按上列 8 项更新 PLAN-04；00 同步版本/decision/BOARD；04 对稳定 PLAN-04 做独立复审。只有 QA-04 全项 PASS 后才可创建 implementation worktree。
