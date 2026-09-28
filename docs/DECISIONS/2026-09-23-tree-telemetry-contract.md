# CPNetFlux 目录阶段遥测契约修订

日期：2026-09-23
状态：00 总指挥于 `R2026-09-24.9` 接受 ARBITRATION-03 的三项生命周期裁定；待 03 修订 PLAN-04、04 独立复审。未授权实现、构建、云端实验或 lookahead。
替代关系：取代 `2026-09-17-directory-data-plane-profiling.md` 中与目录阶段事件边界冲突的部分；保留其环境、证据和非因果边界。

## 修订原因

PLAN-01/QA-01 暴露了六个必须先收口的缺口：旧文档把 `first_payload <= payload_io` 当成通用不等式；`manifest_finalize` 同时混入文件提交和树终态；只有终态记录无法识别进程崩溃；run/file/attempt/worker/stream 的可空性未冻结；下载流 ID 在建连后才可得到；旧消费者要求 `error_code`。这些问题会使阶段观测在成功、失败和崩溃场景下不可审计。

## 时间区间与阶段边界

- 使用同一进程 `steady_clock` 的无符号 64 位纳秒时间戳。每个区间分别验证 `end_ns >= start_ns`、`elapsed_ns = end_ns - start_ns` 和无溢出；不再要求不同阶段之间存在未经定义的大小关系。
- `first_payload`：从数据连接完成到首个完整、有效 DATA payload 完成。`payload_io`：从首个 DATA socket I/O 开始到最后一个完整 DATA socket I/O 结束的包络区间，允许包含调度、读取和校验等待；它不是活跃 I/O 时间总和。两者只要求各自非负。
- `data_channel_finalize`：文件数据客户端从最后一个完整有效 DATA payload（空文件则从所有 stream 明确关闭 payload work）到文件传输函数返回，包含 FIN/COMPLETE 以及文件函数内部的 flush、最终验证、rename/commit；不含 FTP 226。
- 同一 attempt 的 `transfer_complete_wait` 从文件传输函数返回开始，到控制面完整解析 226/最终回复结束。二者按当前 tree 调用顺序相接，不重叠。跨文件、stream、worker 的其他并发 span 仍可重叠；所有并发阶段统计都不得相加冒充 wall time。
- `download_mtime_finalize` 新增为下载专用 tree 文件元数据阶段：226 成功返回后、调用 `setRegularFileMtime` 前开始，到该调用返回结束。失败记录 failed；上游未成功则 skipped/upstream_failure。此变更替代“下载 mtime 属于 data_channel_finalize”的旧句。
- `manifest_finalize` 仅表示 tree 终态 `updateRecord` 的 mutex 内状态更新和 manifest 保存；排除 `updateRecordForTransfer`、初始扫描/manifest 建立与块级 session flush。下载顺序为 mtime 阶段后，上传顺序为 226 后。
- Resume skip 分为两类。进入 worker 前 manifest 已为 Completed 的文件以单条 `file_attempt_decision` 记录预先 skip；其必要校验控制流不属于 control phase telemetry。数据握手后才确认所有 stream 无 missing range 的文件仍是实际 `attempt_id>=1` transfer attempt，保留已经发生的 control/data-connect/identity 行，两个 payload span 为 `not_applicable/no_missing_range`，transfer 可分类为 skipped，不追溯改写成 attempt 0。
- attempt 0 skip 决策记录可以含已确定的 `worker_id`（worker 领取任务时确定）；尚未建立或选择的 control/stream ID 必须为 null。其他关联字段、transfer/integrity/evidence 的准确枚举映射由 ARCH-REVIEW-02 确认，并进入后续版本字段表。
- `file_attempt_decision(state=skipped, reason=manifest_completed)` 精确定义为“按入口 manifest 状态决定不启动数据 attempt，并进入 resume validation gate”，不是 gate 成功结果。该单行可在 gate 结束前 append；summary 单独记录 gate 和 process 的最终结果，禁止回写这条 JSONL 行。
- 若 Completed gate 后实际调用 Changed 终态 `updateRecord`，允许同一 `attempt_id=0, attempt_kind=skip` 写一组真实的 `manifest_finalize` start/terminal span；worker_id 必填，control_id 仅在实际关联且已知时填写，stream 字段 null。它是唯一允许 attempt-0 除 decision 外的 stage，字段集合不变；只有源码实际执行终态保存才写。gate 无法取得 control 或在 Changed 保存前失败，不造 manifest span。
- 状态分层：`transfer_status` 描述新 DATA payload work：预先 Completed gate（无论校验成功跳过还是发现 Changed）为 `skipped`；gate/setup 无法判定是否可传为 `blocked`；实际 attempt 有 payload 且协议/文件函数成功为 `completed`，实际 attempt 出错为 `failed`；实际 attempt 全部 stream no-range 时 payload work 为 `skipped`，但独立 `process_status` 记录文件函数及 control completion 的真实成功/失败。no-range 不可仅因无 DATA 把整个操作标为 failed。
- `integrity_status` 只由独立 source/destination 全文件内容 hash 判 `pass` 或 `fail`；元数据、manifest、ResumeResponse、单 chunk checksum 或缺失 hash 均不等于全文件结果，证据不足时为 `unknown`。`process_status`/原始 result 与 exit code 保留 Changed、连接错误及协议结果，不由 transfer_status 覆盖。`evidence_status` 仅评估声明的观测范围是否完整及日志/验证证据是否可读；标明 excluded 的 resume gate 活动不会自动使其 partial，但缺少 gate 结果、必需事件、summary 或 logger 有写入拒绝时应为 `partial`。状态间互不推导。
- 固定阶段名还包括：`run_preflight`、`control_acquire`、`control_prepare`、`data_connect`、`first_payload`、`payload_io`、`interfile_idle`；每阶段声明适用 scope。schema v1 的最终字段与 ID/null 规则由 ARCH-REVIEW-01 结果提供，待 PLAN-03 纳入后由 QA-03 复审。

## JSONL 生命周期与状态

- 新记录使用 `event=tree_stage_v1`、`schema_version=1`，追加写入。每个阶段先写 `state=in_progress` 的 start 记录（`start_ns` 有值，`end_ns`/`elapsed_ns` 为 null），正常终止再写 terminal 记录（`state=completed|failed|skipped|not_applicable|incomplete`）。至少 flush 到用户态缓冲区后才继续；不要求每条记录 fsync。
- 进程在 terminal 记录前退出时，保留 start 记录并在汇总中标为 `incomplete`/`evidence_gap`，不伪造结束时间或零时长。无法写 start/terminal 的错误只影响 `evidence_status`，不能改写 transfer、integrity 或 wire 状态；树摘要或进程结果须独立暴露 evidence partial 和写失败计数。
- 每条新记录都带 `error_code`；正常 start/complete 用 `ok`。未知 schema 或字段拒绝时跳过该条并把 evidence 标为 partial。现有旧事件名、旧字段和 `elapsed_seconds=0` 保持可读。
- run 级记录的 file、attempt、worker、control、stream、transfer、path 字段显式为 null；文件级记录必须有 `file_id`、`attempt_id`、`attempt_kind`。首次传输 attempt 为 1，重试递增；完整 resume 跳过使用 attempt 0、`attempt_kind=skip`，不生成 data_connect/first_payload/226 假阶段。非空文件没有数据时使用 `no_missing_range` 等明确原因；`empty_file` 只用于空文件。
- worker scope 必须有 `worker_id`；stream scope 必须有 `stream_slot`。下载建连时 `stream_id` 可以为 null，SessionInit 后追加 `stream_identity` 记录绑定 slot 与真实 ID，后续阶段同时带两者。
- 解析器只接受十进制无符号整数且检查负数、浮点、超过 uint64 和 elapsed 不一致；不把缺失、未走到、不适用和真实零时长混为一类。

### R2026-09-24.9 生命周期裁定

- `first_payload` 仍从 data connection 完成开始。no-range/empty 只能在 SessionInit/ResumeResponse 后确认时，先追加 `in_progress` start，再追加唯一配对的 `not_applicable` terminal；terminal 复制 `start_ns`，`end_ns`/`elapsed_ns` 为 null，reason 仅为 `no_missing_range` 或 `empty_file`。`payload_io` 没有 DATA I/O 起点，仍为普通单行 N/A。该局部例外不允许回写、补零或改成 attempt 0。
- retry 使用新的 attempt/span/stream 证据，但沿用实际复用的 `control_id`；只有真实控制连接重建才创建新 control 生命周期。retry 的 control_prepare、各 attempt wire 和原始 process 结果分开记录，retry 默认不具备 payload 性能资格。
- `interfile_idle` 在前一文件终态后追加 start。连续领取下一文件以 completed terminal 补 `next_*`；worker 尾部或取消以配对 N/A terminal（`no_next_file`/`cancelled`）闭合；handoff 错误以 failed terminal 记录。不得回写或用零时长代替不适用。

## 兼容与验收

- 旧 `tools/demo/run_alpha_demo.py` 等消费者遇到新行不得因缺省字段误报；新阶段行直接携带 `error_code`，混合旧/新日志仍可解析。若解析器仍需更新，必须在同一任务中增加回归测试。
- telemetry 开关前后用固定输入比较退出码、源/目标 file/tree hash、manifest/resume 结果、文件/字节计数和 DATA frame 逻辑内容；时间、ID 和 TCP 分段允许变化。加入写失败注入，确认传输和完整性不被证据写失败改写。
- 不改 wire frame、manifest 顺序、checksum/resume 语义、scheduler/compression 默认值、IO backend 或连接默认值。lookahead、manifest 优化、云端构建和跨域实验仍需后续独立任务及门禁。

## 下一闸门

03 的 PLAN-03 因识别出 skip 生命周期与固定源码的矛盾而 superseded；其 BLOCKED 证据保留，不作为实现输入。00 按 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-lifecycle-decision.md` 与 ARCH-REVIEW-02 结果冻结上述状态/attempt-0 例外。03 形成 PLAN-04，04 做独立 QA-04。QA-04 明确 PASS 后才能创建 `codex/LOWLEVEL-TREE-TELEMETRY-IMPL-01` worktree 并派发实现。
