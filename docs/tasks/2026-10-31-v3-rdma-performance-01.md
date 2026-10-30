# V3 RDMA 通道性能优化

任务：`V3-RDMA-PERFORMANCE-01`  
权威目录：R770-1 `/home/Su/projects/CPNetFlux`  
对端：R770-2 `/home/Su/projects/CPNetFlux`

## 目标

移除 CPNetFlux RDMA 适配层的串行发送限制和每帧临时拼接开销，让 V3 应用测试更接近 RDMA 网络本身的可用能力，并输出足以判断 RDMA 通道是否仍是瓶颈的运行指标。

## 本阶段改动

- 将每连接单发送缓冲改为有界注册缓冲池，最多允许 32 个 SEND 同时在途。
- 固定每个槽 128 KiB，发送队列深度 32、接收队列深度 128；每连接发送窗口 4 MiB、预投递接收空间 16 MiB。接收深度大于发送窗口，避免应用重新投递接收缓冲稍慢时触发 RNR。
- 批量轮询 SEND completion queue，移除每帧等待一个 completion 的行为。
- 自旋轮询 CQ，避免固定 50 微秒 sleep 带来的延迟底限；仅周期性让出 CPU。
- V3 frame 的 header、checksum prefix、payload 直接复制进注册发送槽，避免先分配和拼接临时 vector。
- 在客户端 JSON 摘要记录发送 WQE 数、CQ poll/batch、完成等待、最大实际在途数。
- 在摘要中同时记录配置的发送/接收队列深度；双机测试前后读取 `out_of_buffer` 与 `rnr_nak_retry_err` 设备计数器。
- 对同一数据、同连接数和 RAM-backed 临时路径做 R770 双机 RDMA/TCP 实测，核对 SHA-256。

## 非目标

- 不改变 V3 frame 格式、64 KiB 上限、文件语义、checkpoint 和恢复协议。
- 不改 TCP 数据通道、操作系统网络配置或网卡固件。
- 本阶段不承诺零拷贝、RDMA READ/WRITE、kTLS 或 data TLS；不把 `ib_write_bw` 直接当作应用吞吐。
- 不在没有性能证据时宣称所有 RDMA 瓶颈已经清除。若实测仍受 RDMA 适配层限制，应继续下一阶段优化。

## 验收标准

1. RDMA-enabled 和 RDMA-disabled 都能构建，现有全量 CTest 通过。
2. R770-1、R770-2 代码提交一致，均完成相应构建后再互传。
3. 大文件双机 RDMA 上传和下载通过，摘要 `data_transport=rdma`，哈希一致。
4. 实际摘要 `max_send_inflight` 大于 1，且提供等待时间、完成批次数和应用吞吐。
5. 与 TCP 同条件进行对照；报告链路级基准与应用吞吐的差别以及剩余瓶颈。
6. 本阶段代码和结果推送到已核实的 CPNetFlux GitHub remote，并回读远端 SHA。
