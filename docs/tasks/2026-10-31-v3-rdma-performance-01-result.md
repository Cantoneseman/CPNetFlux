# V3 RDMA 通道性能优化开发结果

日期：2026-10-31
任务：`V3-RDMA-PERFORMANCE-01`
权威目录：R770-1 `/home/Su/projects/CPNetFlux`
对端：R770-2 `/home/Su/projects/CPNetFlux`
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`

## 解决的实际问题

之前 CPNetFlux 使用 RDMA 时，网卡本身能跑到约 92.5 Gbps，但应用通道只有约 1.6 Gbps。原因不是 RDMA 网卡只有这么快，而是接收端的 WQE/完成处理跟不上：接收槽用完后，发送端必须等对端重新投递缓冲区，RDMA 反压把等待时间混进了软件吞吐。

本轮把接收窗口扩大并保持硬件队列持续投递，然后把等待、完成和队列状态写进正式摘要。这样可以把“RDMA 路没有铺好”和“CPNetFlux 自身处理慢”分开。当前大块 direct bench 的上限约 29.7 Gbps，服务端传输窗口内 CPU 约占满一个进程核；这部分才是后续应继续拆分的 CPNetFlux 接收处理成本。

## 代码改动

- `include/cpnetflux/core/io/rdma_data_socket.h`
  - 发送 QP 深度 16。
  - 接收 QP 深度 2048，每连接增加 32 个备用接收槽。
  - 增加 ready 队列高水位统计。
- `src/core/io/rdma_data_socket.cpp`
  - 发送/接收使用连续注册内存池。
  - CQ 先短自旋 64 次，再使用 completion event。
  - 完成接收后立即用备用槽补回 RQ；应用消费完的槽位回到备用队列。
  - 读路径批量清空可用 CQ 完成，记录实际补投递次数。
- `tools/perf/rdma_socket_bench.cpp`
  - 使用 V3 DATA frame，输出逻辑字节、线上字节、帧数、WQE、完成等待、进程 CPU、协商 QP/MTU/RNR 和接收槽指标。
- `src/core/io/persistent_tree_transfer.cpp`、`src/core/io/tree_transfer_client.cpp`
  - 将接收队列和完成统计纳入服务端 timing 与客户端 JSON 摘要。
- 相关单元测试、CMake 目标和本任务文档同步更新。

计时约定：吞吐和 elapsed 是传输窗口的 steady-clock 墙钟时间；`process_cpu_seconds` 是基准进程的 `std::clock` 累计 CPU 时间，不标作单线程或专用 CPU 时间；完成等待是各次等待时间的累计，可能与传输墙钟和其他线程时间重叠，不能与其他嵌套计时直接相加。

## 实测证据

测试条件：R770-1 `192.168.100.1`、R770-2 `192.168.100.2`，HCA `mlx5_0`，RoCE IP 路径，1 MiB + 4 KiB 槽，发送 QD16，单连接，2 GiB 有效数据。所有传输结果均通过 payload 校验。

| 测试 | 结果 |
|---|---:|
| 原生 `ib_send_bw`，1 MiB、QD16/RX32 | 约 11,037 MiB/s，约 92.5 Gbps |
| CPNetFlux direct bench，RQ512（备用槽前） | 约 4.69 Gbps；发送完成等待约 3.37 s，接收完成等待约 3.11 s |
| CPNetFlux direct bench，RQ1024（批量 CQ） | 约 9.82 Gbps；发送完成等待约 1.54 s，接收完成等待约 1.18 s |
| CPNetFlux direct bench，RQ2048，R770-1 -> R770-2 | 29.72 Gbps；接收等待约 3.5 微秒 |
| CPNetFlux direct bench，RQ2048，R770-2 -> R770-1 | 29.76 Gbps；接收等待约 0.176 毫秒 |
| V3 实际上传，256 MiB | 通过，约 0.908 Gbps，摘要 `data_transport=rdma` |
| V3 实际下载，256 MiB | 通过，约 0.926 Gbps，摘要 `data_transport=rdma` |

RQ2048 方向的服务端摘要显示：协商接收深度 2048、路径 MTU 1024、`receive_reposts=2043`、接收完成等待接近 0；两端均报告 `result=pass`。V3 上传和下载的源/目标 SHA-256 均为：

`b2555d6ac27dab343fb7ab639c481dfb4c8d135b6d7dfa1ccd952291952385b9`

测试 payload 和摘要保留在两台服务器的 `/tmp/cpnetflux-rdma-v3-final-r1`、`/tmp/cpnetflux-rdma-v3-final-r2`，没有写入仓库。

## 验证

- R770-1：RDMA-enabled 全量 CTest 为 479/481 通过。#434 和 #442 因环境变量 `CPNETFLUX_TEST_TOKEN` 未设置失败；其余测试通过。
- R770-2：RDMA-enabled 全量 CTest 为 478/481 通过。#434 和 #442 同样因 `CPNETFLUX_TEST_TOKEN` 未设置失败；#451 因 smoke 测试读取已退出进程 `/proc/<pid>/fd` 的竞态失败，其余测试通过。
- R770-1：RDMA-disabled 配置重新构建成功，相关 37 项传输和选项测试 37/37 通过。
- R770-1/R770-2 反向 2 GiB direct bench 及 V3 上传/下载均通过。
- 测试服务已停止；R770-2 原有生产服务未修改。

## 边界和剩余问题

- RQ2048 每连接注册接收内存约 2,155,872,256 bytes（约 2.01 GiB），另有约 16 MiB 发送池；多连接时内存按连接数增加，不能直接作为所有部署的默认值。
- CPNetFlux SEND/RECV 仍有应用层复制、V3 帧解析和校验，direct bench 约 29.7 Gbps，尚未达到网卡 92.5 Gbps；这正是下一轮软件瓶颈分析的基线。
- V3 数据面仍未启用 TLS；当前实验只适用于可信、隔离的 RDMA 网络。
- 本轮未实现 RDMA READ/WRITE、零拷贝、kTLS、动态接收窗口或同时双向并发。未把 RNR 参数字段直接解释为 CPU 成本。

## 下一步

在 RQ2048 基线下，分开测量接收复制、帧解析/校验、文件写入和 checkpoint 的 CPU/墙钟成本；再设计按文件大小、连接数和处理速率动态选择接收窗口的方案，降低约 2 GiB/连接的内存代价。任何下一步吞吐优化都必须先用 direct bench 证明没有重新引入接收队列反压。
