# V3 RDMA 通道性能优化

任务：`V3-RDMA-PERFORMANCE-01`  
日期：2026-10-31
权威目录：R770-1 `/home/Su/projects/CPNetFlux`  
对端：R770-2 `/home/Su/projects/CPNetFlux`
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`

## 本轮目标

把 RDMA 网络路径和 CPNetFlux 应用处理路径分开测量。先消除接收队列耗尽、CQ 忙等造成的人工反压，再用可复核的 V3 统计判断剩余瓶颈是否在帧复制、解析、校验和文件处理。

## 本轮范围

- 使用已注册的发送缓冲池，发送队列深度 16，每槽 1 MiB + 4 KiB，并保持最多 16 个 SEND 在途。
- 使用接收队列深度 2048，每连接预留 32 个备用槽；完成一个接收 WQE 后立即用备用槽补回硬件队列，应用消费完再归还。
- CQ 采用 64 次短自旋后切换到 completion event，避免无界忙轮询；读路径批量清空可用 CQ 完成。
- 记录协商后的发送/接收队列深度、路径 MTU、RNR 参数、WQE/完成数、完成等待时间、接收槽回收和 ready 队列高水位。
- 增加 `tools/perf/rdma_socket_bench`，用真实 V3 DATA frame 做不落盘的 2 GiB 双向对照，并保留进程 CPU 时间、逻辑/线上字节和帧数。
- 将 RDMA 统计纳入 V3 客户端摘要和服务端持久化 timing，完成 256 MiB V3 上传、下载和 SHA-256 核验。

## 非目标

- 不改变 V3 frame、checkpoint、manifest 或控制协议。
- 不实现 RDMA READ/WRITE、零拷贝、kTLS、数据面 TLS、GPU Direct 或多物理链路调度。
- 不把 `ib_send_bw`/其他 perftest 结果当作 CPNetFlux 应用吞吐，也不把 `SSL_read`/`SSL_write` 墙钟时间解释成 TLS CPU 时间。
- 不把当前 RQ2048 直接作为多连接生产默认；它是本轮大块性能验收基线，内存成本必须随连接数评估。

## 验收标准

1. R770-1、R770-2 使用同一源码和构建产物；改动先在 R770-1 构建测试，再同步 R770-2 构建测试。
2. RDMA-enabled 全量 CTest 通过；RDMA-disabled 配置至少完成配置、构建和传输选项门禁。
3. 原生 RDMA 链路基准仍约 92.5 Gbps；CPNetFlux direct bench 在 RQ2048 下双向 2 GiB 传输通过，且接收完成等待接近 0。
4. V3 实际上传和下载摘要均为 `result=pass`、`data_transport=rdma`、`actual_transport=v3`，文件 SHA-256 一致。
5. 结果文档记录计时单位、统计范围、嵌套计时重叠关系、内存成本和剩余软件瓶颈。
6. 只提交本轮相关源码、测试、基准和任务证据；推送 GitHub 并回读远端 SHA。

## 后续入口

下一阶段应在 RQ2048 基线下拆分接收端 CPU 成本：帧头/帧体复制、校验、文件写入和 checkpoint。若需要降低每连接约 2.16 GiB 的注册内存，再单独设计按传输大小或处理速率动态调整接收窗口的方案，并用同一 direct bench 验证，不能用小 RQ 的吞吐直接代表软件性能。
