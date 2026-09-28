# 00 裁定：resume skip 的遥测生命周期

路线版本：`R2026-09-23.7`  
输入源码：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`  
依据：`docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-03-result.md`

## 目标与范围

闭合两类 resume 跳过在固定源码中的事件语义，使 append-only telemetry 如实记录已发生的控制/数据活动，同时把实际传输状态、完整性状态和证据状态分开。此次只调整测量契约，不改传输、resume 或协议行为。

非目标：不调整优化路线、不改源码/测试/runner/CMake、不构建/实验/SSH、不清理云端、不声称性能验收。

## 冻结裁定

### manifest 已标记 Completed 的文件

- 在 `controlForFile` 之前观测器尚不能知道最终采用哪个 control 连接；不得用虚构 control ID 写 `control_acquire` start。
- 将 run 内用于调度的 `worker_id` 在 worker 取得 file task 时确定；为这一文件决策写一条 `file_attempt_decision`：`scope=file_attempt`、`attempt_id=0`、`attempt_kind=skip`、`reason=manifest_completed`、时间为 null，worker_id 必填，其他尚未取得的关联 ID 为 null。
- 文件完成性校验所需的 `controlForFile`、SIZE/MDTM 或本地 stat/hash 是 `resume_validation` 子过程，**不在 transfer phase telemetry 的 control_acquire/control_prepare 覆盖范围**。不能将该路径写成阶段零值。该边界只豁免已完成项判定路径；尚未判定的 transfer 项仍从实际 control acquire 开始观测。
- 校验成功后 transfer_status=`skipped`；校验缺证据时为 `unknown/partial`（由既有结果 schema 映射待 01/03 在 PLAN 更新时写明），不得伪装 mismatch。文件变化按当前代码结果单独失败/changed，并正常记录实际终态 manifest 更新。

### 数据握手后发现 missing range 为空

- 这是已启动的数据 attempt，不是预先可知的完整 skip：`attempt_id>=1`、`attempt_kind=transfer`。
- 保留真实发生的 control、data_connect、SessionInit/ResumeResponse 和 stream_identity 证据；不能撤销/删除 append-only 行。
- 对每个实际无 missing range 的 stream，`first_payload` 和 `payload_io` 各写一条 `not_applicable`、`reason=no_missing_range`，三个时间字段 null。若所有 stream 均无 range，attempt 仍可没有 DATA payload。
- `data_channel_finalize` 只有在代码实际调用文件传输函数且存在有效终止事件时才按实际边界记录；若客户端函数在协议上成功完成但没有 payload，则使用明确的 `payload_work_closed` 作为起点，状态 completed 且 elapsed 可真实为 0。若代码没有可证明此事件的观测点，则 evidence partial，不补估时长。
- 该 attempt 的 `transfer_status=skipped`（不把“完成了握手但无传输字节”计为失败）；`integrity_status` 依赖 resume 的实际独立验证证据，缺证据为 unknown；`evidence_status` 取决于日志完整性。三者独立。

### 统一状态优先级

- 是否为预先 skip，由进入 file worker 前已有的 manifest `Completed` 状态决定；后续控制/数据握手中发现无 missing range 不追溯改成 attempt 0。
- 完整 resume skip 是 `attempt_id=0` 的 file decision；任何已启动的数据连接必须属于 `attempt_id>=1`。
- 实际未执行的阶段可以是 null 时间的 `skipped/upstream_failure`；语义不适用是 `not_applicable`。不得给无事件阶段写 elapsed 0。
- QA-02 其余 schema、ID、logger、parser 裁定继承 ARCH-REVIEW-01；本文件只覆盖与固定源码冲突的 skip 分类及覆盖边界。由 01 审核上述与四维结果的映射，再由 03 更新 PLAN-04；04 QA-04 独立复审前不得实现。

## 下一步

本裁定接受固定源码下的两类不同 skip 语义。01 需确认 transfer/integrity/evidence 字段映射和 preflight 覆盖边界；03 随后制作可完整审阅的 PLAN-04；04 执行 QA-04。若角色发现新矛盾，停止并交 00，不扩大修改范围。
