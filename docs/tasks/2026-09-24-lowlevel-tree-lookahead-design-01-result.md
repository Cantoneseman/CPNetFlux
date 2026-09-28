# LOWLEVEL-TREE-LOOKAHEAD-DESIGN-01 结果：有界目录 lookahead/preconnect 契约

日期：2026-09-24（Asia/Shanghai）
角色：01 架构与需求；交付：00 总指挥
路线/任务：`R2026-09-24.9` / `v1`
实现输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`

## 1. 结论和证据边界

本结果冻结一个可回退的最小 slice：`lookahead_depth=1`，每个 worker 至多一个候选，每个 run 至多两个额外的 pending control 连接；默认关闭，显式开启后仍不预建未来文件的 data socket。当前 wire 协议要求每个文件先完成 EPSV/传输命令、获得 transfer ID，再由数据连接交换 SessionInit 和 ResumeResponse；在这些决定以前打开未来文件的数据 socket 会把尚未确定的 manifest、resume 和远端文件副作用提前发生。因此“data preconnect”在本版本只保留资源/接口占位，实际连接数上限为零，不能把它写成已实现的性能手段。

资料提供的 dense profile 是 WSL2 loopback、本机隔离服务、128 个 1 MiB 文件、upload/download × file-parallelism 1/8 × 3，共 12 case，全部退出成功且 hash 一致；fp8 没有 wall 收益，高并行时 control_prepare、data_connect、first_payload、interfile_idle 中位数升高。它支持调查固定等待与竞争，不能证明 lookahead 的因果、WAN/GridFTP 收益或生产 readiness。本结果没有运行构建、测试、传输、实验或 SSH，也没有把设计写成实现通过。

## 2. 目标、非目标和唯一推荐形状

目标是让当前 worker 传输文件时，有限度准备下一个可安全候选的控制资源，减少下一文件的控制建连等待，同时在失败、取消、resume 和旧服务端上无条件回到原路径。upload 与 download 均适用。

非目标是改变 framed data、握手、manifest 格式、checksum/resume 语义、默认 scheduler/compression/IO backend、文件分配顺序或单文件客户端；不在本任务引入实际未来 data socket、预发 STOR/RETR、全局 scheduler 重构、连接池扩展、sendv、云端实验或 GridFTP 结论。

唯一推荐配置如下：

| 项目 | 冻结值 | 说明 |
|---|---:|---|
| `lookahead_depth` | `0`（默认）或 `1` | 只接受显式 `--lookahead-depth 1`；大于 1 为参数错误或整 run 回退，不静默放宽 |
| 每 worker pending | 1 | 只允许当前 worker 的一个候选；不做跨 worker 链式预取 |
| run 级 pending control | 2 | `P=min(worker_count,2)`；硬上限，不提供本版运行时放大开关 |
| pending data socket | 0 | 当前协议无法在 transfer ID/ResumeResponse 前安全预连 |
| control reuse=worker | 0 个额外 socket | 只建立候选计划/指纹；worker 已有 control 不并发发后续文件命令 |
| control reuse=off | 至多 `P` 个额外 socket | 可将 ready 的控制 lease 移交给随后文件；失败立即走原 `controlForFile` |
| memory budget | 每个 pending control 的有界状态预算 1 MiB，合计不超过 `P*1 MiB` | 包括命令/回复缓冲和实现声明的 TLS 状态；无法量化或超预算即回退 |
| FD 预算 | 额外 control FD ≤ `P`，额外 data FD = 0 | 启用前要求 `active_fd_estimate + P + 32 <= RLIMIT_NOFILE.soft`；否则关闭候选，不改变传输 |

启用只增加树选项 `TreeTransferOptions.lookaheadDepth`（默认 0）和摘要字段，不改变已有选项默认值。`scheduler=global`、`compression=auto` 或未知后端与本 slice 组合时，必须记录 `unsupported_combination` 并按 depth 0 运行；不改变这些功能本身。实现初版只接入 `runTreeScheduler` 的 worker 模式；global scheduler 维持原路径。

## 3. 固定源码时序和连接边界

固定源码 `tree_transfer_client.cpp` 的 `controlForFile`（约 1151–1178 行）先确保或复用 worker control，再由 `processUploadFile`（约 1307–1474 行）或 `processDownloadFile`（约 1480–1632 行）执行文件级控制。上传在控制成功后才做 Completed 校验、获取 EPSV、发 REST/STOR，随后调用 `runFileTransferClient`；下载同样先过 `controlForFile`，再做本地 Completed 校验、SIZE/MDTM、EPSV、REST/RETR，随后调用 `runFileDownloadClient`。manifest 的 `Transferring`/`Completed`/`Changed` 写入仍由 `updateRecordForTransfer`、`updateRecord` 完成，lookahead 不写 manifest。

`file_transfer_client.cpp` 的 `sendStream` 在约 390–487 行建立 data socket、完成 SessionInit、读 ResumeResponse 后才发送数据；`runFileTransferClient`（约 492 行以后）按 `connections` 建立本次文件的 streams。`file_download_client.cpp` 的 `receiveStream`（约 374–425 行）建连后从 SessionInit 得到服务端 `stream_id`，再计算 missing ranges；下载最终在约 690–758 行完成验证、rename/commit。故未来文件不能先发 STOR/RETR，不能先取得有效 transfer ID，也不能先确定 stream ID 或 missing range。

允许的准备只有：

1. 候选已通过当前 manifest/preflight 的路径、方向、端点、TLS、chunk/buffer、checksum 和文件元数据指纹检查后，开启普通 control 登录并停在 ready；不得发文件级 SIZE/MDTM/EPSV/REST/STOR/RETR。
2. 当前文件完成后，只有指纹完全相等、代际仍有效、候选归属 worker 未改变时，才把 control lease 交给原有 `controlForFile` 等价路径。
3. 真正文件级命令、transfer ID、SessionInit、ResumeResponse、stream ID 和 manifest 状态变化仍在当前 attempt 内按原调用顺序发生。

upload/download 的不对称约束如下：上传候选的本地 stat 可以使用扫描记录作快速指纹，但交接时必须重新 stat；大小或 mtime 变化即失效并走普通路径。下载候选的远端 SIZE/MDTM 只能作为 preflight 记录，交接时仍需按原路径复核；远端变化、权限拒绝或 RETR 失败均不得复用候选到下一 attempt。两方向的 control 登录可对称预建，但 data socket、transfer ID、range 决策和文件提交均不可预建。

## 4. 候选、ID 和状态机

所有新增 ID 在 JSON 中使用十进制无符号整数；不得用负数、浮点或复用终态值。`run_id` 沿用现有 run 字符串；`worker_id` 为 `uint32`、本 run 从 0 到 `W-1`；`file_id` 为 manifest 下标 `uint64`、从 0 开始；`candidate_id` 为 run 内 `uint64`、从 1 开始且永不复用；`generation` 为 worker slot 的 `uint64`、从 1 开始，每次 slot 重置、取消、指纹变化或选项变化递增；`attempt_id` 为文件 attempt 的 `uint64`，第一次实际数据 attempt 为 1，retry 递增，预取阶段为 null；`control_lease_id` 为真实 ControlClient 生命周期的 `uint64`、从 1 开始，真实断开重建才新建；`stream_slot` 是本次文件的本地 `uint32` 槽位，从 0 到 `connections-1`，仅在 attempt 开始时保留；`stream_id` 是数据 SessionInit 后得到的服务端 `uint32`，未收到前必须为 null，不能本地假造或跨 attempt 复用。

候选键固定为：`(run_id, worker_id, candidate_id, generation, file_id, relative_path, direction, manifest_version, size, mtime, record_status, record_transfer_id, resume_mode, endpoint_fingerprint, options_fingerprint)`。`relative_path` 是规范化相对路径；两个 fingerprint 任一改变都使候选 stale。候选不绑定 stream；实际 attempt 产生的 span 和 stream 只绑定该 attempt。

状态机为：

```text
empty -> pending -> ready -> in_use -> released/empty
                 |        |       |
                 v        v       v
               failed   cancelled failed
```

`pending` 表示已占用名额、尚未完成 control 登录；`ready` 表示登录成功但尚未发文件命令；`in_use` 表示 lease 已原子移交给当前文件；`failed` 和 `cancelled` 是不可复用终态。`released` 只表示成功交接后旧 slot 的清空，不把旧 lease 重新标成 ready；下一个候选必须递增 generation/candidate_id。候选失败、取消、stale、worker 退出或 run stop 都关闭其 socket、释放内存和名额，且不回写历史 JSONL。

候选分配必须保留原 manifest 顺序。worker 领取当前索引后，lookahead manager 只能在同一调度锁下对下一个尚未领取的索引建立一次 reservation；reservation 不推进 `nextIndex`，也不阻塞其他 worker。若其他 worker 先领取该文件，原候选立即 cancelled；若当前 worker 在完成当前文件后领取同一 `file_id`，再做原子 lease handoff。任何 reservation 竞态只能造成 miss/fallback，不能重复传输、改变文件归属或跳过 manifest 项。

`control_reuse=worker` 时，已有 `TreeWorkerRuntime::control` 是唯一控制连接；候选只做 plan-only 指纹和预算，不得为同一 worker 再开第二个控制 socket，也不得在当前文件传输期间复用一个 control 并发发下一个文件命令。`control_reuse=off` 时才允许 pending control lease。retry 不创建“每次 retry 一个 control”的假设：固定源码压缩失败重试路径（约 1414–1447 行）实际复用仍存活的 control；lookahead 不参与 retry。retry 必须新建 attempt/span/stream 证据，但沿用同一 `control_lease_id`，只有真实控制连接重建才递增 lease。

## 5. 生命周期和异常回退

### fresh、空文件和 resume

只有 manifest `Pending` 且当前方向/端点/元数据可验证的文件可以成为候选。`Completed` 文件不创建候选；如果候选建立后被 preflight 或当前 gate 判为 Completed，转 `cancelled`，文件仍走原 Completed 校验和 skip 语义。空文件可以命中 ready control，但不得生成 data socket；文件函数按现有空文件成功路径完成，摘要标记 `data_preconnect=not_applicable`，不能把 0 字节当成预连成功。

`resume_partial` 可以预建 control，但候选键必须带原 manifest transfer ID、大小、mtime 和 resume=true；真正 missing range 仍在 SessionInit/ResumeResponse 后决定。`no_missing_range` 不是候选阶段可知的 skip：它产生正常 `attempt_id>=1`，保留实际 control/data-connect/identity 记录；没有 DATA 时 payload 阶段按现有 telemetry 的 `not_applicable/no_missing_range` 表示。完整 manifest skip 仍是 attempt 0，不借用候选 lease 伪造 data 阶段。若 preflight 判定 changed，候选取消，按原 `Changed`/失败路径处理。

### 断连、拒绝、部分失败和取消

control 登录超时、服务端拒绝、TLS 错误、FD/内存预算不足只使候选 `failed` 并计数，当前文件立即调用原 `controlForFile`；同一 generation 不重试预取，避免风暴。当前文件数据失败、226 失败、mtime/rename/manifest flush 失败时，所有尚未 in_use 的候选均 cancelled；文件状态和 transfer/integrity/evidence 由原逻辑独立决定。run cancel、worker stop、首个错误或 `max-files` 停止均先关闭 pending/ready，再退出 worker；in_use lease 由当前文件收尾，不能交给下一文件。

候选从不调用 `updateRecordForTransfer`，不产生远端临时文件、不改变 manifest version。当前文件真正开始 transfer 后才按原路径保存 `Transferring`；226、下载 mtime/rename 和最终 `Completed`/`Changed` 保存的顺序不变。manifest 保存失败不回写候选历史；应关闭或标 stale，下一次从 manifest 重新判断。

## 6. 接口、兼容和可观测性影响

03 实现时需要新增树层 `TreeLookaheadOptions`/`LookaheadPool` 和不可复制的 `ControlLease`，但不修改 file transfer wire API。`processUploadFile`、`processDownloadFile` 接收可选 lease；函数内部仍先执行现有 control/Completed/resume 顺序，命中只替换 control 对象来源。`runTreeScheduler` 为 worker 提供 reservation、generation 和回退；global scheduler、单文件客户端和服务端不接入本 slice。所有接口必须在 lease 不匹配时返回 `fallback` 而不是强制失败。

旧服务端无需 capability 扩展：预建只执行现有 control 登录/auth；服务端拒绝额外控制连接时关闭候选并使用原流程，绝不发新 frame 或依赖服务端理解 lookahead。服务端连接上限、认证频率限制或 TLS 失败只能降低 hit 数，不能影响 transfer/integrity。

不新增 `tree_stage_v1` 阶段名，也不把候选准备时间塞进当前文件 `control_prepare` 或 `data_connect`，避免并发 sum 冒充 wall。每个 tree summary 增加以下可选对象（depth=0 也写 enabled=false 和零计数）：

```json
{"lookahead":{"enabled":true,"depth":1,"mode":"plan_only|control_pending",
  "per_worker_cap":1,"global_pending_cap":2,"pending_peak":0,
  "prepared":0,"ready":0,"hit":0,"miss":0,"fallback":0,
  "failed":0,"cancelled":0,"stale":0,"fd_extra_peak":0,
  "memory_peak_bytes":0,"prepare_elapsed_ns":0}}
```

`prepare_elapsed_ns` 只用于候选摘要，不能并入文件阶段。命中当前文件时，既有 `control_acquire` 仍记录真实 lease handoff；可用的 `reason` 值限定为 `lookahead_hit`、`lookahead_miss`、`lookahead_fallback`。候选没有 attempt/stream，不生成伪造的 `data_connect`、`first_payload` 或 `payload_io`。若严格 telemetry validator 尚未接受 summary 扩展，先只写已有 summary/事件字段并计 `evidence_status=partial`，不把未知行写入 JSONL；04 必须在实现前确认字段白名单。

## 7. 验收矩阵和门禁

### 实现前置门

1. 00 接受本契约，03 将 `lookahead_depth=1`、P=2、data socket=0、worker reuse plan-only 写入实现计划；04 对字段白名单、ID/null、回退和旧消费者做独立 QA。未通过不得建实现 worktree。
2. 选项解析测试覆盖默认 0、显式 1、>1、global/compression 组合回退；状态机单测覆盖 duplicate reservation、generation mismatch、lease double-use、FD/memory budget。
3. 静态检查确认固定 wire frame、manifest/resume/checksum、scheduler/compression 默认值和单文件 API 未变；telemetry 开关前后比较 process/integrity 结果。

### 本地功能与性能矩阵（实现后）

| 组 | 配置 | 必须通过的门 |
|---|---|---|
| A/B dense | 现有 128×1 MiB、同 seed、upload/download × fp1/fp8 × 3；depth 0 对 depth 1，control reuse=worker | 12/12 类 case exit=0、file/tree hash、file_count、logical bytes、manifest 终态相同；worker 模式无额外 control/data socket，记录 plan-only/miss |
| hit slice | 小型 upload/download，control reuse=off、fp1，depth 1 | 至少一次 ready→in_use hit；control 建连计数下降或明确无下降原因；wire/frame、hash、manifest 不变 |
| fresh/no-range | 空文件、非空 fresh、resume 后所有 stream 无 missing range | 空文件无 data socket；no-range 为实际 attempt，不伪造 attempt 0，不判 transfer failed |
| resume_partial/changed | 部分 range、源 stat 改变、远端 SIZE/MDTM 改变 | candidate stale/cancelled，普通路径成功或按原失败；不复用 stale control 到 retry |
| failure/cancel | control 拒绝、数据断连、226/mtime/manifest 保存失败、worker cancel | 无 FD/线程/lease 泄漏；terminal counters 完整；transfer/integrity/evidence 独立，证据写失败不改传输结果 |
| 资源 | fp1/fp8、connections=1/8；采集 FD、RSS/内存预算、CPU | `fd_extra_peak<=2`、无 data pending；`memory_peak_bytes<=2 MiB` 的 pending 预算；不因 fp8 增加无界争用 |

性能只作非劣化门，不预设 loopback 必有收益：每组中位 wall 不得比 depth 0 高超过 5%，p95 不得高超过 10%；若没有命中或 worker reuse 为 plan-only，报告“无收益但契约通过”，不得把阶段相关性写成因果。阶段报告保留独立 wall、control_prepare/data_connect/first_payload/interfile_idle，不能把候选准备时间相加为 wall。

实现前必须完成解析/状态/回退和 correctness 门；CPU、RSS、FD 峰值与 A/B wall 可在固定构建上后置，但在任何云端或跨域准入前必须完成。深圳—上海、真实 GridFTP、WAN 50G/100G、global scheduler、compression auto 和实际 data preconnect 均是后续独立任务，不在本契约验收。

## 8. 风险、放弃选项和下一步

放弃“每个 worker 预连一个未来 data socket”：它需要提前决定 EPSV/transfer ID/REST/SessionInit/range，并可能产生 STOR 临时目标或 RETR 服务端副作用，与当前源码调用顺序冲突。放弃“worker reuse 下再开第二条 control”：fp8 会只增加 FD、TLS 和服务端竞争，而不消除同一 control 上的文件命令串行等待。放弃无上限的全局候选池和 scheduler 预取：会改变分配、公平性和取消清理边界。

剩余风险是 worker reuse 的最小 slice 主要是 plan-only，可能没有端到端收益；control reuse=off 的命中收益也受服务器连接上限和认证成本影响；候选 reservation 与当前 `nextIndex` 竞争需要 03 在单元测试中证明不重排、不重复。若 04 认为 summary 扩展违反已冻结 telemetry 字段白名单，保守方案是只保留既有 `control_acquire` reason 和进程摘要计数，暂停实现并交 00 决定，不擅自改 schema。

建议 00 下一步派发 `LOWLEVEL-TREE-LOOKAHEAD-PLAN-03 v1`：03 仅据本文件写实现计划和文件范围；随后派 `LOWLEVEL-TREE-LOOKAHEAD-QA-04 v1` 做独立契约审查。QA 明确 PASS、且 00 确认实现前置门通过后，才创建 lookahead 实现 worktree；本结果本身不授权代码或实验。

## 9. 本轮执行回执

- 实际输入：固定实现基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；dense profile 结果文件及固定源码调用路径。
- 实际输出：仅新增本文件；未修改源码、测试、runner、CMake、决策、BOARD、ROSTER、云端或 Git index。
- 实际动态工作：构建、CTest、传输、性能实验、SSH、故障注入均 `NOT_RUN`。
- 证据边界：源码顺序和 profile 数字用于契约推理；没有把未执行的实现/测试/性能门写成通过。
- 下一角色：00 收口本契约后派 03 形成计划，再由 04 独立复审；在此之前不得派发实现。
