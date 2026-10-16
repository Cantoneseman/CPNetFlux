# CPNetFlux V3 统一编排与十链路开发路线

- 路线编号：R2026-10-16-V3-UNIFIED-MULTILINK
- 状态：已批准进入实现准备；各实现阶段仍需单独验收
- 生效基线：深圳 `/root/projects/CPNetFlux`
- 输入提交：`922401b49450b47366a026a8285bc72fb52067dd`
- GitHub：`origin/main` 必须与输入提交核对一致
- 编写日期：2026-10-16

## 1. 目标

从本路线开始，CPNetFlux 的 V3 以一个统一的工作执行模型承载：

- 单文件上传/下载
- 目录中的小文件上传/下载
- 目录中的大文件 range 上传/下载
- 控制流水线
- 持久数据通道
- 可选的全局调度器
- 未来的多链路调度

用户可见的传输类型可以不同，但底层必须使用同一套任务身份、租约、连接生命周期、校验、恢复和提交语义。

实际目标网络为 10 条 100Gbps 链路，总标称容量 1Tbps。系统必须能表达每条链路的端点、容量、RTT、BDP、队列、在途字节、健康度和故障状态；不能把“连接数”直接当作“物理链路数”。

## 2. 当前事实与未完成边界

当前提交已经完成：

- V3 事务身份、generation、attempt、checksum、resume、commit 和 range 身份校验。
- DynamicFileQueue 的有界元数据队列、slot 独占、lease 状态和取消回收。
- persistent data worker 与 control pipeline 共享 dispatch/lease/backpressure 语义。
- persistent tree + dynamic queue + CRC32C + pending window=2 的默认目录路径。
- 大文件 range 的身份校验、按 offset 写入、缺失 range 恢复和最终提交。
- V1 wire 已退役。

当前仍未完成：

- persistent data session 与 global scheduler 的统一接入；现阶段仍明确拒绝该组合。
- 单文件、目录和 global scheduler 的执行编排仍有多处独立函数和 worker loop。
- 当前 `GlobalScheduler` 只管理一个 `linkId` 和一个 `LinkState`。
- persistent 目录服务端通道协商仍限制为最多 8 条，不能表达 10 条物理链路。
- 当前 range planner 具备 RTT、带宽、磁盘和调度开销输入，但持久路径仍主要使用默认参数；尚未接入 10 条链路的实时状态。
- 连接切换尚无完整的 connection epoch、在途 frame/byte 计数和 drain/cut 状态机。
- ACK、结果帧或进程中断等情况下的 watchdog 仍需作为统一连接生命周期的一部分验收。
- 每链路 goodput、RTT/BDP、drain 时间、链路切换、lease 回收和链路级重试指标还不完整。

因此，本路线不把当前实现描述为“10 链路完成”或“所有调度器已统一”。

## 3. 已固定的架构决定

### 3.1 一个工作模型

所有传输工作都转换为统一的 `WorkItem`：

```text
WorkItem
  task_id
  file_id
  range_id
  generation
  attempt
  offset
  length
  checksum/state
  source/destination identity
```

工作执行统一经过：

```text
WorkItem
  -> WorkLease
  -> Worker
  -> ConnectionEpoch
  -> framed BEGIN/DATA/END
  -> checksum verification
  -> commit
```

`WorkLease` 必须记录：

- lease_id
- worker_id
- link_id
- connection_epoch
- work item identity
- claimed/in_flight/commit_pending/completed/reclaimable 状态
- deadline
- inflight_frames
- inflight_bytes
- retry/attempt

任何旧 generation、旧 attempt、旧 connection epoch、重复提交或身份不匹配的结果，都必须在写入或发布前拒绝。

### 3.2 一个执行器接口

单文件、目录文件和 global scheduler 不再各自实现一套发送循环。统一执行器至少要有以下边界：

```text
claim(ready_queue, worker_context)
prepare(lease, connection)
send_begin(lease)
send_payload(lease)
send_end_and_checksum(lease)
await_result(lease)
commit_or_reclaim(lease)
cancel_or_timeout(lease)
```

目录扫描、manifest、range planner 和调度器只产生工作项及元数据；worker 才拥有连接和数据传输生命周期。

### 3.3 十链路 LinkSet

增加逻辑上的 `LinkSet`，而不是把 `link0` 扩展成一个大数字：

```text
LinkSet
  link0 ... link9
  each:
    link_id
    local_bind_address
    remote_endpoint
    capacity_gbps = 100
    rtt
    bdp_bytes
    ready_queue_bytes
    inflight_bytes
    current_connections
    pending_window
    health
    connection_epoch
    failure/retry counters
```

调度器每次从健康链路中选择工作项，至少综合：

- capacity 和 capacity share
- RTT/BDP
- ready queue 与 inflight bytes
- CPU、磁盘、发送压力
- retry/failure pressure
- 当前连接数和 pending window
- drain 或暂停状态

第一版采用加权最小负载或 weighted deficit round-robin。权重来自链路容量和健康度；不使用固定的 `file_id % channel_count`。

小文件按文件级分配到一个链路，避免切块成本。超过自适应阈值的大文件按 range 分配到多个链路；每个 range 同时只能属于一个 lease 和一个 link。

### 3.4 安全切换

链路或 worker 需要切换时：

```text
accept new work
  -> drain_requested
  -> stop new claims
  -> draining
  -> wait inflight_frames/inflight_bytes == 0
  -> await result/checksum/commit
  -> cut_ready
  -> reassign only missing work
```

不能在未确认的 payload 中间直接换链路。断线时只回收未完成 lease，已提交 range 保持有效；迟到结果按旧 epoch 拒绝。

ACK 或 result 丢失时必须有超时 watchdog，最终把 lease 转为 reclaimable 或 failed，而不是永久 paused。

## 4. 分阶段任务

### V3-WORK-UNIFY-01：统一执行器契约

范围：

- 定义统一 WorkItem/WorkLease/WorkerContext/ConnectionEpoch 边界。
- 将单文件和目录文件都转换为同一工作项输入。
- 明确取消、超时、断线、重复结果和提交的状态转换。

允许修改路径：

- `include/cpnetflux/core/io/`
- `include/cpnetflux/core/scheduler/`
- `src/core/io/`
- `src/core/scheduler/`
- `tests/unit/`
- `docs/DECISIONS/`
- `docs/tasks/`

非目标：

- 不改变网络协议字段，除非为传输工作身份或 epoch 提供必要且向后兼容的字段。
- 不启用 10 链路。
- 不启动跨域性能实验。

验收：

- 单文件、目录小文件和目录 range 都经过相同 lease 状态测试。
- 取消、RST、timeout、duplicate result、stale generation 均被拒绝或安全回收。
- 原有定向测试和全量 CTest 通过。

### V3-WORK-UNIFY-02：合并执行路径

范围：

- 将当前单文件 worker loop、persistent directory worker loop 和 global scheduler executor 汇入统一执行器。
- 保持 dynamic queue 作为唯一 ready/lease 来源。
- 解除 persistent data session + global scheduler 的当前拒绝条件。
- 保留显式兼容参数，但不再维护第二套状态语义。

验收：

- summary 中能看到统一 worker、lease、connection epoch 和实际调度模式。
- 同一套测试覆盖单文件、目录、小文件、大文件 range 和 resume。
- 不出现 per-file 路径私有的身份字段或独立提交逻辑。

### V3-LINKSET-01：十链路模型和能力协商

范围：

- 增加 LinkSet、每链路配置和运行时状态。
- 控制面协商链路数量、链路能力、端点绑定和最大 pending window。
- 将服务端目录通道上限从当前 8 的单一限制改为协商后的安全上限；默认值仍不能未经测试直接设为 10。
- 将每链路状态写入 summary/event。

推荐接口：

```text
LinkSet::add(LinkProfile)
LinkSet::choose(WorkItem, SchedulerSnapshot)
LinkSet::reserve(link_id, lease_id, bytes)
LinkSet::release(link_id, lease_id, bytes)
LinkSet::requestDrain(link_id, reason)
LinkSet::reclaim(epoch, reason)
```

验收：

- 1、2、4、8、10 条链路的配置解析和能力协商。
- 非法重复 link_id、容量为零、端点冲突、超出对端能力均明确报错。
- 单链路故障不会让其他健康链路上的已提交工作失效。

### V3-MULTILINK-RANGE-01：大文件多链路调度

范围：

- 使用文件大小、链路总容量、每链路 RTT/BDP、磁盘能力、通道数和调度开销计算阈值。
- 小文件保持文件级并行。
- 大文件按 range 分配到 LinkSet；不做重叠 range。
- 只重传缺失或未提交 range。

核心约束：

```text
range_id + generation + offset + length + attempt + checksum/state
```

验收：

- 1/2/4/8/10 链路下 range 分区完整且无重叠。
- 部分链路断开只回收对应缺失 range。
- 所有 range 校验后才允许文件 commit。
- 旧 range、重复 range、错误 offset 和错误 generation 均拒绝。

### V3-FLOWCUT-01：连接 epoch 与安全 drain/cut

范围：

- 在 worker、数据连接和 link 之间加入 connection epoch。
- 为每个连接维护 inflight frame/byte。
- 完成 drain 后才能迁移新工作。
- 对 result/ACK 丢失提供 watchdog 和 lease 回收。

验收：

- 流切换后没有旧 epoch 数据写入新 lease。
- 连接 RST、服务重启、超时和取消都不会永久占用 slot。
- 迁移只影响未完成工作。

### V3-EDGE-STREAM-01：主机侧流式路径

范围：

- 有界 ring buffer 和每链路背压。
- 读、校验、发送流水线。
- 评估 sendfile/splice；TLS 不能使用时明确降级。
- 增加 NUMA/CPU/磁盘压力观测。

非目标：

- 不实现交换机侧聚合。
- 不把 loopback 吞吐当成 1Tbps 结论。
- 不引入未经验证的 RDMA 默认路径。

### V3-STABILITY-10LINK-01：十链路稳定性验收

固定顺序：

1. 现有定向单元/loopback 测试。
2. 全量 CTest。
3. 1、2、4、8、10 链路功能矩阵。
4. 单链路、多链路 RST/timeout/restart。
5. 小文件密集目录。
6. mixed 目录。
7. 大文件多 range。
8. 实验室实体网络的短时阶梯实验。
9. 最后才考虑长时间演示或性能矩阵。

每个 case 必须保留：

- 输入提交和构建参数
- 每链路配置
- manifest/checksum/finalize 结果
- lease/epoch/drain/reclaim 事件
- transfer、integrity、evidence、wire accounting 分列

### V3-DEFAULT-01：默认值提升

只有在上述稳定性门禁通过后，才考虑：

- 将统一 worker executor 设为默认。
- 将 LinkSet 的安全固定策略设为默认。
- 将自适应链路选择设为默认。
- 将 10 链路默认并发提高到经过实测的值。

在此之前，所有高级策略保留显式开关，summary 必须报告 requested/actual/fallback_reason。

## 5. 测试和代码门禁

每个实现任务必须：

- 在独立 `codex/<task-id>` worktree 开发。
- 只提交任务范围内的文件。
- 运行相关 CMake/CTest 或脚本门禁。
- 失败、skip 和 blocked 分开记录。
- 推送 GitHub 后回读远端 SHA。
- 不提交凭据、构建目录、payload、大型原始日志。

## 6. 非目标

本路线暂不做：

- 交换机内聚合或修改用户 payload。
- 包级多路径乱序传输。
- 跨深圳—上海的长时间性能实验。
- RDMA/SmartNIC 作为默认传输后端。
- 桌面端重新设计。
- 重新恢复 V1 wire。
- 用历史 GridFlux 结果替代 CPNetFlux 当前版本证据。

## 7. 回滚

每阶段保持独立提交。出现回归时：

1. 停止扩大测试矩阵。
2. 回读失败 case 和状态事件。
3. 对对应提交执行 `git revert <commit>`，不使用 reset/clean。
4. 保留测试和失败证据。
5. 保持兼容入口，直到新路径重新通过门禁。

## 8. 当前起始点

本路线开始后，第一项实现任务是：

```text
V3-WORK-UNIFY-01
统一 WorkItem/WorkLease/WorkerContext/ConnectionEpoch 契约，
并让单文件和目录执行路径共享同一套租约和提交边界。
```

完成该项前，不启动 10 链路性能实验，也不提升高级策略默认值。
