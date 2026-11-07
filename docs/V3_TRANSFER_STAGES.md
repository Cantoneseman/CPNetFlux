# V3 三阶段执行与缓冲预算

更新时间：2026-11-07。实现与结果见 [本轮结果](tasks/2026-11-07-v3-stage-decoupling-01-result.md)。

## 执行和所有权

源端 producer 读文件并计算 CRC，使用有界 SPSC 队列把已准备的 payload 交给网络 lane。接收 lane 解帧、验证帧 CRC，再交给有界队列后的 writer。读和写的活跃操作数分别受整个 V3 batch 共享的准入器约束，网络并发由 connections/lane 数控制。

DynamicFileQueue 共享源端控制器，PersistentTransferContext 共享接收控制器。只有已有数据可处理时才申请许可；源端 read+CRC 和接收端 payload write/certification 执行完就释放，在等待网络或 payload 队列前释放。读配额控制活跃 read+CRC；显式写上限小于lane数且选择pool时，接收上下文共享固定数量writer工作线程，只接收已经就绪的存储/认证操作，现有每lane writer等待其完成。池中没有网络/空payload队列等待；排队加执行中的job总量最多为lane数，借用的payload在run返回前结束使用/销毁捕获，不新增payload副本。发送端不创建空闲writer池，单lane或写上限达到lane数跳过派发。它没有实现独立的源读线程池，也没有消除每lane原有线程。admission模式保留为同版本诊断。后台 checkpoint、最终 full 复读校验、提交和元数据 I/O 继续遵循原事务实现，不属于 payload 写入并发上限；不能把该上限称为所有存储操作的总并发上限。

源读/诊断admission的等待者各自持有CV，仅唤醒队首。为了减少每帧强制换线程，空闲容量最多允许32次就绪操作绕过正在休眠的队首，随后必须让队首执行。等待者之间按先后顺序取得容量；这是有界插队而非严格逐帧 FIFO。移除超时等待者时会唤醒可执行的后继；RAII 许可在错误、异常、移动赋值和正常返回时释放，许可持有共享状态。许可不跨 payload 队列/网络等待，不新增 payload 副本。

## 配置

环境变量必须在对应服务端/客户端进程启动前设置；两端各使用本地配置，不通过协议自动协商。一个 batch 创建控制器时冻结参数，过程中不能改变 lane/depth 形状。

| 变量 | 范围/默认值 | 含义 |
|---|---|---|
| CPNETFLUX_STAGE_READ_CONCURRENCY | 0..64，默认0 | 同一 batch 的源 payload read+CRC 活跃并发上限；0沿用每 lane 执行 |
| CPNETFLUX_STAGE_WRITE_CONCURRENCY | 0..64，默认0 | 同一 batch 的接收 payload 写入/认证活跃并发上限；0沿用每 lane 执行 |
| CPNETFLUX_STAGE_WRITE_EXECUTOR | pool/admission，默认pool | 仅在非零写上限小于lane数时启用；pool为共享就绪写执行器，admission为原线程准入控制 |
| CPNETFLUX_STAGE_BUFFER_BYTES | 默认67108864，最多268435456 | 本端每 batch 的 payload 所有权预算，单位 bytes；必须容纳保守上界 |

每 lane 最多一个源 producer 或接收 writer；请求上限达到/超过 lane 数时无需额外准入锁，JSON 仍报告请求值。显式0也不进入准入锁。线程数、网络 lane 数和活跃 I/O 并发不是同一指标。

默认队列深度仍为2，不通过加深队列制造收益。预算校验公式：

`payload_ownership_bound_bytes = network_lanes * (pipeline_depth + 6) * 65536`

保守上界覆盖队列、生产/消费、复用、解码/压缩中可能同时持有的 payload。4 lane/depth2 为2097152 bytes；整个 batch 的预算不足会在 DATA/FILE_BEGIN 前明确拒绝，而非先让不同 lane 各占预算后互相等待。它不预分配64MiB，且不包括传输池/原生 RDMA 注册池、元数据/CRC统计、线程栈和 tmpfs 数据；后者必须独立用 RSS、MemAvailable、VmSwap 和 tmpfs 计量。预算不是整台服务器所有并行任务的总内存上限。

## 观测和验收

每份 file timing 的 stage_execution 包含读/写请求并发、network_lanes、是否实际需要准入、budget/bound、read/write_calls、等待秒数和本端write_executor/write_pool_workers/write_pool_cpu_seconds。pool按就绪job FIFO执行，admission允许最多32次就绪插队。等待lane/writer线程CPU不含池内执行CPU，必须另加pool CPU；双端进程CPU自动包含这些线程，作为主要比较指标。累计等待时间跨线程重叠，不能从 task wall 中直接相减。默认完整性、durability、checkpoint、FILE_END/FILE_RESULT/DirectoryEnd、V3 attempt/epoch/lease 语义不变；TCP 经数据网卡不等于原生 verbs。

配置收益必须在相同提交、配置、版本和二进制摘要下比较，分别报告 task rate、文件阶段、双端进程 CPU、内存与恢复。原严格 FIFO/广播和过小写并发导致的负结果保留在结果记录；不得因某个 syscall/wait 指标下降而推广配置。小规模 tmpfs、commit_sync=none 的数字不代表持久化或100Gbps能力。
