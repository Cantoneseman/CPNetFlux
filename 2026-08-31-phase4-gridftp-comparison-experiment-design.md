# CPNetFlux Phase 4 与真实 GridFTP 并发传输对比实验设计

日期：2026-08-31
状态：设计稿，实验口径已确认，等待实现实验工具
范围：CPNetFlux Phase 4 文件传输路径、现有 framed STOR/RETR 并发能力与真实 GridFTP 的黑盒对比
主要测试环境：深圳云服务器
同步验证环境：上海云服务器

## 1. 实验目的

本实验用于建立一套可复现、可审计的对比方法，回答以下问题：

1. 在相同数据集、方向和网络环境下，CPNetFlux 的单文件多连接能力与真实 GridFTP 的并行能力分别如何。
2. 在包含多个文件的目录传输中，CPNetFlux 的文件级并行度、单文件连接数以及二者组合是否能够提高总体有效吞吐。
3. CPNetFlux Phase 4 的 IO 后端、队列深度和批量提交参数是否会影响端到端传输表现。
4. Global Scheduler 的固定策略和自适应策略是否能够在保持正确性、背压和恢复语义的前提下稳定运行。
5. 在深圳和上海的 10M 公网环境中，能够确认哪些 correctness、backpressure、resume、稳定性结论，哪些高吞吐结论必须明确标记为未验证。

本实验的主要结论目标是“并发传输能力和行为对比”，不是制造一个脱离链路上限的极限吞吐数字。

## 2. 设计边界

### 2.1 实验范围

实验分为三个互补的测试层次：

| 测试层次 | 对象 | 目的 |
|---|---|---|
| A. 原始并发黑盒对比 | CPNetFlux raw path 与真实 GridFTP | 比较端到端并发传输能力 |
| B. CPNetFlux 调度器行为 | CPNetFlux `scheduler=off/global`、`fixed/adaptive` | 验证调度、背压、稳定性和可观测性 |
| C. CPNetFlux Phase 4 IO 消融 | POSIX 与 io_uring，以及 queue depth、batch size | 判断 IO 后端对传输结果的影响 |

A 层是与 GridFTP 的主要公平对比；B、C 层是 CPNetFlux 内部机制验证，不应伪装成 GridFTP 的等价功能对比。

### 2.2 明确不做的内容

本阶段不包含以下内容：

- 不实现 daemon，不把 Global Scheduler 改造成常驻服务。
- 不做 Prometheus、HTTP metrics 或其他服务端指标接口；只输出 CSV、JSONL 和日志。
- 不做数据连接跨文件复用。
- 不强依赖 NIC name、NUMA、CPU pinning、RSS、PCIe topology 或特定硬件拓扑。
- 不做 100G readiness、线速 readiness 或高端网络扩展声明。
- 不使用 scp、rsync、SSH tunnel 或其他工具替代真实 GridFTP。
- 不引入有损压缩。实验中的压缩只允许无损策略。
- 不把 scheduler 状态当作完成事实源。
- 不改变 manifest、verified_chunks、tree manifest 的完成语义。
- 不因本实验重构与实验目标无关的模块，不修改 unrelated files。

深圳和上海的公网带宽约为 10M。该环境只能用于证明正确性、背压、恢复、指标完整性和低带宽下的稳定行为，不能用于证明 100G 或其他高带宽 readiness。

## 3. 必读文档与代码入口

实验工具实现前，必须阅读以下文档：

1. `docs/superpowers/specs/2026-08-05-global-scheduler-design.md`
2. `docs/CONCURRENT_TRANSFER_RESEARCH.md`
3. `docs/DESIGN.md`
4. `docs/ENGINEERING.md`
5. `docs/ROADMAP.md`
6. `docs/perf/README.md`
7. `docs/perf/PHASE1_GLOBAL_SCHEDULER_MVP.md`
8. `docs/perf/PHASE2_GLOBAL_SCHEDULER.md`
9. `docs/perf/PHASE3_HOT_PATH_COMPRESSION.md`
10. `docs/perf/PHASE4A_BASELINE.md` 至现有最新的 Phase 4 验收文档

实现者还必须检查当前代码与远端工作树的实际状态，不得只依据本文档假定文件已经存在。重点代码入口包括：

- `include/cpnetflux/core/scheduler/`
- `src/core/scheduler/`
- `src/core/io/file_transfer_client.cpp`
- `src/core/io/file_download_client.cpp`
- `src/core/io/file_download_sender.cpp`
- `src/core/io/tree_transfer_client.cpp`
- `src/storage/file_io.cpp`
- `src/storage/file_io_uring.cpp`
- `src/storage/file_io_uring_stub.cpp`
- `tools/perf/run_gridftp_private_matrix.py`
- `tools/perf/run_gridftp_tree_private_matrix.py`
- `tools/experiments/beta_matrix/`

其中，CPNetFlux 的数据传输仍必须通过现有 framed STOR/RETR 路径完成，实验工具不能另起一套传输协议。

## 4. 当前实现映射

### 4.1 CPNetFlux 并发控制

单文件传输使用 `connections` 控制并行连接或数据流数量。目录传输使用：

- `--file-parallelism`：同时处理的文件数；
- `--connections`：每个文件使用的连接或数据流数；
- `--scheduler off|global`：关闭或启用全局调度；
- `--scheduler-policy fixed|adaptive`：固定或自适应调度策略；
- `--control-reuse off|worker`：控制连接复用策略。

CPNetFlux 的理论并发预算可用下式描述：

```text
total_stream_budget = file_parallelism * per_file_connections
```

该值只是配置预算，不等于实际同时存在的数据 socket 数。正式结果必须采样并记录实际最大数据流数。

### 4.2 CPNetFlux Phase 4 IO

Phase 4 相关变量包括：

- `file_io_backend=posix|io_uring`
- `queue_depth`
- `batch_size`
- POSIX write strategy
- buffer size 和相关 writeback/advice 配置

IO 后端消融只用于回答“本地 IO 路径是否影响端到端结果”，不能把 io_uring 当作网络并发机制，也不能把单机 IO 结果直接外推为 100G 能力。

### 4.3 正确性事实源

实验结果中“完成”的判断必须依赖：

- 单文件：源文件与目标文件的独立 SHA-256 校验；
- 断点恢复：manifest 与 `verified_chunks`；
- 目录传输：tree manifest、文件数量、文件大小、独立 tree hash；
- 客户端和服务端进程退出码。

调度器中的 completed、drained、released 或类似状态只能作为运行时指标，不能替代上述完成事实。

### 4.4 外部 GridFTP

外部基线使用真实 Globus GridFTP 工具：

- `globus-url-copy`
- `globus-gridftp-server`

当前深圳、上海环境已观察到相关工具存在，但正式实验前仍必须执行版本、认证、服务端、控制端口和 passive/data 端口的 preflight。历史上曾经可以用 anonymous GridFTP 完成实验，但不能把历史结果当作本次运行的证据。

## 5. 实验拓扑

### 5.1 主拓扑

主实验使用深圳服务器作为主要开发和测试端：

```text
深圳 cpnetflux-beta-shenzhen
    120.25.121.51
    /root/projects/CPNetFlux-Beta

上海 cpnetflux-beta-shanghai
    47.116.174.181
    /root/projects/CPNetFlux-Beta
```

两端分别执行：

1. 深圳发送、上海接收；
2. 上海发送、深圳接收。

两个方向必须分开运行，不在同一时间并发运行，以避免把双向竞争误认为单向并发能力。

### 5.2 服务端与端口

CPNetFlux 使用项目已有的 `cpnetflux-gridftp-server` 和 framed STOR/RETR 客户端。真实 GridFTP 使用独立启动的 `globus-gridftp-server` 与 `globus-url-copy`。

每个 case 必须使用：

- 独立的 transfer ID；
- 独立的源目录和目标目录；
- 明确记录的控制端口、passive/data 端口范围；
- 独立的客户端和服务端日志路径。

不能覆盖生产目录，不能复用上一个 case 的未清理目标目录，也不能通过修改全局服务配置来隐藏端口或认证问题。

## 6. 外部 GridFTP preflight

只要以下任一项失败，外部 GridFTP 结果必须标记为 `blocked_external_gridftp`。不得用 scp、rsync 或其他协议填补该列。

Preflight 至少检查：

1. `globus-url-copy --version` 或等价版本信息；
2. `globus-gridftp-server` 可执行且能启动；
3. 实际使用的认证模式，例如 anonymous 或证书认证；
4. 控制端口可达，默认端口通常为 2811，但必须以实际配置为准；
5. passive/data 端口范围可达；
6. 一个独立的最小 GridFTP STOR/RETR smoke；
7. smoke 传输后的 SHA-256、文件大小和退出码；
8. 客户端、服务端日志能够保存并关联到 case ID。

Preflight 必须输出 JSON，至少包含：

```json
{
  "status": "pass",
  "tool": "globus-url-copy",
  "tool_version": "...",
  "server_version": "...",
  "auth_mode": "anonymous",
  "control_port": 2811,
  "data_port_range": "...",
  "smoke_hash_match": true,
  "server_log": "...",
  "client_log": "..."
}
```

如果认证材料、证书、密钥或防火墙规则缺失，必须保留原始错误信息，并把所有需要 GridFTP 的正式比较结果标记为 blocked，而不是报告 CPNetFlux “胜出”。

## 7. 数据集设计

数据集必须确定性生成，并为每次实验保存生成参数、文件清单、总字节数和 SHA-256/tree hash。不能使用未记录内容的随机临时文件。

建议初始数据集如下：

| 名称 | 内容 | 总大小 | 用途 |
|---|---|---:|---|
| `single_256MiB` | 一个 256MiB 文件 | 256MiB | 主单文件并发对比 |
| `tree_dense_128MiB` | 128 个 1MiB 文件 | 128MiB | 小文件密集型目录并发 |
| `tree_mixed_256MiB` | 128 个 512KiB、16 个 4MiB、4 个 32MiB 文件 | 256MiB | 混合文件大小和调度行为 |
| `single_1GiB` | 一个 1GiB 文件 | 1GiB | 选定配置的稳定性和 IO 消融 |

每个数据集至少保存：

- `dataset_manifest.json`
- 文件相对路径；
- 文件大小；
- 文件 SHA-256；
- 总逻辑字节数；
- tree hash；
- 生成器版本和固定 seed；
- 生成时间及主机信息。

`single_1GiB` 可根据 10M 链路耗时和磁盘空间调整为 512MiB，但调整必须记录，不得把不同总大小的数据混为同一组统计。

## 8. 公平性与控制变量

CPNetFlux 与 GridFTP 的正式比较必须固定以下变量：

- 同一数据集、同一文件内容和同一目录结构；
- 同一传输方向；
- 同一源端和目标端文件系统；
- 每个 case 使用全新的目标路径；
- 同一组重复次数和相同的 warm/cold 状态记录；
- Release 构建，不使用 Debug 性能结果；
- 同等数量级的 buffer/chunk/block 配置；
- 主对比关闭压缩，确保比较的是 raw transfer path；
- 主对比使用 `checksum=none`，独立 SHA-256 负责最终正确性；
- 可靠性辅助矩阵可使用 CRC32C，但必须单独标记；
- 不在结果中把 manifest 校验流量隐藏为 data throughput；
- 记录实际 socket 数，而不只记录命令行参数；
- 记录 CPU、内存、磁盘和队列状态，至少保存主机环境快照。

正式比较前不得因为某个工具结果较差而为该工具单独更改数据集、重复次数、目标文件系统或清理规则。

### 8.1 原始路径配置

CPNetFlux raw 对比建议使用：

```text
scheduler=off
compression=off
checksum=none
file_io_backend=posix
queue_depth=1
batch_size=1
```

如果命令行或默认配置仍可能触发压缩，结果必须检查 `wire_bytes == logical_bytes`，否则该 case 不能进入 raw 对比统计。

### 8.2 GridFTP 配置映射

单文件：

```text
CPNetFlux: --connections N
GridFTP:  -p N
```

目录：

```text
CPNetFlux: --file-parallelism M --connections N
GridFTP:  -r -cc M -p N
```

两者的参数语义并不完全相同，因此报告中必须同时列出配置预算和实际观测值。`-cc`、`-p` 以及 CPNetFlux 的 `file-parallelism`、`connections` 不能只按名称推断等价。

## 9. 实验矩阵

### 9.1 Preflight 与 smoke

| Case | 数据集 | 方向 | 并发 | 重复 |
|---|---|---|---|---:|
| `preflight` | 64MiB | 双向各一次 | 单流 | 1 |
| `local_smoke` | 64MiB | 本机或可控端到端 | 单流 | 1 |

Preflight 不用于性能排名，只用于确认工具、认证、端口、服务进程和正确性链路可用。

### 9.2 单文件原始并发对比

数据集使用 `single_256MiB`，两方向分别运行：

| 配置 | CPNetFlux `N` | GridFTP `-p` | 重复 |
|---|---:|---:|---:|
| 单流基线 | 1 | 1 | 3 |
| 低并发 | 2 | 2 | 3 |
| 中并发 | 4 | 4 | 3 |
| 高并发 | 8 | 8 | 3 |

如果 10M 链路已经在单流时接近链路上限，则应报告“链路受限”，不能仅凭高并发没有明显增长就判定并发实现无效。

### 9.3 密集小文件目录对比

数据集使用 `tree_dense_128MiB`，两方向分别运行：

| 配置 | CPNetFlux `M` | CPNetFlux `N` | GridFTP `-cc` | GridFTP `-p` | 重复 |
|---|---:|---:|---:|---:|---:|
| 文件串行、单流 | 1 | 1 | 1 | 1 | 3 |
| 文件并行、单流 | 2 | 1 | 2 | 1 | 3 |
| 文件并行、单流 | 4 | 1 | 4 | 1 | 3 |
| 文件并行、单流 | 8 | 1 | 8 | 1 | 3 |

该矩阵主要观察文件级并行是否减少小文件元数据和控制路径造成的等待。

### 9.4 混合文件大小与组合并发对比

数据集使用 `tree_mixed_256MiB`，两方向分别运行：

| 配置 | `M` | `N` | 理论流预算 `M*N` | 重复 |
|---|---:|---:|---:|---:|
| 串行单流 | 1 | 1 | 1 | 3 |
| 单文件双流 | 1 | 2 | 2 | 3 |
| 两文件双流 | 2 | 2 | 4 | 3 |
| 四文件双流 | 4 | 2 | 8 | 3 |

这一组用于观察大文件和小文件混合时，文件级调度和单文件并发是否互相争用。结果必须包含实际最大数据流数和每类文件的完成分布，不能只看总吞吐。

### 9.5 CPNetFlux 调度器行为

该层只测试 CPNetFlux，不与 GridFTP 做一对一功能等价声明：

1. `scheduler=off`；
2. `scheduler=global, policy=fixed`；
3. `scheduler=global, policy=adaptive`。

建议在 `tree_mixed_256MiB` 上使用 `M=4`、最大 `N=8`，正式稳定性重复 5 次。每次运行必须保存调度器 CSV/JSONL sidecar，至少能观察：

- work item enqueue/dequeue；
- active item、active stream；
- queue depth；
- backpressure；
- raw/compressed candidate 决策；
- 压缩失败或收益不足时的 raw fallback；
- task weight 和 weighted round-robin 相关字段；
- completed 统计与 manifest 最终事实的对应关系。

自适应策略若因链路拥塞退回 raw 或降低并发，不应直接判定为失败；应结合决策事件、队列压力、最终正确性和稳定性解释。

### 9.6 Phase 4 IO 后端消融

该层使用 `single_1GiB`，两方向分别运行，建议固定 `N=8`：

| 后端 | queue depth | batch size | 重复 |
|---|---:|---:|---:|
| POSIX | 1 | 1 | 3 |
| POSIX | 4 | 4 | 3 |
| POSIX | 8 | 8 | 3 |
| io_uring | 1 | 1 | 3 |
| io_uring | 4 | 4 | 3 |
| io_uring | 8 | 8 | 3 |

io_uring 构建必须明确记录 `CPNETFLUX_ENABLE_IO_URING=ON`、liburing 版本和实际 backend；若环境不支持，结果标记为 `blocked_io_uring`，不能把 stub 当作真实 io_uring 结果。

如果网络耗时过长，该层可缩减为单方向或 `single_512MiB`，但必须在报告中明确写出缩减原因，并保持各组总大小一致。

### 9.7 断点恢复辅助矩阵

恢复不是主吞吐排名项，但必须作为 correctness 证据保留：

- 单文件传输在约 25% 至 50% 位置主动中断；
- CPNetFlux 使用现有 manifest、`verified_chunks` 和 resume 路径继续传输；
- GridFTP 使用其实际支持的 restart 参数，例如 `-t`/`-rst`，并记录具体命令；
- 恢复前后保存已确认字节、重新发送字节、最终 SHA-256；
- 不把两套恢复机制的内部实现声称为等价，只比较最终正确性和可观测行为。

## 10. 指标与产物

### 10.1 主结果 CSV

每个 case 每次重复至少写一行 CSV。建议字段如下：

```text
system
tool
tool_version
direction
dataset
file_count
logical_bytes
profile
file_parallelism
per_file_connections
total_stream_budget
max_observed_data_streams
chunk_or_block_size
buffer_size
checksum
compression
file_io_backend
queue_depth
batch_size
control_reuse
scheduler
scheduler_policy
repeat_index
elapsed_seconds
logical_goodput_mbps
wire_goodput_mbps
source_tree_hash
destination_tree_hash
hash_match
source_file_count
destination_file_count
exit_code
result
spread_percent
unstable
server_log
client_log
environment_json
socket_sample_jsonl
```

`logical_goodput_mbps` 定义为：

```text
logical_bytes * 8 / elapsed_seconds / 1,000,000
```

如果需要报告线上字节吞吐，单独使用 `wire_goodput_mbps`，并明确是否包含协议、manifest、checksum 或压缩影响。

### 10.2 调度器 JSONL

CPNetFlux scheduler 开启时，至少输出一条 JSONL 事件流。每条事件应包含：

```json
{
  "timestamp_ns": 0,
  "run_id": "...",
  "task_id": "...",
  "work_item_id": "...",
  "event": "dequeue",
  "active_work_items": 0,
  "active_streams": 0,
  "queue_depth": 0,
  "backpressure": false
}
```

字段可按现有实现扩展，但必须保持可追加、可解析，不能用自由格式文本替代关键状态。

### 10.3 环境与审计产物

每次正式矩阵至少保存：

- `environment.json`：主机、内核、CPU、内存、磁盘、构建类型、Git revision、工具版本；
- `command.jsonl`：实际执行的完整命令和退出码；
- CPNetFlux client/server 日志；
- GridFTP client/server 日志；
- socket 采样 JSONL；
- dataset manifest；
- 原始 CSV；
- 汇总 CSV；
- 失败 case 的 stderr/stdout；
- preflight JSON；
- 结果状态，例如 `pass`、`fail_correctness`、`fail_runtime`、`blocked_external_gridftp`、`blocked_io_uring`、`inconclusive_unstable`。

## 11. 统计与判定方法

每组至少报告：

- `min`、`median`、`max`；
- 重复间 spread；
- 选定正式组的 p95 或置信区间；
- 正确性通过率；
- 实际最大数据流数；
- 是否发生 backpressure；
- 是否发生 raw fallback 或 scheduler 降级。

定义：

```text
speedup_vs_1_stream = median_goodput_N / median_goodput_1
parallel_efficiency = median_goodput_N / (N * median_goodput_1)
```

目录测试中，`N` 应替换为实际比较的总流预算，或单独报告文件级和连接级效率，避免把 `M*N` 误认为实际吞吐流数。

判定规则：

- `spread_percent <= 20%` 才能称为该组“稳定”；
- 高并发相对单流中位数提升至少 10%，且高并发组稳定，才可称为“观察到正向扩展”；
- 若 spread 超过 20%、置信区间明显重叠或实际并发不足，结论应标记为 `inconclusive`；
- 若单流已达到测得链路上限的约 90%，应标记 `link_limited`，不把无增长解释为实现缺陷；
- 任意 hash、文件数、字节数或退出码失败，都不能进入性能排名；
- GridFTP preflight 未通过时，GridFTP 列只能为 `blocked_external_gridftp`；
- 10M 环境结果只能支持 correctness、backpressure、resume、指标完整性和稳定性结论。

## 12. 执行顺序

正式执行顺序固定为：

1. 检查深圳、上海工作树、构建类型和 Git revision；
2. 生成并校验确定性数据集；
3. 执行真实 GridFTP preflight；
4. 执行 CPNetFlux 本地或受控 smoke；
5. 执行 64MiB 双向远端 smoke；
6. 执行单文件原始并发矩阵；
7. 执行密集目录和混合目录矩阵；
8. 执行 CPNetFlux scheduler off/fixed/adaptive 矩阵；
9. 执行 Phase 4 IO 后端消融；
10. 执行断点恢复辅助矩阵；
11. 运行独立 hash/tree hash 校验；
12. 生成 CSV 汇总、JSONL 审计和结论报告；
13. 将代码、配置、结果和文档按既定流程同步到上海；
14. 在上海重新执行必要的 build、CTest、smoke 和选定矩阵，区分“已同步”与“已实跑”。

每次只运行一个 case。case 之间必须使用唯一目录和唯一端口，结束后只清理本次 case 创建的资源。

## 13. 建议新增的实验模块

实验工具应尽量复用已有 `tools/perf` 运行器和结果格式。若现有脚本无法表达真实 GridFTP 命令和审计信息，建议新增独立目录：

```text
tools/experiments/gridftp_compare/
    preflight.py
    dataset.py
    runner.py
    analyze.py
    schemas.py
    test_gridftp_compare.py
```

建议职责：

- `preflight.py`：检查 GridFTP 工具、认证、端口、服务进程和最小 smoke；
- `dataset.py`：生成确定性文件和 manifest；
- `runner.py`：统一运行 CPNetFlux、GridFTP、单文件、目录、双向和 resume case；
- `analyze.py`：计算 goodput、speedup、efficiency、spread、稳定性和 blocked 状态；
- `schemas.py`：集中定义 CSV、JSONL、environment 和 result status；
- `test_gridftp_compare.py`：测试命令生成、参数映射、状态判定和结果解析，不要求每个单元测试都启动远程传输。

若必须扩展已有脚本，应保持原有调用兼容，不改变已有 Phase 1 至 Phase 4 验收命令的含义。

## 14. 与现有 CPNetFlux 路径的集成要求

实验 harness 必须通过现有入口调用 CPNetFlux：

- 单文件上传/下载使用现有 `cpnetflux-file-client`、`cpnetflux-file-download-client` 或对应 CLI；
- 目录上传/下载使用现有 `cpnetflux-tree-upload-client`、`cpnetflux-tree-download-client`；
- 服务端使用项目现有 `cpnetflux-gridftp-server`；
- 数据面保持 framed STOR/RETR；
- 完成判断读取现有 manifest、`verified_chunks` 和 tree manifest；
- 日志和指标通过已有输出目录或明确的 sidecar 目录保存；
- scheduler 开启时只作为运行控制和观测来源，不能替代最终 manifest 验证。

raw CPNetFlux 对比必须显式关闭 scheduler 和压缩。scheduler 实验可另开一组 case，不能把 scheduler 的内部调度开销和 raw GridFTP 黑盒结果混在同一条结论中。

## 15. 测试要求

### 15.1 工具自身测试

至少覆盖：

- CPNetFlux 单文件和目录命令生成；
- GridFTP `-p`、`-cc`、`-r`、`-rst` 参数生成；
- `M`、`N`、总流预算和实际流数的字段解析；
- 目录路径、端口和 transfer ID 隔离；
- CSV schema 完整性；
- JSONL 每行可独立解析；
- hash mismatch、exit code failure 和 timeout 的状态判定；
- GridFTP 不可用时输出 `blocked_external_gridftp`；
- io_uring 未启用时不误报真实 io_uring；
- 重复统计、spread 和 unstable 判定；
- 不同方向的结果不会混组。

### 15.2 项目回归

实验工具完成后，深圳服务器至少执行：

```text
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

如使用真实 io_uring 构建，还需在对应 `build-io-uring-real` 目录执行同等 build/CTest，并确认真实 backend。

必须执行：

- CPNetFlux 本地 file smoke；
- CPNetFlux 本地 tree smoke；
- 远端 64MiB 双向 smoke；
- 至少一组 raw `N=1` 与 `N=4`；
- 至少一组 tree `M=1,N=1` 与 `M=4,N=2`；
- 至少一组 resume；
- 正确性 hash/tree hash 校验。

### 15.3 失败处理

任何失败 case 都必须保留：

- 完整命令；
- 客户端和服务端 stdout/stderr；
- 退出码；
- 当前源/目标路径；
- transfer ID；
- 端口和认证错误；
- 已传输字节和 manifest 状态。

不得只记录“失败”二字，也不得通过删除失败日志后重跑来替换审计记录。

## 16. 云服务器测试边界

深圳是主要实现和测试端，上海用于同步和独立验证。云服务器测试必须遵守：

- 明确记录公网 10M 链路限制；
- 不运行会影响其他服务的无限并发或无限重试；
- 控制单次数据量、磁盘占用和并发上限；
- 只使用实验专用目录；
- 不修改不属于本 case 的配置和文件；
- 不把深圳结果自动写成上海已验证；
- 同步后重新记录上海的 build、CTest、smoke 和矩阵状态；
- 认证材料缺失时将外部 GridFTP 标记 blocked；
- 防火墙或 passive port 不通时保留错误并停止扩展矩阵；
- 不把公网 10M 结果外推为高性能网络或 100G 结果。

## 17. 不得破坏的语义

实验 harness、CLI 参数扩展和结果解析不得破坏以下既有语义：

1. framed STOR/RETR 的协议边界和错误处理；
2. manifest 的逻辑文件和传输状态记录；
3. `verified_chunks` 对已验证 chunk 的事实含义；
4. 断点续传时已验证数据不可被错误地标记为未验证或已完成；
5. tree manifest 对目录文件列表、大小和 hash 的事实含义；
6. 传输成功必须同时满足进程成功和独立内容校验；
7. 压缩失败、收益不足或 CPU 压力高时必须能够 raw fallback；
8. 压缩候选不能阻塞 raw path；
9. backpressure 不能通过无界队列掩盖；
10. scheduler 的运行状态不能替代 manifest/verified_chunks；
11. io_uring 不可用时必须有清晰 fallback 或 blocked 状态；
12. 现有 Phase 1 至 Phase 4 CLI 和测试命令保持兼容。

## 18. 预期交付物

实验实现阶段应交付：

1. 本设计文档的实现状态更新；
2. 实验 harness 和单元测试；
3. Shenzhen 运行说明和实际命令；
4. GridFTP preflight JSON；
5. 确定性数据集 manifest；
6. CPNetFlux 和 GridFTP 原始 CSV；
7. scheduler JSONL；
8. socket、环境和命令审计文件；
9. client/server 原始日志；
10. 汇总统计 CSV；
11. correctness、backpressure、resume 和稳定性报告；
12. CPNetFlux 与 GridFTP 的对比结论；
13. 上海同步记录及上海实际复验状态；
14. 明确列出的 blocked、link-limited、inconclusive case；
15. 不得把未运行的项目写成通过，不得把历史结果冒充本次结果。

## 19. 下一阶段给深圳 Codex 的实现边界

深圳 Codex 接到实现任务后，应按以下顺序工作：

1. 先阅读本文档和第 3 节列出的所有文档；
2. 检查当前深圳工作树、构建产物和现有 `tools/perf` 工具；
3. 先实现 preflight、dataset manifest、统一 result schema 和 dry-run 命令审计；
4. 以真实 CPNetFlux raw path 完成单文件和目录 smoke；
5. 接入真实 `globus-url-copy`/`globus-gridftp-server`，若 preflight 失败则实现 blocked 记录；
6. 实现单文件、目录、双向、resume 和 IO 消融矩阵；
7. 实现独立分析和稳定性判定；
8. 执行深圳 build、CTest、smoke 和受控矩阵；
9. 保留所有日志和原始证据；
10. 按本文档的交付物清单报告结果，并指导同步到上海。

实现阶段不得修改 Global Scheduler 的独立核心接口来迁就实验 harness。若发现实验需要新的观测字段，应优先使用 sidecar 或最小兼容扩展，并在交付报告中说明影响范围。
