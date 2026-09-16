# 2026-09-17 目录数据面 profiling 任务边界

状态：批准作为路线重置后的第一个 profiling 阶段。本文只定义测量契约和实验边界，不批准任何性能优化实现。

## 背景

控制连接复用重测已经证明：`control_reuse=worker` 能降低目录传输的控制连接开销，但不能解释剩余的 33–49% 吞吐差距。单文件差距只有约 8–12%，因此下一步必须把目录路径拆成可归因的阶段，而不是继续调整 scheduler、compression 或连接数。

输入代码基线为 `3b0820d` 的 CPNetFlux 实现；文档提交不改变实验代码。实验运行必须使用固定提交或由该提交生成的归档构建，并记录工作树状态与每个二进制的 SHA-256。

## 阶段目标

回答一个问题：目录传输相对 GridFTP 的主要时间差来自控制命令/连接生命周期、数据通道启动、有效数据读写，还是 manifest/finalize 路径？

本阶段只增加可观测性和证据，不改变传输协议、调度策略、压缩策略、checksum 语义、resume 语义或默认 IO backend。

## 必须测量的阶段

CPNetFlux tree upload/download 的每个文件和整个 run 都要记录以下单调时钟区间，单位为秒：

1. `scan_plan`：目录扫描、manifest 加载和任务规划；
2. `control_acquire`：获取或建立 worker 控制连接，包括复用命中；
3. `control_prepare`：SIZE/MDTM、EPSV、REST、STOR/RETR 等文件级控制命令；
4. `data_connect`：数据通道建立和握手；
5. `first_payload`：数据通道建立到首个有效 payload；
6. `payload_io`：首个 payload 到最后一个有效 payload 的数据读写/发送；
7. `transfer_complete_wait`：最后一个 payload 到服务端完成状态确认；
8. `manifest_finalize`：manifest 更新、flush、mtime/rename/finalize；
9. `tree_verify`：实验 runner 进行的独立 tree hash，不计入传输客户端内部耗时。

`scan_plan`、`tree_verify` 属于 run 级阶段；其余阶段至少要有 file-level 数据，并在 run summary 中提供 sum、median、p95、max 和文件计数。并发传输下区间可以重叠，不能把各阶段简单相加当成 wall time；summary 必须同时保留 wall elapsed。

## 代表性矩阵

所有 CPNetFlux case 固定使用 `control_reuse=worker`、`scheduler=off`、`compression=off`、`checksum=none`、POSIX backend。GridFTP 只作为端到端参考，不要求伪造同名内部阶段。

- 方向：`local_to_remote`、`remote_to_local`；
- 单文件：`single_256MiB`，connections 为 1、8；
- dense 目录：`tree_dense_128MiB`，file parallelism 为 1、4、8，per-file connections 为 1；
- mixed 目录：`tree_mixed_256MiB`，至少覆盖 `(file_parallelism, connections) = (1,1)、(2,2)、(4,2)`；
- 系统：CPNetFlux 和真实 GridFTP；
- 重复：每个组合 3 次，使用相同数据 seed 和独立 case 目录。

资源策略沿用阶段 0：运行前本地/远端至少保留 10 GiB；每个 case 后删除可再生 payload；失败时保留日志、summary、hash、命令和最小诊断包。

## 验收条件

1. 所有未 blocked 的 CPNetFlux case 退出码为 0，独立 file/tree hash 匹配；transfer status、integrity status、evidence status 和 wire accounting 分列。
2. 每个成功 case 有固定提交、二进制 hash、完整命令、环境 JSON、阶段 summary 和清理结果；缺任一项不得进入性能结论。
3. 阶段时间均为非负，`first_payload <= payload_io` 的边界关系可解释；每个 run 的 wall elapsed、logical bytes 和 throughput 与既有 summary 一致。
4. 至少 90% 的成功文件具有完整阶段记录；缺失阶段只能标为 evidence partial，不能改写 transfer result。
5. 报告按方向、数据集和并发组合给出 CPNetFlux/GridFTP 端到端差距，并指出一个主要候选阶段和证据强度；不能把阶段相关性写成已证实因果。
6. 既有 CMake/CTest、tree smoke、resume smoke 和阶段 0 Python 测试保持通过；新增 schema/解析测试覆盖空阶段、并发重叠和失败 case。

## 结果解释门槛

- `control_acquire` 或 `control_prepare` 占比高：优先评估控制命令批量化、worker 生命周期和任务提交路径。
- `data_connect` 或 `first_payload` 占比高：优先评估数据通道建立、端口协商和首块调度。
- `payload_io` 占比高且单文件也相近：优先评估 socket/file IO、buffer 和并行数据流；不先改 scheduler。
- `manifest_finalize` 或 `tree_verify` 占比高：先区分产品传输耗时与实验验证耗时，再评估 manifest flush/finalize。
- 阶段证据不完整或资源被 blocked：只修实验治理，不进入性能优化。

## 非目标

- 不在本阶段引入 hot-path compression、global scheduler 策略、io_uring、QUIC、FEC、GSI 或生产认证。
- 不把深圳/上海约 10M 公网结果外推为 50G/100G readiness。
- 不修改 `/root/projects/GridFlux-Beta` 正式源码树，不接触 `/root/projects/CPSS(DCC)`。
- 不用 scp/rsync/SSH tunnel 代替 GridFTP 或 CPNetFlux 传输对照。

## 交接

实验与证据负责人根据本文生成固定构建和矩阵任务；核心实现负责人只有在阶段字段和 JSON/CSV schema 被确认后才实现 instrumentation；测试与质量负责人先验证阶段记录不会改变 transfer result；架构负责人依据 profiling 报告更新下一份决策记录。

