# V3 原生 RDMA 数据通道决策

日期：2026-10-30

CPNetFlux V3 增加显式的 `tcp|rdma` 数据通道选择。首个 RDMA 实现使用 `rdma_cm` 建立 RC queue pair，并以 verbs SEND/RECV 搬运现有 V3 framed 字节流。V3 控制协商回显双方选定的数据通道，服务端按已协商值建立 TCP 或 RDMA EPSV 被动监听。未协商 RDMA 时保持现有 TCP 路径。

首版不采用 RDMA READ/WRITE，因为现有 V3 API 是有序字节流，不是远端内存注册协议。SEND/RECV 能复用现有 framing 和 checkpoint 实现。第二阶段将发送队列设为 32 个 128 KiB 槽、接收队列设为 128 个 128 KiB 槽，分别提供 4 MiB 发送窗口与 16 MiB 预投递接收空间，允许 32 个 SEND 在途并批量轮询 CQ；帧头、校验前缀和 payload 直接复制到注册发送槽，避免额外临时拼接。第一轮 512 MiB 实测发现接收深度等于发送窗口仍触发了 24 次 `out_of_buffer`/`rnr_nak_retry_err`，因此加大接收队列以留出接收端重新投递缓冲的余量。该阶段仍使用 SEND/RECV 和 V3 64 KiB 帧协议，不等于已清除网卡/交换网络全部限制；需由双机对照和设备计数器复核。第一阶段硬件结果见 `docs/tasks/2026-10-30-v3-rdma-transport-01-result.md`，第二阶段范围见 `docs/tasks/2026-10-31-v3-rdma-performance-01.md`。

RDMA 数据面暂不提供 TLS。控制面仍可使用 TLS 和 token 认证；部署时必须使用可信、隔离的 RDMA 网络。RDMA 连接失败时明确失败，不自动切回 TCP。TCP 仍是默认值和独立可选路径。

硬件基础测试 `ib_write_bw` 只用于确认 R770 RoCE 链路可互通和记录对照值，不代表 CPNetFlux 应用吞吐。应用性能与生产 readiness 必须由单独的 V3 实测证明。
