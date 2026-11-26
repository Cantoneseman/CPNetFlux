# R770 年度舒适区 1 GiB 对比实验

## 范围

在 R770-1/R770-2 当前 100 GbE 直连条件下，以精确 1 GiB（1,073,741,824 字节）不可压缩 tmpfs 单文件，完成 CPNetFlux 与 Globus GridFTP 的双向主对比，以及五种安全设置对照。数据面使用 192.168.100.1/.2 并显式本地 bind，网卡 RX/TX ring 保持 1024/1024。

CPNetFlux 的主对比和两个安全臂统一使用 V3/TCP、四条数据流、接收端 buffered inline mmap、direct receive opt-in 关闭、256 KiB DATA、请求 SO_RCVBUF 1 MiB、每流 1.5 GB/s application pacing、CRC32C、1 MiB chunk、每 16 块 checkpoint、compression off。安全组只在所测认证或 TLS 属性上变化。GridFTP GSI 明文沿用已核验的四流、MODE E、1 MiB TCP buffer、256 KiB block。

安全组包含 Globus GridFTP 匿名明文、GSI 明文、GSI 私密数据、CPNetFlux token 明文、CPNetFlux token 加控制和数据 TLS。每组固定 R770-2 到 R770-1，三次分块交错。主对比固定两种软件各自配置，每方向五对交错。

## 非目标

- 本轮没有新生产性能优化通过准入，不修改 C++ 传输逻辑或系统设置。
- 不运行 5 GiB 或更大负载，不改 RX/TX ring、RSS/IRQ/NAPI、路由、MTU、sysctl、THP、swap 或全局服务。
- 不把内存盘 TCP 结果外推为磁盘持久化、原生 verbs、100 Gb/s 稳定吞吐或 1 Tb/s 汇聚链路结果。
- 不重用此前被网络门拒收的样本，也不通过重试、阈值放宽或重置计数基线绕过门槛。
- 不提交凭据、构建产物或大 payload。

## 验收标准

- 代码输入为远端核验的 CPNetFlux branch commit 9f3c95a05278e62520c43ecf7e188b236dc64a40。如需 runner 修正，R1 提交并推送后，R2 同步同一提交、同配置构建，server/upload/download SHA-256 一致。
- 每次正式样本仅运行一个传输，四条实际数据流，显式绑定直连接口，记录双端终态、模式回执、外部 SHA-256、内存/cgroup/swap、接口/HCA 计数和 TCP 重传。
- 传输前空载计数稳定，MemAvailable 至少 12 GiB 加 payload 与 256 MiB 余量，swap-out、OOM/cgroup 事件、接收漏包、缓冲不足、CRC、错误或丢包增量均为零。
- 主对比双向各有 5 个合格 CPNetFlux 样本和 5 个合格 GSI GridFTP 样本，形成 5 个完整交错配对。安全组每臂 3/3 合格，模式 receipts 证明认证和文件数据保护状态。
- 失败行保留但不纳入吞吐统计。任何硬门失败后停止当前矩阵，不补跑单个样本凑数。
- 实验报告和完整可交付证据在 R1 项目与 /home/Su/experiments/CPNetFlux/，只精确提交本轮报告、scope、runner 与必要测试。最终将报告和证据清单复制到本机指定的实验报告目录并核对 SHA-256。

## 预期受影响文件

- tools/perf/r770_annual_optimized_node.py
- tools/perf/r770_annual_optimized_runner.py
- tools/perf/test_r770_annual_optimized_runner.py
- tools/test/run_cpnetflux_large_data_smoke.py
- 本任务范围、结果报告和证据索引

## 当前准入快照

2026-11-25 的只读 6.5 秒窗口内，两端 rx_missed_errors、rx_out_of_buffer、NIC/HCA 错误及 TCP 重传计数稳定。当前累计值为 R770-1 两项接收计数 2826、R770-2 1332；这些历史累计值保留，样本只按独立窗口增量判断。两端 ring 为 RX/TX 1024/1024，直连路由与原 2121 服务 PID/start time 未变。MemAvailable 约 27.7/28.7 GiB，/dev/shm 可用约 15.25 GiB，允许 1 GiB 及固定安全余量。

之前年度主对比的 1 GiB 矩阵使用的 CPNetFlux profile 不等同于本轮要求的舒适区 profile。本轮结果独立统计，不并入历史 1 GiB、5 GiB 或 9 GB 样本。

## 2026-11-26 续跑修正

本次续跑从提交 `1eeea5746163402d5a009aae40b88413516daea7` 开始，修正空载和传输窗口的计数口径。`idle()` 仍检查直连接口/HCA 错误、内存、swap、PSI 与 cgroup 稳定性，但不以主机级 `Tcp.RetransSegs` 增量拒绝空载窗口，因为该计数无法按接口归属，采样命令还会经过管理 SSH。每个传输样本仍对双端 `Tcp.RetransSegs` 增量执行硬拒收，不降低传输门槛。

本次使用新的唯一 run ID，security 为 `2026-11-26-r770-annual-comfort-security-1g-02`，正向为 `2026-11-26-r770-annual-comfort-main-forward-1g-02`，反向为 `2026-11-26-r770-annual-comfort-main-reverse-1g-02`。此前预检中止记录保留在 `/home/Su/experiments/CPNetFlux/2026-11-26-r770-annual-preflight-abort-01/`，不并入新 run。

本次受影响文件为 runner、对应纯单测、节点 run ID 清单和本任务范围记录。CPNetFlux 传输逻辑、网络配置、样本阈值与舒适区参数均不变。
## 2026-11-26 TCP socket 终态观测修正

### 范围

修正年度 runner 的 TCP 重传证据来源。使用双端 NETLINK_SOCK_DIAG IPv4 TCP destroy multicast 记录 socket 关闭时的 INET_DIAG_INFO，并以 TCP_INFO 中的 `tcpi_total_retrans` 和 `tcpi_bytes_retrans` 作传输样本硬门。周期 diag dump 保留活动 socket 快照，补充启动/停止握手、观测时间、4-tuple、socket cookie、事件丢失状态和四条数据流覆盖审计。主机级 Tcp.RetransSegs 只留诊断值，不参与空载或样本拒收。

### 非目标

不修改 CPNetFlux 生产协议、数据路径、舒适区参数、NIC/路由/系统设置或既有硬门。保留所有过往失败样本和报告。先完成回环短连接、GridFTP 与 CPNetFlux 的 socket 事件覆盖准入，全部通过后再运行新的完整 1 GiB 年度矩阵。

### 验收标准

- 纯解析和 runner 单测覆盖管理 SSH/2121 排除、实验端口筛选、destroy 终态事件、socket 在开始与结束间关闭、重传增量拒收、无实验 socket、事件丢失、终态缺失和四条数据流覆盖。
- 双端 observer 均完成订阅 ready 与 stop/drain 握手。NETLINK 过载、截断、observer 异常退出、样本无实验 socket 或任一预期数据流缺少双端活动及终态证据时拒收。
- R1 py_compile、runner 单测与相关 CTest 通过。改动在 R1 精确提交并推送后，R2 同提交同配置构建通过，server/upload/download 二进制 SHA-256 一致。
- 新的唯一验证 run 中短连接、GridFTP 四数据流和 CPNetFlux 四数据流均留下双端 4-tuple/socket-cookie/TCP_INFO 证据。任何验证门未通过，不启动年度 1 GiB 矩阵。
- 完成验证后使用未使用过的新 annual run ID，原 1 GiB 安全臂与双向主比较保持原样本数、顺序、参数和其余硬门。失败证据不纳入速率统计。

### 预期受影响文件

- tools/perf/r770_annual_optimized_node.py
- tools/perf/r770_annual_optimized_runner.py
- tools/perf/test_r770_annual_optimized_runner.py
- 本任务范围和结果报告
- 本轮新 run 目录中的 observer/样本证据

代码输入提交为 `61a77951c419b8d0a5a2f7e1ae6a8c7ec9c2a57a`。验证 run ID 规划为 `2026-11-26-r770-tcp-diag-validation-01`，正式年度矩阵 run ID 规划为 security/forward/reverse `...-1g-03`。