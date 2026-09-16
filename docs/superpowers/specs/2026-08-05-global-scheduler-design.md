# CPNetFlux Global Scheduler 设计说明

日期：2026-08-05  
状态：设计稿，等待评审  
范围：全局多任务/多链路调度、稳定自适应传输、无损 adaptive-sample 压缩策略  
相关调研：`docs/CONCURRENT_TRANSFER_RESEARCH.md`

## 1. 背景

CPNetFlux 当前已经具备单文件多连接 framed STOR/RETR、目录级 `--file-parallelism`、GridFTP 风格控制面、断点恢复 manifest、chunk checksum 和 `verified_chunks` 事实源。现有目录传输通过 bounded file-level scheduler 同时处理多个文件，每个文件内部继续使用既有 framed data channel 和 per-file manifest。

下一阶段目标不是简单增加 `connections`，而是引入一个可独立演进的 Global Scheduler Core Library，让 CPNetFlux 在普通机器、云服务器和未来 100G/多 100G 环境中都能稳定自适应：普通环境不被激进并发拖垮，高性能环境可通过反馈逐步逼近链路和存储上限。

## 2. 第一原则

### 2.1 优先级

第一优先级是稳定自适应。链路高利用率是高性能环境下的优化目标，但不能以破坏正确性、恢复语义或普通机器稳定性为代价。

默认策略必须满足：

- 普通机器自动保守运行，不因连接、队列、压缩或采样导致 CPU、内存、fd、磁盘压力失控。
- 云服务器 10M 环境用于 correctness、backpressure、resume 和观测验证，不声明 100G readiness。
- 100G/多 100G 环境必须先有 iperf3/fio/host baseline，再评价 CPNetFlux 是否成为瓶颈。
- 压缩不能阻塞 raw path。
- 调度器可以聪明，但不能成为完成事实源。

### 2.2 完成事实源

调度器状态不是完成事实源。完成事实源只能是现有 manifest / `verified_chunks` / tree manifest。

权威持久状态：

- per-file upload manifest。
- per-file download manifest。
- tree manifest。
- `verified_chunks`。
- `manifest_body_crc32c`。
- file-level `committed` / `failed` / `changed` 状态。

非权威调度状态：

- in-memory ready queue。
- in-flight WorkItem。
- per-link feedback window。
- `target_connections`。
- adaptive-sample-lossless 决策。
- 压缩速度、压缩率、吞吐、stall 统计缓存。

允许将部分非权威统计落盘为 cache，但恢复时只能作为 hint。崩溃恢复必须从 tree manifest 和 per-file manifest 派生 missing ranges，重建 WorkItem，丢弃旧 in-flight scheduler state。

## 3. 总体架构

采用方案 B：Global Scheduler 作为独立核心库，先由现有 CLI / tree transfer 调用，未来可升级为 daemon。

```text
CLI / Tree Transfer
  -> Global Scheduler Core Library
    -> TaskQueue
    -> Planner
    -> CompressionAdvisor
    -> LinkManager
    -> FeedbackController
    -> MetricsWriter
    -> Executor Adapter
      -> existing CPNetFlux framed STOR/RETR
      -> existing per-file manifest / verified_chunks
```

Phase 1 不做 daemon，不引入 Prometheus/HTTP metrics，不做数据连接跨文件复用，不强依赖 NIC 名称或 NUMA 绑定。

## 4. Phase 1 设计

### 4.1 Scope

Phase 1 名称：Global Scheduler Core MVP。

Phase 1 做：

- Global Scheduler Core Library。
- 大粒度 WorkItem planner。
- logical LinkProfile。
- 轻量多任务公平队列。
- adaptive-sample-lossless 压缩决策模型。
- raw-first 调度规则。
- CSV + JSONL 指标输出。
- 从 manifest/tree manifest 重建待传任务。
- 保守 ramp-up 的接口和基础规则。

Phase 1 不做：

- 数据连接跨文件复用。
- per-link data connection pool。
- 长连接 multiplex framing。
- daemon/API 服务化。
- Prometheus/HTTP metrics。
- NIC name binding。
- NUMA/CPU affinity。
- 真实 100G readiness 声明。
- 有损压缩。

### 4.2 WorkItem 粒度

调度粒度和 frame chunk 粒度分离：

```text
Scheduler WorkItem: 64MiB-256MiB 级别，偏大
Frame Chunk: 1MiB-8MiB 级别，沿用现有 framed data path
```

规则：

- 小文件可以作为一个 file WorkItem。
- 大文件按 range 切成多个 WorkItem。
- 一个 WorkItem 内部映射到多个已有 frame chunk。
- resume 后从 `verified_chunks` 派生 missing ranges，再生成新的 WorkItem。
- WorkItem 可以跨 link 重派发，但完成状态仍由 per-file manifest 判断。
- WorkItem 元数据必须包含 `task_id`、`file_id`、relative path、offset、length、direction、checksum policy、compression decision、retry count、resume generation。

建议默认：

- `workitem_min_bytes = 64MiB`。
- `workitem_max_bytes = 256MiB`。
- 小于 `workitem_min_bytes` 的 regular file 使用 file WorkItem。
- 第一版可以先用固定大小，后续再根据文件大小、链路速度和存储反馈自适应。

### 4.3 TaskQueue 与轻量公平性

Phase 1 保留多任务接口，但默认路径仍像单任务工具。

```text
TaskQueue = WeightedRoundRobinTaskQueue
default task weight = 1
```

规则：

- 单任务时无额外复杂行为。
- 多任务时按 task weight 轮询派发 WorkItem。
- 同一个 task 内部先按 file/range 队列顺序派发。
- Phase 1 不实现用户 quota、tenant priority、deadline-aware scheduling、cost-aware scheduling。

### 4.4 LinkProfile

Phase 1 使用 logical LinkProfile。即使当前设备只有一条真实链路，也通过同一模型运行。

```text
LinkProfile {
  link_id
  remote_host
  data_port_range
  capacity_gbps
  local_bind_addr optional
  max_connections
  initial_connections
  target_queue_bytes optional
}
```

Phase 1 支持：

- `local_bind_addr` 作为可选绑定入口。
- `capacity_gbps` 用于计算 link utilization 和队列目标。
- `initial_connections` 和 `max_connections` 控制保守 ramp-up 边界。
- per-link ready queue。
- per-link feedback window。

Phase 1 延后：

- NIC name binding。
- RSS/NIC queue awareness。
- NUMA topology。
- CPU pinning。
- PCIe topology。

### 4.5 队列水位与 backpressure

每条 link 维护 ready queue 和水位：

```text
target_queue_bytes = max(2 * estimated_bdp_bytes, min_workitem_bytes)
low_watermark = 0.5 * target_queue_bytes
high_watermark = 2.0 * target_queue_bytes
queue_fill_ratio = ready_bytes / target_queue_bytes
```

Phase 1 没有稳定 RTT 时，可以用 conservative fallback：

```text
estimated_bdp_bytes = capacity_gbps * default_rtt_ms / 8
default_rtt_ms = 10ms unless configured
```

规则：

- link queue 低于 low watermark 时，scheduler 补充 WorkItem。
- link queue 高于 high watermark 时，暂停派发到该 link。
- CPU、写入、send stall 压力升高时，降低派发速度或降低 target connections。
- 普通机器上宁可低利用率，也不能无界排队。

### 4.6 连接策略

Phase 1 不做跨文件数据连接复用。

```text
Phase 1:
  scheduler dispatches WorkItem
  executor still uses existing per-file/per-transfer framed STOR/RETR lifecycle
  control-reuse worker remains compatible opt-in
```

延后：

- per-link data connection pool。
- cross-file data connection reuse。
- long-lived data channels。
- protocol-level multiplex framing。

原因：第一版先稳定调度、观测和压缩决策，不同时修改协议热路径。

### 4.7 CompressionAdvisor

压缩策略锁定为无损：

```text
compression_policy = adaptive-sample-lossless
```

Phase 1 的 CompressionAdvisor 负责采样、试压缩、决策和记录指标。压缩不能阻塞 raw path。

采样规则：

- 对文件或大 WorkItem 做分层随机采样。
- 采样覆盖头部、中部、尾部和随机 block。
- 样本只用于决策，不是完成事实源。

决策指标：

```text
sample_ratio = compressed_sample_bytes / raw_sample_bytes
sample_comp_gbps = raw_sample_bytes * 8 / sample_compress_seconds / 1e9
cpu_pressure = observed_cpu_percent
link_ready_ratio = ready_bytes / target_queue_bytes
```

默认决策：

```text
if sample_ratio > poor_ratio_threshold:
  raw, reason = poor_ratio
else if sample_comp_gbps < min_compress_gbps:
  raw, reason = slow_compression
else if cpu_pressure > cpu_high_threshold:
  raw, reason = cpu_pressure
else if link_ready_ratio < low_ready_ratio:
  raw, reason = link_starvation
else:
  compression_candidate, reason = worthwhile
```

建议初始阈值：

- `poor_ratio_threshold = 0.85`。
- `cpu_high_threshold = 85%`。
- `low_ready_ratio = 0.5`。
- `min_compress_gbps` 第一版从配置读取，默认保守值，不用于 100G 声明。

Phase 1 可以先完成 advisory 和 staging-style 集成验证；真正 compressed WorkItem wire format 和 receiver 解压写原 offset 放到 Phase 3。如果 Phase 1 已接入现有无损压缩工具链，也必须保持 raw fallback，且不改变 manifest 完成语义。

### 4.8 raw-first 调度规则

默认规则：

```text
if compressed_ready_queue has item and link is not starving:
  dispatch compressed candidate
else:
  dispatch raw WorkItem
```

其中 `link is starving` 指 per-link queue 低于 low watermark，或 recent feedback 表示 send path 缺 ready bytes。压缩结果可以提升端到端完成时间，但不能让链路等待压缩。

### 4.9 Executor Adapter

Phase 1 通过 Executor Adapter 调用现有 CPNetFlux framed STOR/RETR 路径。

要求：

- 不改变现有 DATA/FIN/SESSION/CHUNK_COMPLETE 基础语义。
- 不改变 per-file manifest / download manifest 的权威性。
- 不把调度器 in-flight 状态写成完成状态。
- 对 upload/download/resume 都能从 manifest 派生待传 range。
- 保持 `final_verify_policy=full` 默认语义；`verified_chunks` 跳过 final verify 仍按现有 opt-in 条件处理。

## 5. 指标与观测

Phase 1 使用 CSV + JSONL，延续现有 perf 工具风格。不引入 Prometheus/HTTP metrics。

### 5.1 输出文件

`scheduler_summary.csv`：一行一个任务/一次传输，给人看结论。

```text
task_id,total_bytes,logical_bytes,wire_bytes,elapsed_seconds,
goodput_gbps,wire_gbps,compression_ratio_effective,
raw_workitems,compressed_workitems,retried_workitems,
result,dominant_bottleneck
```

`scheduler_events.jsonl`：一行一个调度事件，给排查用。

```json
{"ts":"2026-08-05T12:00:01Z","event":"link_low_watermark","link_id":"link0","ready_bytes":1048576}
{"ts":"2026-08-05T12:00:03Z","event":"ramp_up","link_id":"link0","target_connections":4}
{"ts":"2026-08-05T12:00:05Z","event":"compression_reject","reason":"poor_ratio","sample_ratio":0.93}
{"ts":"2026-08-05T12:00:08Z","event":"workitem_retry","file_id":"dataset-a/file-0001.bin","offset":67108864}
```

`scheduler_samples.csv`：记录压缩采样和性能窗口。

```text
file_id,offset,length,sample_ratio,sample_comp_gbps,
decision,reason,cpu_percent,link_ready_ratio
```

### 5.2 核心公式

```text
wire_utilization = wire_gbps / capacity_gbps
queue_fill_ratio = ready_bytes / target_queue_bytes
compression_ratio = compressed_bytes / raw_bytes
goodput_gbps = verified_logical_bytes * 8 / elapsed_seconds / 1e9
```

### 5.3 可解释瓶颈判断

第一版不做复杂综合评分。用可解释规则判断 `dominant_bottleneck`：

```text
if wire_utilization low and queue_fill_ratio low:
  scheduler_supply
else if wire_utilization low and queue_fill_ratio high and send_stall high:
  network_or_receiver_backpressure
else if compression_time_share high:
  compression
else if receiver_write_time_share high:
  receiver_write
else if cpu_percent high:
  cpu
else:
  unknown_or_balanced
```

指标目标是回答“为什么没跑快”，不是制造不可解释的总分。

## 6. 错误处理与恢复

### 6.1 崩溃恢复

恢复流程：

```text
read tree manifest
for each file:
  read per-file manifest
  validate manifest_body_crc32c
  precheck verified_chunks if existing path requires it
  derive missing ranges
  generate WorkItems
discard old in-flight scheduler state
reuse scheduler cache only as hint
```

### 6.2 WorkItem 失败

WorkItem 失败时：

- 不标记文件完成。
- 不把调度器状态写入 manifest 的完成集合。
- 根据错误类型重试、降级 raw、降低 link target connections 或标记 file failed。
- retry count 达到上限后，将错误传播到 tree manifest 的 file-level status。

### 6.3 压缩失败

压缩失败时：

- 当前 WorkItem 降级 raw。
- 写入 `scheduler_events.jsonl`。
- 更新非权威压缩统计 cache。
- 不影响已 verified chunk。

### 6.4 解压或 checksum 失败

Phase 3 引入 hot-path compressed WorkItem 后，解压失败或 checksum 不一致必须触发 raw 重传或文件失败。完成判断仍以解压后原始 bytes 的 checksum 和 `verified_chunks` 为准。

## 7. Phase 1 验收标准

Phase 1 不承诺 100G。验收口径：

- 本地 correctness 通过。
- 深圳云服务器主测、上海同步验证通过。
- scheduler 不破坏现有 manifest/resume。
- 普通环境开启 scheduler 后资源不失控。
- adaptive-sample-lossless 能正确选择 raw/compressed candidate。
- 不可压数据不会被持续压缩拖慢。
- CSV/JSONL 能解释一次慢传输的主要瓶颈。
- 现有默认可靠性语义保持不变，包括 `final_verify_policy=full` 默认、`verified_chunks` opt-in 条件和 changed-file fail-safe 策略。

建议测试矩阵：

- 本地小目录 upload/download。
- 本地大文件 range planning。
- resume 后从 manifest 重建 WorkItem。
- 压缩采样 poor ratio -> raw。
- 压缩采样 worthwhile -> compression candidate。
- link low/high watermark event。
- fixed low bandwidth cloud smoke。

## 8. 后续 gated roadmap

### 8.1 Phase 2: Feedback-Driven Dynamic Parallelism

目标：让 `connections` 真正由反馈控制。

内容：

- per-link ramp-up/down。
- queue 水位控制。
- send/write/cpu pressure feedback。
- 自动调节 target connections。

入口条件：Phase 1 指标稳定产出。

退出条件：在云限速、本地 loopback、私网环境下，自动策略不低于保守固定参数，并能解释降级原因。

### 8.2 Phase 3: Hot-Path Lossless Compression Integration

目标：把无损压缩真正接入传输热路径，但不阻塞 raw。

内容：

- compressed WorkItem wire format。
- receiver 解压后按原 offset 写入。
- raw fallback。
- 压缩失败重传。
- 压缩统计 cache。

入口条件：Phase 1/2 raw scheduler 稳定。

退出条件：压缩开关不破坏 correctness/resume；不可压数据自动 raw；可压数据端到端收益可解释。

### 8.3 Phase 4: Multi-Link / Multi-Rail Execution

目标：从 logical LinkProfile 走向真实多链路。

内容：

- 多 `local_bind_addr`。
- 多端口段。
- per-link queue。
- 跨 link WorkItem 重派发。
- 链路故障降级。

入口条件：有能验证多链路的环境，至少可模拟多 link。

退出条件：多 link 场景下 work-conserving；单 link 故障时任务继续；完成事实源仍是 manifest。

### 8.4 Phase 5: Data Connection Pool / Protocol Evolution

目标：解决小文件和跨 WorkItem 数据连接成本。

内容：

- per-link data connection pool。
- 长连接。
- 跨文件串行复用或 multiplex framing。

入口条件：Phase 1-4 证明连接建立成本是主要瓶颈。

退出条件：协议语义清晰，resume/manifest 不被连接复用污染。

### 8.5 Phase 6: HPC/100G Readiness

目标：面向真实 100G/多 100G 环境调优。

内容：

- NUMA。
- NIC/RSS。
- CPU affinity。
- network io_uring 或其他网络热路径优化。
- storage striping。
- DTN profile。

入口条件：具备真实硬件或合作环境。

退出条件：用 iperf3/fio/CPNetFlux 分层证明瓶颈位置，不使用云 10M 数据做 100G 声明。

## 9. 与现有工程边界的关系

本设计继承现有工程约束：

- tree manifest 只管理 file-level status。
- per-file manifest / download manifest 管理 chunk-level 恢复事实。
- `verified_chunks` 是恢复事实源。
- `completed_ranges` 只作为派生可读字段。
- 目录层不得复制 chunk 级调度逻辑。
- Phase 1 通过 Global Scheduler 在目录层之上规划大 WorkItem，但最终完成仍回到 per-file manifest。
- Beta 压缩 evidence 仍是 staging/restore 口径，不等价于 hot-path compression 已完成。

## 10. 开发指导结论

Phase 1 的开发应围绕三个核心产物展开：

1. Global Scheduler Core Library：TaskQueue、Planner、LinkManager、FeedbackController、MetricsWriter。
2. CompressionAdvisor：adaptive-sample-lossless 决策和 CSV/JSONL 观测。
3. Executor Adapter：把 WorkItem 安全映射到现有 CPNetFlux framed STOR/RETR 和 manifest 恢复语义。

任何实现变更都必须通过以下检查：

- 是否把调度器状态误当成完成事实源。
- 是否破坏 `verified_chunks` / manifest / resume。
- 是否让压缩阻塞 raw path。
- 是否要求当前设备不具备的 NIC/NUMA 能力。
- 是否让普通机器资源无界增长。
- 是否能用 CSV/JSONL 解释性能结果。
