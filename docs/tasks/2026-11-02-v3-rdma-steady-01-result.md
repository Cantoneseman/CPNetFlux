# V3-RDMA-STEADY-01：长传输停顿定位与修复结果

日期：2026-11-02。唯一源码工作区：R770-1 `/home/Su/projects/CPNetFlux`；R770-2 仅同步同一提交、同配置构建和运行。实验根目录：`/home/Su/experiments/CPNetFlux/2026-11-02-rdma-steady-01`。Windows 交付副本：`D:\读研\干活\算力网项目——存储优化\CPNetFlux\实验报告\2026-11-02-rdma-steady-01`。

## 结论

长传输掉速的直接原因已定位到 **接收队列短时无可用 WQE 时触发 RNR，连接管理器默认的最小 RNR 等待编码为 0，而 0 表示 655 ms，不表示立即重试**。发送端会等待对端重新提供接收资源；短暂的接收队列空档因此扩大成数百毫秒级的发送完成停顿。连续传输中反复发生时，吞吐下降且波动放大。

只改变这一个参数：连接建立后，在主动和被动两端将 QP `min_rnr_timer` 设为 IBTA 编码 12（0.64 ms 的 RNR 重试间隔），设置失败则连接失败，不静默回退。没有扩大 RQ/SRQ、改变发送窗口、补投递逻辑、MTU、NUMA、协议、内存池或网络配置。已有 [rdma_accept 手册](https://raw.githubusercontent.com/linux-rdma/rdma-core/master/librdmacm/man/rdma_accept.3)说明 CM 默认编码 0 对应 655 ms；[ibv_modify_qp 手册](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_modify_qp.3)说明连接态 QP 属性更新接口。

复测表明，RNR/`out_of_buffer` 计数仍会增加，说明接收队列的短时空档仍存在；改动缩短了它造成的停顿，没有消灭队列空档。原来 3 GiB / 8 GiB / 64 GiB 的长等待在修复后没有复现，帧和内容检查通过。不能把硬件计数增加直接等同于任务失败：本轮有效数据期没有工作完成错误，bench 全部返回 pass。

## 固定环境与输入

- 基线提交 `92382ad7d7a41f4dc22d094850726267be8389b4`：仅增加阶段计时、CQ 等待与诊断观测、硬件计数和关闭屏障，不改变数据面。
- 代码修复提交 `300805c299fd5bca0b4a8d8107159e79556d5214`：只调整两端 `min_rnr_timer` 并增加有效 QP 属性回归断言；该代码提交已推送，GitHub `refs/heads/codex/V3-GRIDFTP-OPTIMIZED-RERUN-01` 回读 SHA 一致。R770-1/R770-2 均基于此代码提交完成同配置构建、测试和性能复测；本结果文档及路线记录随后作为单独文档提交归档。
- 两机均为 RelWithDebInfo、GCC 15.2、`-O2 -g`、Ninja；RDMA/TLS/ISA-L/tests 开启，desktop/io_uring/cross-domain tests 关闭。设备 `mlx5_0`、端口 1、RoCEv2 GID index 3，固定直连地址 `.1`→`.2`，显式绑定 `192.168.100.1` / `192.168.100.2`。未改 MTU 或 CPU/NUMA 设置。
- R770-1/R770-2 同配置构建产物 SHA-256：`cpnetflux-rdma-socket-bench` `6238a74a013cff05c848c02aea0e10f0f135bb033daff886666113114eeba2ff`；`cpnetflux-server` `e17c42febe697bd1de045c03ce7786630443ce27219a9b22033ee0c203039d09`；`cpnetflux-gridftp-server` `7238b19e0505b933994f29dde2e301079008d9941ad1d65f0f25fe1e3e775604`；`cpnetflux-tree-upload-client` `146b5379d7c088bc4c72f2c5bb97a6a74f38b58ecf8b4f5fedfc486cc462e252`；`cpnetflux-tree-download-client` `30882eb215e4ae1f2bece348e4cf48ca6c73d0ed745f8065c3b81f917500a35a`。本轮内存通道测试实际执行的是 `cpnetflux-rdma-socket-bench`。
- 本轮 direct bench 在内存中发送带 CPNetFlux 帧头的 64 KiB 模式数据，并在接收端检查帧身份、顺序、长度及每个 payload 字节。**它没有文件读写、SHA-256 或 CRC32C**；内容检查不冒充文件传输完整性测试。

## 开始、持续和关闭阶段

阶段探针在数据完成但两端 socket 仍打开时同步采样，随后再放行关闭；关闭屏障不计入 payload 用时。代表性样本：单连接 3/8/64 GiB。

| 阶段/指标 | 实测 | 含义 |
|---|---:|---|
| process start 到 transport ready | 客户端约 2.08–2.14 s；服务端约 2.12–2.19 s | 包含建连流程；服务端还包含等待对端到达，不是纯网络 RTT。 |
| QP 初始化 | 单端约 1.03–1.09 s | 分配/清零缓冲区是主要部分；不能把整段都说成 MR 注册耗时。 |
| 用户态缓冲区分配与清零 | 约 0.98–1.04 s/连接 | 每连接常量初始化成本，尚未优化。 |
| `ibv_reg_mr` | 两个 MR 合计约 46–50 ms/连接 | 比缓冲区初始化短一个数量级。 |
| 初始 RQ 预投递 | 约 34–36 μs/连接 | 不是建连主耗时。 |
| 单进程峰值 RSS | 中位约 2,112 MiB；范围 2,111–2,112 MiB | 每连接接收池约 2 GiB。4 连接时每节点约 8.25 GiB 进程 RSS；空闲可用内存约 25.9 GiB。 |
| 正常关闭 | socket close 约 0.12–0.19 s | 与数据期计时分离。 |

CPU 通过每 250 ms 读取进程及其工作子进程的 `/proc` CPU tick 采样；RSS 使用 `/usr/bin/time` 和并发进程汇总。单连接 64 GiB 固定版平均 CPU：R770-1 约 0.58 核、R770-2 约 0.94 核；四连接×64 GiB 时分别约 1.95–2.03 核和 3.11–3.61 核。并发四连接的 group 峰值 RSS 两端均约 8.25 GiB。CPU tick 是进程及其线程总量，不能定位具体函数热点。

网卡采样同时读取 `/sys/class/infiniband` HCA 与 netdev 计数。64 GiB 固定样本的 HCA 发送数据计数约增加 18.17×10⁹ units（发送端），接收数据计数约增加 18.17×10⁹ units（接收端）；netdev `tx_errors/rx_errors/tx_dropped/rx_dropped` 均为 0，HCA `port_rcv_errors/port_xmit_discards/port_xmit_wait/link_error_recovery` 均未增加。HCA 的 `out_of_buffer` 表示对应 QP/RQ 缺少 WQE 时发生的丢弃，`rnr_nak_retry_err` 计收到的 RNR NAK；这两个计数与 bench 观测到的短暂接收资源空档相符。[NVIDIA DOCA Telemetry Service Guide](https://networking-docs.nvidia.com/doca/archive/3-4-0/doca-telemetry-service-guide) 对计数器的定义如此。本轮无 netdev 错误/丢包或物理链路错误计数增长证据。

阶段等待和计数：

| 固定探针 | 修复前 | 修复后 |
|---|---|---|
| 3 GiB | 服务端单次接收 CQ 等待 1.532 s，最长 1.532 s；发送端一次发送完成等待 2.065 s；数据期 RNR / 接收无缓冲计数各 +1。 | payload 约 30.3 Gbps；服务端最长接收 CQ 等待约 0.37 ms，发送端最长单次等待约 2.6 ms。RNR/无缓冲计数仍有增加。 |
| 8 GiB | 服务端累计接收等待 2.360 s（单次最长 1.342 s）；发送端累计等待 3.559 s，最长 1.870 s；数据期计数各 +2。 | payload 约 30.5 Gbps；服务端完成等待总计约 7 μs；发送端最长单次等待约 3.3 ms。数据期计数各 +452，但没有 CQ work-completion error，全部内容检查通过。 |
| 64 GiB | 服务端累计接收等待 29.893 s；发送端累计完成等待 42.577 s，单次可超过 2 s；数据期计数各 +22。 | payload 约 30.6 Gbps；服务端完成等待总计约 7 μs；发送端累计等待 6.743 s，单次最长约 3.4 ms。RNR / 无缓冲计数各 +5,440；没有 payload CQ 错误或传输失败。 |

这里的“等待”是代码测得的等待函数墙钟，不等于 CPU 时间。RNR/`out_of_buffer` 是 HCA 累计计数差值；计数增量说明数据期确实发生了接收资源短缺/重试现象，但不表示每次都导致 WQE 最终失败。编码 12 指定 0.64 ms 的 RNR retry interval；实际最长观测等待仍包括 CQ/调度过程，修复版发送端最长单次完成等待约 2.6–3.4 ms，不能将其写成每次严格低于 0.64 ms。

修复版发送端仍有累计 SQ 完成等待：64 GiB 样本 `send_completion_wait_seconds=6.74 s`，该指标只在 16 个发送槽均在用、等待 CQ 回收可用槽时累计；最长一次约 3.35 ms。它约占 17.96 s payload+完成确认计时的 37.6%，是已测到的下一项发送窗口/CQ 回收成本。它没有形成旧版秒级单次停顿，但仍是单连接速率与原生 1-QP 参照差距的候选机制。需要下一任务单独观测批量 CQ 回收、每个 WQE 均 signaled 的成本，再一次只改一个机制。

关闭期单独出现的 `resp_cqe_error` / `*_cqe_flush_error` 增量集中在 QP 销毁之后的 outstanding receive flush（代表样本如服务端 `+65/+64`，其他端/样本随关闭时剩余 WQE 数变化）。在 payload 完成屏障之前这些 CQ error/flush 增量为零；应用没有报告数据期 WQE 错误，帧内容检查全通过。不能把关闭 QP 时的 flush 误记成传输期掉包，也不能将硬件计数说成全零。

修复后客户端仍有约 0.53 s 的接收 CQ 等待，bench 的 payload 计时在读取服务端完成回复后结束，因此该等待包含在吞吐时间里。此计时对应单字节完成回复的 CQ 路径，当前尚未拆分为服务端回复发送、CQ 事件通知或客户端调度；这是需单独归因的剩余结束阶段成本，不能误算成 RNR payload 停顿。

## 同配置性能对照

同一基线/固定版本，各测 2/3/8 GiB 单连接三次、4×1 GiB 三次、4×8 GiB 三次。64 GiB 原基线只测一次；固定版本测三次。表中是成功接收端的有效 payload Gbps，不包含帧头字节。

| 负载 | 修复前中位（范围） | 修复后中位（范围） | 修复前后变异系数 |
|---|---:|---:|---:|
| 单连接 2 GiB | 30.19（30.17–30.48） | 30.06（30.03–30.28） | 0.46% / 0.37% |
| 单连接 3 GiB | 14.93（10.78–18.08） | 30.28（30.25–30.45） | 20.49% / 0.29% |
| 单连接 8 GiB | 16.44（14.32–16.46） | 30.33（30.31–30.40） | 6.38% / 0.13% |
| 单连接 64 GiB | 11.48（仅 1 次） | 30.35（30.26–30.38，3 次） | 基线样本不足 / 0.17% |
| 4×1 GiB，总 4 GiB | 11.19（11.19–11.26）组端到端 | 11.19（11.19–11.19）组端到端 | 0.29% / 0.01% |
| 4×8 GiB，总 32 GiB | 48.32（47.50–48.45）组端到端 | 48.45（47.61–48.88）组端到端 | 0.88% / 1.09% |

短四通道用例的大部分时间花在连接建立和任务启动上，不能代表稳态。为验证持续吞吐，额外测固定版本单连接 64 GiB 两次（加上原固定样本共三次），以及 4×64 GiB/连接（总 256 GiB）三次：

| 固定版持续负载 | payload 速率 | 含启动/建连/完成等待的组端到端速率 | 稳定性 |
|---|---:|---:|---:|
| 单连接 64 GiB，n=3 | 中位 30.35 Gbps（30.26–30.38） | 中位 26.89 Gbps（26.83–26.89） | payload CV 0.17% |
| 4 连接×64 GiB，n=3 | 每 lane 中位 23.11 Gbps；四 lane payload 速率相加约 92.43 Gbps | 中位 82.97 Gbps（82.51–83.13） | 组速率 CV 0.32% |

端到端组速率以本机总有效字节除以从 launch 到全部 client 进程结束的墙钟计算；并发 lane 的 duration 没有相加。组速率低于 payload 速率，是启动、四条连接各自初始化和终结成本所致的一部分。

## 原生 verbs 参照与差距

同一 RDMA 直连、同一 GID/MTU 1024，`perftest 6.24` 每轮 10 秒、1,052,672-byte message、发送队列 16、CQ moderation 1，明确绑定数据口。每种运行三次：

| 基准 | 3 次均值结果（Gbps） | 与 CPNetFlux 对照边界 |
|---|---:|---|
| `ib_send_bw`，1 QP | 92.59 / 92.59 / 92.59 | 原生 SEND/RECV；不含 CPNetFlux 帧解析、逐字节模式检查和应用确认。 |
| `ib_send_bw`，4 QP | 92.59 / 92.59 / 92.59 | 原生多 QP 汇总值；与 CPNetFlux 四独立连接仅比较链路尺度。 |
| `ib_write_bw`，1 QP | 92.59 / 92.59 / 92.59 | 原生 WRITE；协议语义不同，仅作硬件基准。 |

以 92.59 Gbps 作参照，CPNetFlux 单连接 64 GiB payload 约为 32.8%；四连接长传输的 payload lane 速率之和约为 99.8%，但四连接端到端组速率约为 89.6%。后两者都不是协议等价对比，不能宣称 CPNetFlux 已达到“100% 原生性能”：CPNetFlux有帧头/拷贝/内容校验/确认，native 不做这些；报告同时给稳态 per-lane 和含初始化的总墙钟，避免混算。

## 回归、安全边界与失败样本

- R770-1、R770-2 两端同版本构建。有效的 RDMA 定向 CTest：60/60 通过，其中包括显式 `.1`/`.2` 硬件双向回归与双方有效 `min_rnr_timer=12` 检查。RDMA-off 构建定向门禁：59 通过、1 项按无硬件条件 skipped。
- 两端全量 CTest：482 项、481 通过、1 项 io_uring 能力跳过、0 失败。观察版第一次全量运行中两个认证单测缺少环境变量 `CPNETFLUX_TEST_TOKEN` 而失败；只对这两项用临时进程环境值重跑后通过。初次失败日志、红灯回归日志和定向重跑都保留在 `logs/`，未将认证值写入仓库或日志。
- 修复前新增的硬件回归先失败：实测 `negotiated_min_rnr_timer=0`，期望为 12；修复后两端均查询为 12。这个红灯证明了实际 QP 参数而非仅仅测试 stub。
- 正常数据期 `req/resp_cqe_error` 和 flush delta 为零；HCA 的 RNR/无缓冲增量保留如实记录。关闭阶段 flush 单独列出。netdev error/drop 本轮 delta 为零。
- 未改变 2121 服务、路由、接口地址、MTU、NUMA、电源或其他项目；未持久化测试 payload。未运行 GridFTP 或跨域性能实验。

## 当前判断与后续

**已证实并修复：** QP 的 `min_rnr_timer=0` 默认编码对应 655 ms 的 RNR retry interval；队列暂时无 WQE 时，发送完成和接收 CQ 均出现秒级等待。两端设为编码 12 后，旧版秒级单次等待消失，3/8 GiB 分别稳定在约 30.3 Gbps，CV 均低于 0.3%。长传输数据期仍有 RNR 计数，并且发送端累计 SQ 完成等待仍占单连接计时的重要比例；这些是下一步需要独立分析的机制。

**还没有证实：** 是什么代码区段、CPU 指令或调度点使 RQ 偶尔耗尽，尚未定位。接收端单连接进程约占满一个 CPU 核（64 GiB 样本 CPU/墙钟约 1.0），但需要 `perf`/火焰图或分段复制计时，不能直接归咎于拷贝。每连接约 2.1 GiB RSS 和约 1 秒的分配/清零成本仍在。下一阶段应将发送端 16-slot SQ/CQ 回收成本与接收端 RQ 空档分别加细粒度观测，再只选一个确认的热点复测 64 GiB 与 4×64 GiB；不要先增大 2 GiB 接收池来掩盖问题。另将完成回复的约 0.53 s CQ 等待单独归因。

回滚代码：只执行 `git revert 300805c299fd5bca0b4a8d8107159e79556d5214`，保留诊断提交、原始基线与证据；不 reset/clean，不改变实验数据。

## 原始交付

结构化 CSV：`data/samples.csv`、`data/lanes.csv`、`data/phases.csv`、`data/trace-events.csv`、`data/network-deltas.csv`、`data/native.csv`、`data/summary.csv`、`data/native-summary.csv`；完整 JSON 为 `data/summary.json`。两台服务器各自的原始日志、闭合前后快照、构建输入与命令、失败样本都在实验目录。Windows 收到同路径副本并逐文件 SHA-256 核验。
