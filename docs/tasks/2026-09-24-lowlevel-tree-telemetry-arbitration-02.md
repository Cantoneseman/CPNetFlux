# 00 裁定：attempt-0 decision 与 Changed finalize

路线版本：`R2026-09-24.8`  
输入源码：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`  
依据：`docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md`

## 目标和边界

消除 ARCH-REVIEW-02 留下的两项 attempt-0 行形状歧义，让 append-only 遥测能够如实记录 Completed gate 的决策以及 Changed 状态的实际 manifest 更新。只冻结观测契约，不改传输、校验、resume、协议或产品结果。

## 冻结向量

### Completed resume gate 的 decision 行

- 在 worker 领取 manifest 已是 `Completed` 的文件时，追加唯一的 `file_attempt_decision`：`scope=file_attempt`、`attempt_id=0`、`attempt_kind=skip`、`reason=manifest_completed`、`state=skipped`、时间为 null、`worker_id` 必填；尚未关联的 control/stream/transfer ID 为 null。
- 该行表示“当前不启动 DATA transfer，进入 resume validation gate”，不表示 gate 已成功。不得等待 gate 后再伪造其时间顺序，也不得回写/删除 append-only 行。
- gate 的 control/local/remote validation 操作属于声明的 excluded `resume_validation` scope，不记为零时长 `control_acquire/control_prepare`。如果实际执行了 Changed 终态 `updateRecord`，按下项观测；只有 `controlForFile` 失败且未调用终态更新时不造 manifest span。

### Changed 的 attempt-0 manifest finalize

- 严格 JSON key 集合不变；对 scope/stage/attempt 互斥约束作一个精确例外：Completed gate 的 decision 行之后，如源码实际调用 Changed 终态 `updateRecord`，可以对同一 `file_id, attempt_id=0, attempt_kind=skip` 写一组 `stage=manifest_finalize, scope=file_attempt` 的真实 start/terminal span。
- 此 span `worker_id` 必填；`control_id` 仅当所用连接与该保存明确关联且已知时填写，否则 null；所有 stream 字段 null。其余 ID、时间、state、错误及配对规则沿用 tree_stage_v1。不得把该更新伪装成 attempt 1 或 run-level 无文件 span。
- 未调用终态保存的分支（例如 gate 的 control 获取失败）没有 `manifest_finalize`。改变/保存失败由真实 span failed 表达；不得把已完成 decision 行改为 failed。

### 独立结果轴

- `transfer_status` 只描述新 DATA payload work：Completed gate（校验成功 skip 或判定 Changed）均是 `skipped`；gate/setup 错误导致无法判定是否可传是 `blocked`；真实 attempt 有 payload 且文件协议路径成功是 `completed`，实际 attempt 失败是 `failed`；握手后所有 stream no-range 的 payload work 为 `skipped`。
- 独立 `process_status`/既有 result、error、exit code 保留最终 gate、文件函数和 226/control completion 的真实结果。因此 Completed Changed 为 transfer skipped + process failed/changed；all-no-range 可为 transfer skipped + process completed。status 轴不互相覆盖。
- `integrity_status` 仅以独立 source/destination 全文件 hash 判断：两端有值且相等为 pass，不同为 fail；只靠 stat、SIZE/mtime、manifest、ResumeResponse 或 per-chunk checksum，或任一 hash 缺失，均为 unknown。
- `evidence_status` 评估任务明确声明的 telemetry/验证证据覆盖：被标为 excluded 的 validation 活动不自动 partial；但 gate 终态未在 summary 中留存、预期 in-scope span/identity 缺失、required summary 丢失或 logger 写入/rejection/orphan 非零时 partial。遗漏不改变 transfer/process/integrity。
- wire accounting 独立处理实际测得流量；no-range 仅说明 DATA logical bytes 为 0，不能推导 handshake/FTP 线上 bytes 为零。没有适用的计数证据时 unknown/N/A，不能写数值 0 代替缺失。

## 不变量及后续门禁

两种 skip 不合并：入口 Completed gate 是 attempt 0 decision；握手后 no-range 是 attempt >=1 的真实控制/数据协议 attempt，保留已经 append 的 control/connect/identity 证据。部分 stream 有范围时，仅无范围 stream 的 payload 两阶段 N/A；整个文件依实际文件函数/226 结果记录 process_status。

这项裁定只修正未实现 schema 的约束；03 必须在 PLAN-04 逐行覆盖有效/非法向量，04 QA-04 独立审查状态映射、ID/null 矩阵与 strict parser 向量。PLAN-04/QA-04 通过前不授权代码、worktree、构建、实验或 lookahead。
