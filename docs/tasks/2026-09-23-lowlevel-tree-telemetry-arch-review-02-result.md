# LOWLEVEL-TREE-TELEMETRY-ARCH-REVIEW-02 结果

状态：**BLOCKED：静态调用链已核清；两项 attempt-0 schema 向量仍需 00 冻结**

路线/任务版本：`R2026-09-23.7` / `v1`
固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，分支 `main`，ahead 11。起始工作树有其他协作文档修改；本结果路径原为既有短摘要且未跟踪，index 无 staged 路径。本轮仅恢复/替换本结果文件。

## 范围与证据边界

本轮依任务读取生命周期裁定、tree telemetry decision、ARCH-REVIEW-01 结果、PLAN-03 BLOCKED 结果、QA-02 结果及本任务单；对固定提交的 `tree_transfer_client.cpp`、`file_transfer_client.cpp`、`file_download_client.cpp` 做静态行号核对。以下“源码事实”仅说明固定提交的控制流；“契约建议”是供 00 收口、03 写入 PLAN-04 的架构建议。没有把设计要求说成已实现。

本任务无源码、测试、runner、CMake、decision、BOARD、ROSTER、云端或共享 index 改动；不切分支、不建 worktree、不 SSH。parser/validator、构建、CTest、故障注入、真实传输、hash/resume 对照及性能实验全部 `NOT_RUN`。

## 固定源码的调用顺序

**Worker 与文件领取。** `runTreeScheduler` 为每个 worker 创建自己的 `TreeWorkerRuntime`，worker 在线程循环内领取 manifest index 后调用 `processFile`（`tree_transfer_client.cpp:2340-2359`）。源码没有现成 telemetry worker ID；实现时应在线程/worker slot 创建时固定 `worker_id`，领取文件时将该 ID 关联到 file decision。`file_id` 沿用 manifest/planning 的稳定序号，不得按 attempt 重新编号。run-level preflight 没有 worker。

**上传 Completed 候选。** 函数先复制 record 并检查本地源文件 stat、resume metadata；stat 失败或 metadata 不匹配会 `markChanged` 并返回。之后才调用 `controlForFile`；若失败直接返回。再检查 `record.status == Completed`，用远端 SIZE 校验，成功则递增 skipped 计数并返回；失败则 `markChanged` 后返回 Changed 错误（`tree_transfer_client.cpp:1315-1358`，SIZE 函数 `490-500`）。EPSV、可选 REST、STOR、`runFileTransferClient` 在 Completed 成功分支之后（`1361-1409`）。故固定源码没有“Completed 校验失败后同一文件继续 active transfer”的路径。

**下载 Completed 候选。** `processDownloadFile` 先调用 `controlForFile`，失败则直接返回；然后对 Completed 项检查本地目标文件存在性/regular-file/stat 和 size/mtime，失败调用 `markChanged` 后返回，成功记录 skipped 并返回（`tree_transfer_client.cpp:1486-1521`）。active 分支的远端 SIZE/MDTM、EPSV、可选 REST、RETR 和 `runFileDownloadClient` 位于其后（`1524-1592`）。同样没有校验失败后续接 active transfer 的调用顺序。`markChanged` 会经 `updateRecord` 设置 Changed 并保存 manifest，然后设置 scheduler 首错/停止（`1068-1079,1194-1198`）。`controlForFile` 在 worker reuse 下可能新建连接、复用现有连接或调整并行度（`1151-1178`）。

**preflight 边界。** 上传 resume preflight 扫描本地树并校验 manifest metadata；下载 resume preflight 建立单独控制连接，对远端文件查询 SIZE/MDTM，并针对 Completed 项检查本地 metadata（`tree_transfer_client.cpp:1212-1305`）。这发生在 worker scheduler 前；下载目录枚举本身也先建立 run 级控制连接（`2433-2513`）。这些操作属于 run/preflight，而不是某个 worker file attempt 的 `control_acquire/control_prepare`。

**控制面测量边界裁定。** Completed 状态在进入 `process*File` 时已知，但源码仍先执行 `controlForFile`。因此 telemetry 可依据入口 record 状态将该次 Completed gate 中的 control 获取/选择排除在 transfer phase `control_acquire/control_prepare` 外，标注为 `resume_validation` / excluded scope；不得声称该 gate 无控制连接活动，也不得把它编码成零时长。上传 Completed gate 后有 SIZE；下载 worker gate 只有本地 stat，下载远端 SIZE/MDTM 属于之前的 run preflight。已经连接的 worker control 若随后供 active 文件复用，只把 active attempt 实际发生的获取/复用选择记到其 `control_acquire`；不回填或挪用 gate 建连时间。control ID 应在连接生命周期创建时注册、在 worker runtime 中持有，reuse 沿用既有 ID。active attempt 的 acquire span 从实际 `controlForFile` 调用边界开始是可行的，但其含义须覆盖 acquire/lease/reuse，不可仅解释为 socket connect。

**数据握手后 no-range。** 上传 `sendStream` 先连接 framed socket，再执行 SessionInit/ResumeResponse；此后才拿到 missing ranges。即便无范围，仍会发送 FIN 并等 Complete，随后 stream 和文件函数返回成功（`file_transfer_client.cpp:390-416,423-487`；SessionInit/ResumeResponse 见 `165-218`）。下载先建连、解码 SessionInit 取得真实 stream ID，再 prepare session/缺失范围、发送 ResumeResponse，接收端仍等 FIN 并发送 Complete（`file_download_client.cpp:378-425,438-445,625`）。文件结束时仍会做 session missing-range 检查、验证临时文件或已验证块，并完成 rename/commit（`690-758`）。所以所有 streams no-range 是已启动的 `attempt_id>=1, attempt_kind=transfer`，不是预知的 attempt-0 skip；已经写出的 connect/握手/identity 行必须保留。

no-range 是按 stream 判断：只对实际报告无 missing range 的 stream 写 `first_payload` 和 `payload_io` 各一条 `not_applicable/reason=no_missing_range`，时间均 null。若所有 streams 都无 range，以最后一个必要 ResumeResponse/no-work 决议作为所有 payload work 已关闭的证据；按生命周期裁定，从可观测的 `payload_work_closed` 到文件传输函数返回记录 `data_channel_finalize`。若源码接入处未能证明这个起点，则 `evidence_status=partial`，不能补估时长。部分 stream 无 range、其他 stream 有范围时，前者写 N/A，后者记录真实 payload span；attempt 仍是实际 transfer。空文件是不同向量，使用 `empty_file`，不能标为 `no_missing_range`。

## 两类 skip 的建议状态向量

`tree_stage_v1` / `stream_identity` 沿用 ARCH-REVIEW-01 的严格字段全集与 JSON 类型/null 规则；本结果不新增事件字段。attempt/result 三维是 run/process summary 的分类，不塞入 stage 行，也不改变其严格字段集合。

| 向量 | 事件、ID 与阶段 | `transfer_status` | `integrity_status` | `evidence_status` / wire | 性能覆盖 |
| --- | --- | --- | --- | --- | --- |
| manifest 已 Completed，gate 成功 | worker 领取后记录 `scope=file_attempt, stage=file_attempt_decision, attempt_id=0, attempt_kind=skip, reason=manifest_completed`；worker 必填，control/stream/transfer ID 为 null；时间全 null。无 data_connect、payload、data_channel_finalize、transfer_complete_wait、download_mtime_finalize。若无实际终态 updateRecord，不生成 manifest_finalize。 | `skipped`，仅在 gate 校验成功且 run 未启动数据 attempt 时成立。 | SIZE/mtime/stat 只是 resume 元数据校验，不是独立全内容 hash。源/目标独立 hash 均有值且相等才 pass；均有值但不同才 fail/mismatch；缺任一侧则 unknown。 | telemetry 行/summary 完整且 logger 无写失败时，可相对声明的 telemetry scope 为 complete；若 gate 验证所需事实缺失，则 integrity unknown、evidence partial。被排除的控制活动不等于零，也不声称已测。 | 不进 payload throughput 分母。payload wire 可标 N/A；若统计全网 wire，已发生的 gate 控制流量不得伪报为零，且其本身不在此 profile 覆盖内。 |
| manifest 已 Completed，但本地/远端检查发现 changed | 保留入口 decision 作为“没有启动数据 attempt”的决策证据；本地/远端变化路径实际调用 `updateRecord(Changed)` 时需有真实 `manifest_finalize`；不造 transfer attempt、control/data 阶段零值。controlForFile 连接失败的直接返回分支没有 Changed updateRecord，不伪造 manifest_finalize。 | `failed`，原因单列 `changed`；control/setup 失败按实际进程错误分类，不伪装成 transfer pass/skip。 | 除非有独立内容 hash 不匹配，否则 unknown；metadata 不一致不足以判内容 mismatch。 | Changed 保存的成功/失败按实际 span 表达。被排除 gate 的失败若无独立 summary 结果则 evidence partial；不完整 telemetry 不能改写 transfer/integrity。 | 排除性能样本；control 流量若不在 telemetry 计数器范围内，标明 out-of-scope，不能填 0。 |
| 活跃 attempt 全部 streams 无 range | `attempt_id>=1, attempt_kind=transfer`；真实控制阶段、EPSV/RETR 或 STOR、每个实际 data_connect、SessionInit/ResumeResponse、download `stream_identity` 均保留。每个无 range stream 的两个 payload span 各一条 N/A。所有无 range时，记录可证明的 payload_work_closed → 文件函数返回 finalize、其后的 control wait；download 后续 mtime/manifest 按实际执行。 | `skipped` 只表示本 attempt 无新 DATA payload；file API 和控制面流程仍可成功，summary 的 process/result/exit code 需保留真实成功/失败。 | resume manifest、ResumeResponse 或局部 chunk verification 单独不等于独立 source/dest 全内容比对；使用上述独立 hash 规则，否则 unknown。 | 必要事件、终态和 identity 完整，且无 write/rejection/orphan 时 complete；缺少可证明的 payload_work_closed 或 identity/terminal 记录时 partial。 | 实际握手/FIN/COMPLETE/控制 wire 可能非零；仅 DATA logical payload 为 0。可用于握手与 finalize 耗时分析，不进 payload throughput。 |
| 活跃 attempt 部分 stream 无 range、部分有 range | 一个 attempt，attempt>=1；无 range slot N/A，有范围 slot 的 payload 计时按实际事件；stream identity 依 download SessionInit 实际绑定。 | 根据整次文件函数和 226 实际结果为 completed/pass 或 failed，不因部分 slot 无工作将全文件改为 skipped。 | 独立 hash 三值规则；不能由单流 checksum 或缺失的 hash 推导全文件 pass/mismatch。 | 各 slot 及 attempt terminal 均完整则 complete，否则 partial。 | 只以实际新 missing logical bytes 作为新 payload 分母；新 payload 为零时不算 goodput 样本。全文件 size 不能冒充本轮有效负载。 |

### 失败、早退与状态互斥

- Completed candidate 的 `controlForFile` 失败会在 Uploaded/Downloaded per-file 函数直接返回；不得发明 control ID、成功的 skip 验证或 transfer span。入口 decision 仍说明“未启动 data attempt”，summary 需保留实际失败。数据完整性通常 unknown；依 00 frozen “验证缺证据为 unknown/partial”要求，证据不足时 evidence partial。
- Uploaded Completed 的源文件 stat/metadata 变化、远端 SIZE 不符/失败会走 Changed；Downloaded Completed 的本地目标 stat/metadata 变化走 Changed。调用 `markChanged` 的分支会真实保存 Changed；controlForFile 失败分支不调用 Changed 保存。事件只能按实际被调用的 `updateRecord` 记录。
- data attempt 一旦开始，不得再出现 attempt 0。stream slot 按计划在范围结果判定前预留；attempt 内重试 ID 递增且失败消耗的 ID 不复用；下载 stream ID 仅在成功解码 SessionInit 后绑定，未收到则不得推测。上传传入的 stream ID 是协议参数，但不产生 download-only identity event。
- `skipped`、`not_applicable`、`failed`、`completed` 互斥；N/A 的 `reason=no_missing_range` 只给真实已判定无工作 stream，`empty_file` 只给空文件。setup 早退只终止真实开始的 stage；未执行阶段是 skipped/upstream_failure 或完全不生成，不能写成功零时长。

### ID、span 与关联键

- `run_id`：每次 tree run 唯一 UUID；`file_id`：manifest 顺序稳定序号、run 内固定；attempt 首次真实数据传输为 1，retry 递增；0 只用于完整 resume 候选 skip sentinel。
- `worker_id`：worker slot，创建时固定、领取文件时关联；同 worker 可服务多文件。最新 lifecycle decision 明确要求 attempt-0 decision 行 worker 必填，这覆盖 ARCH-REVIEW-01 旧的“skip 行 worker null”向量；PLAN-04 应显式记为此次窄覆盖。`control_id`：具体控制连接生命周期，连接创建前预留，失败也消耗且不复用；worker reuse 沿用原 ID。gate decision 的 control ID 仍 null，因为 control 连接不属于其 telemetry phase 关联。
- `stream_slot`：attempt 内逻辑计划 slot，范围判定前预留；连接失败或无 range 均消耗且不复用。`stream_id`：线上协议事实，不能从 slot 推算；download 成功解析 SessionInit 后用 identity 事件绑定，attempt 内唯一。
- `span_id`：run 内 uint64，从 1 起，每一 timed span、N/A 或 skip 行写入前预留；记录失败也不回收。span start 与 terminal 的 run/span/stage/direction/scope 和实体键一致；终态不得重复。`stream_identity` 是独立事件，不伪造 span/time 字段。
- 上述类型、严格字段全集、十进制 JSON uint64 词法、0/null 区别、terminal 配对、重复/孤立 start 和 identity 错 slot/冲突规则继承 ARCH-REVIEW-01；本次未运行 parser。具体机械样例应由 PLAN-04 逐字复制 ARCH-REVIEW-01 的有效/反例，再新增下面两种 lifecycle 向量，不应在本结果臆造测试通过。

## 两项未决的严格 schema 向量（交 00 收口）

1. **attempt-0 decision 的 state 与 append-only 语义。** 00 lifecycle decision 要求 worker 领取 Completed 文件时立即追加 `file_attempt_decision(reason=manifest_completed)`；ARCH-REVIEW-01 将完整 skip 表为单行 `state=skipped`，但 00 又只在后续 gate 成功时把 summary `transfer_status` 记为 skipped，失败可能走 Changed。最小建议是冻结该行的 `state=skipped` 精确定义为“manifest 入口决定不启动数据 attempt、进入 excluded validation gate”，不宣告 gate 已验证成功；最终 transfer/process 状态在独立 summary 后置映射。若该行必须代表“校验成功的最终跳过”，则不能在入口时写 terminal；需要 00 改定为验证成功后才追加，或制定不违反单 terminal 的失败候选行。不能把事件延迟并声称它在调用顺序中先发生，也不能回写 JSONL。

2. **Changed 的 attempt-0 `manifest_finalize`。** ARCH-REVIEW-01 的 strict scope/null 条件仅允许 `attempt_id=0, attempt_kind=skip` 出现在 `file_attempt_decision`；00 lifecycle decision 要求 Changed 分支正常记录真实终态 manifest update。固定源码确实在 Completed 变化分支调用 `updateRecord(Changed)`，它既不是 active transfer，也没有 attempt>=1。最小兼容建议是在字段全集和 v1 version 不变的前提下，为这个唯一真实分支增加一个精确 null-vector 例外：`scope=file_attempt, stage=manifest_finalize, file_id=<同文件>, attempt_id=0, attempt_kind=skip, worker_id=<实际 worker>, control_id/stream_id/transfer_id 按实际关联规则，真实 start/terminal`；仅实际调用终态 updateRecord 才发 span。若 00 不接受该例外，则该更新不能声称已由 tree_stage_v1 观测，summary 必须显式 `evidence_status=partial`；不能冒用 attempt 1、伪造 run scope 文件关联或隐瞒 updateRecord。

以上是状态/null 向量闭合项，不要求变更 schema 字段集合、协议或传输行为。其余映射建议可直接进入 PLAN-04；这两项没有 00 的决定前，本审阅不建议标为可实现 PASS。

## PLAN-04 与后续 QA 的落地清单

1. 文件开头记录固定源码 SHA、资料 HEAD、schema version、run summary 与严格 stage row 的边界。
2. 文件领取时固定 worker ID；以入口 manifest 状态选择 Completed validation gate 或 active transfer。明确 `resume_validation` 和 run preflight 的 excluded coverage，active 的 `control_acquire` 从实际调用/选择边界计时且区分新建、失败、reuse。
3. 按表列出 Completed 成功、Changed、control setup error、no-range 全部/部分 stream、empty file、SessionInit 前连接失败、retry 向量；只对实际发生的控制、数据和 manifest 调用写事件。
4. `transfer_status` 表示本次数据工作结果（no-new-payload 可为 skipped）；单独保留文件函数/run exit 的实际 process result。`integrity_status` 仅由独立内容 hash 得 pass/fail，缺边为 unknown。`evidence_status` 只反映遥测/验证证据完整度与 sink/parser 错误；不得把 unknown 转成 mismatch。wire accounting 独立：协议握手字节可能非零；若计数器仅覆盖 DATA payload，则 no-range 的 payload accounting 为 N/A，不代表总 wire 为零。
5. profiling eligibility：预先 Completed skip 不进传输性能样本；所有 stream no-range 仅参与握手/finalize 生命周期分析，不进 payload goodput；部分有 payload 的 resume 样本仅以实际 missing logical bytes 计 throughput 分母。stage 统计可重叠，summary 另保留 wall elapsed，不能以 sum 假装 wall time。显式标明未测 run preflight、Completed validation gate、服务端内部工作及 runner 外部 hash 的边界。
6. QA 至少校验每个状态向量单行 JSON 可通过/非法向量会拒绝：attempt-0 decision worker 必填其余未获关联 ID null；Changed finalize 的获批例外；no-range attempt>=1 且 identity/event 顺序不回写；每 stage N/A 三个时间字段 null；hash 缺失是 unknown 而非 mismatch；日志缺行只使 evidence partial。执行前本回执所有相应测试仍是 NOT_RUN。

## 本轮命令、结果与未运行项

- `git -C D:\Project\CPNetFlux rev-parse HEAD`：退出码 0，`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- `git -C D:\Project\CPNetFlux status --short --branch`：工作分支 `main`，相对 `legacy-reference/main` ahead 11；共享目录有其他未提交文档。目标结果路径为未跟踪文件。
- `git -C D:\Project\CPNetFlux diff --cached --name-only`：退出码 0、无输出；index 空。
- `git -C D:\Project\CPNetFlux diff --quiet 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp`：退出码 0，`FIXED_SOURCE_DIFF=clean`。
- `rg -n` 定位上述文档契约与固定源码调用点：退出码 0，命中位置见本文代码引用；命令只读。
- 写后要求复核：`git rev-parse HEAD` 与上值一致；cached index 无变化；固定三源码对比 clean；`git diff --check` 成功（此命令本身只检查 Git 跟踪的 diff；本未跟踪结果文件另以 UTF-8 解码和逐行尾随空格扫描检查）；结果必须以 LF 结尾。写后输出记录在总控执行核验中。
- parser/validator、CMake/build、CTest、Python tests、run_alpha_demo 回归、故障注入、上传/下载/resume/hash/frame 对照、performance profile、SSH/cloud、清理：全部 `NOT_RUN`。

## 执行回执与交接

- 输入源码 commit：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；输入资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。恢复后的目标文件为唯一改动；没有 stage/commit。
- transfer / integrity / evidence / wire accounting 已按状态向量分别列出；缺失 hash/验证事实按 unknown/partial，不判 mismatch。历史/当前性能与 correctness 未作任何新结论。
- 当前阻塞：等待 00 冻结上述两项 attempt-0 schema 向量；不覆盖 PLAN-03 BLOCKED 证据。
- 下一步：00 决定后，03 把已裁定的两项及表中状态向量写进 PLAN-04；04 对完整 PLAN-04 做独立 QA-04。QA 明确 PASS 前不创建 implementation task。

这是固定源码上的架构静态审阅，不是 telemetry 实现、测试、传输、正确性或性能验收。
