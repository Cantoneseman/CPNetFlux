# V3 原生 RDMA 数据通道决策

日期：2026-10-30

CPNetFlux V3 增加显式的 `tcp|rdma` 数据通道选择。首个 RDMA 实现使用 `rdma_cm` 建立 RC queue pair，并以 verbs SEND/RECV 搬运现有 V3 framed 字节流。V3 控制协商回显双方选定的数据通道，服务端按已协商值建立 TCP 或 RDMA EPSV 被动监听。未协商 RDMA 时保持现有 TCP 路径。

首版不采用 RDMA READ/WRITE，因为现有 V3 API 是有序字节流，不是远端内存注册协议。SEND/RECV 能复用现有 framing 和 checkpoint 实现。初版每个 QP 只预投递一个接收缓冲区，32 MiB 上传耗时 36.33 秒；硬件实测暴露接收供给不足后，改为 16 个各 1 MiB 的预投递接收槽。当前每 QP 另有一个 1 MiB 发送缓冲区，发送仍一次一个 in-flight 消息并轮询完成，注册内存约 17 MiB/连接。接收队列改善后 32 MiB 上传/下载成功，但应用吞吐仍远低于链路基准；下一步需测量 RNR/完成等待、队列深度和并发窗口，再评估事件式 CQ、注册缓冲池、批量发送及零拷贝数据模型。本轮硬件结果见 `docs/tasks/2026-10-30-v3-rdma-transport-01-result.md`。

RDMA 数据面暂不提供 TLS。控制面仍可使用 TLS 和 token 认证；部署时必须使用可信、隔离的 RDMA 网络。RDMA 连接失败时明确失败，不自动切回 TCP。TCP 仍是默认值和独立可选路径。

硬件基础测试 `ib_write_bw` 只用于确认 R770 RoCE 链路可互通和记录对照值，不代表 CPNetFlux 应用吞吐。应用性能与生产 readiness 必须由单独的 V3 实测证明。
