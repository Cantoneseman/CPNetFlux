# R770 年度舒适区 1 GiB 对比实验

## 范围

在 R770-1/R770-2 当前 100 GbE 直连条件下，以精确 1 GiB（1,073,741,824 字节）不可压缩 tmpfs 单文件，完成 CPNetFlux 与 Globus GridFTP 的双向主对比，以及五种安全设置对照。数据面使用 192.168.100.1/.2 并显式本地 bind，网卡 RX/TX ring 保持 1024/1024。

CPNetFlux 的主对比和两个安全臂统一使用 V3/TCP、四条数据流、接收端 buffered inline mmap、direct receive opt-in 关闭、256 KiB DATA、请求 SO_RCVBUF 1 MiB、每流 1.5 GB/s application pacing、CRC32C、1 MiB chunk、每 16 块 checkpoint、compression off。当前修复在不改变这些配置的前提下，将四条发送 lane 的首帧 admission 按最大帧周期均匀错相，以验证是否能降低接收队列缓冲耗尽。安全组只在所测认证或 TLS 属性上变化。GridFTP GSI 明文沿用已核验的四流、MODE E、1 MiB TCP buffer、256 KiB block。

安全组包含 Globus GridFTP 匿名明文、GSI 明文、GSI 私密数据、CPNetFlux token 明文、CPNetFlux token 加控制和数据 TLS。每组固定 R770-2 到 R770-1，三次分块交错。主对比固定两种软件各自配置，每方向五对交错。

## 非目标

- lane 首帧相位展开仍是待实验验证的生产代码变更，不预先宣称性能优化通过准入。
- 不修改 CPNetFlux 的 pacing rate、frame、buffer、校验/恢复语义、协议或任何系统/NIC 设置，不降低实验硬门。
- 不运行 5 GiB 或更大负载，不改 RX/TX ring、RSS/IRQ/NAPI、路由、MTU、sysctl、THP、swap 或全局服务。
- 不把内存盘 TCP 结果外推为磁盘持久化、原生 verbs、100 Gb/s 稳定吞吐或 1 Tb/s 汇聚链路结果。
- 不重用此前被网络门拒收的样本，也不通过重试、阈值放宽或重置计数基线绕过门槛。
- 不提交凭据、构建产物或大 payload。

## 验收标准

- 代码输入为 R1 提交并推送、R2 同步构建核验的固定 CPNetFlux commit；两端 server/upload/download SHA-256 一致。
- 每次正式样本仅运行一个传输，四条实际数据流，显式绑定直连接口，记录双端终态、模式回执、外部 SHA-256、内存/cgroup/swap、接口/HCA 计数和 TCP 重传。
- 传输前空载计数稳定，MemAvailable 至少 12 GiB 加 payload 与 256 MiB 余量，swap-out、OOM/cgroup 事件、接收漏包、缓冲不足、CRC、错误或丢包增量均为零。
- 主对比双向各有 5 个合格 CPNetFlux 样本和 5 个合格 GSI GridFTP 样本，形成 5 个完整交错配对。安全组每臂 3/3 合格，模式 receipts 证明认证和文件数据保护状态。
- 失败行保留但不纳入吞吐统计。任何硬门失败后停止当前矩阵，不补跑单个样本凑数。
- lane 相位单测、相关 C++/Python 门禁和双端同提交构建通过后，才允许启动新的完整矩阵。实验报告和证据在 R1 项目与 /home/Su/experiments/CPNetFlux/，完成后复制到指定本地实验报告目录并核对 SHA-256。

## 预期受影响文件

- include/cpnetflux/core/io/detail/sender_pacing.h
- src/core/io/persistent_tree_transfer.cpp
- tests/unit/framed_data_socket_test.cpp
- tools/perf/r770_annual_optimized_node.py
- 本任务范围记录

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
- 双端 observer 均完成订阅 ready 与 stop/drain 握手。128 MiB smoke 负载高于 64 MiB 自适应范围阈值，须触发四到八条 CRC32C 并行范围。诊断客户端开启 phase timing 收集每范围实际 mmap/backend 回执，正式性能样本仍关闭 phase timing。NETLINK 过载、截断、observer 异常退出、样本无实验 socket 或任一预期数据流缺少双端活动及终态证据时拒收。
- R1 py_compile、runner 单测与相关 CTest 通过。改动在 R1 精确提交并推送后，R2 同提交同配置构建通过，server/upload/download 二进制 SHA-256 一致。
- 新的唯一验证 run 中短连接、GridFTP 四数据流和 CPNetFlux 四数据流均留下双端 4-tuple/socket-cookie/TCP_INFO 证据。任何验证门未通过，不启动年度 1 GiB 矩阵。
- 完成验证后使用未使用过的新 annual run ID，原 1 GiB 安全臂与双向主比较保持原样本数、顺序、参数和其余硬门。失败证据不纳入速率统计。

### 预期受影响文件

- tools/perf/r770_annual_optimized_node.py
- tools/perf/r770_annual_optimized_runner.py
- tools/perf/test_r770_annual_optimized_runner.py
- 本任务范围和结果报告
- 本轮新 run 目录中的 observer/样本证据

8 MiB 准入验证中，GridFTP 四流与双端 TCP_INFO 观测通过。CPNetFlux mmap 客户端按协议拒绝了未分段的小文件，原因是 8 MiB 未超过自适应范围门槛，无法产生接收端 mmap 所需的四至八个并行范围。该失败记录保留，不作为性能样本。128 MiB 验证的 GridFTP 与 CPNetFlux 实际传输均成功，CPNetFlux 四流、双端终态、0 重传及 NIC 门通过，但 runner 因正式 phase timing 关闭而未收到范围级 backend 回执，仍按验证样本拒收。新的诊断 run 将只对诊断客户端开启 phase timing，正式 1 GiB 参数和硬门不变。

代码输入提交为 `8650384de955becf7ee240fc52f7f785e67b7135`。诊断验证 run ID 为 `2026-11-27-r770-tcp-diag-validation-02`。首次年度矩阵 run ID 为 `2026-11-27-r770-annual-comfort-security-1g-01`、`2026-11-27-r770-annual-comfort-main-forward-1g-01` 与 `2026-11-27-r770-annual-comfort-main-reverse-1g-01`。

### 2026-11-27 正式矩阵重跑

首次 security 矩阵在前五个 1 GiB 样本通过后，于 `security-formal-06-clear` 停止。接收和 CRC32C 校验通过，接口/HCA 错误计数在样本期间没有增量；双端 socket 证据确认 R2 到 R1 的一条数据流有 1 次 TCP 重传、1408 字节，因此该样本按硬门拒收。样本原始证据保留于 `2026-11-27-r770-annual-comfort-security-1g-01`，不计入年度结果。收尾核验显示两端临时 listener、凭据和 tmpfs payload 均已清理，postflight 身份匹配，清理后的接口错误计数增量为零。

首次完整重跑 `-02` 在首个 128 MiB GridFTP 准入 smoke 停止，没有产生正式性能样本。R1 观测到该服务 listener 的一个 `SYN_RECV` 半连接快照没有 TCP_INFO，但相同 cookie 的 CLOSE 终态具有完整 TCP_INFO，重传为零。过滤器原先未排除 `SYN_RECV`，导致半连接被误计为完整实验 socket。修复 TCP 状态过滤并添加回归测试后，正式重跑 `-03` 至 `-12` 均完成 smoke，但在正式安全矩阵中遇到真实 TCP 重传而停止。`-03` TLS 四 lane 各重传 1 次，共 552、1448、760、760 字节。`-04` TLS lane 4 重传 1 次、760 字节。`-05` clear lane 2 重传 1 次、1256 字节。`-06` 在第 10 个样本 TLS 的 lane 1 和 lane 4 各重传 1 次、760 字节。`-07` 在第 10 个样本 TLS 的 lane 1、3、4 各重传 1 次，共 760、1448、760 字节。`-08` TLS lane 2 重传 1 次、952 字节。`-09` 在第 4 个 clear 样本 lane 4 重传 1 次、936 字节。`-10` 在第 10 个 TLS 样本四条 lane 均重传 1 次，共 760、1448、760、1448 字节。`-11` 在第 10 个 TLS 样本 lane 1 和 lane 2 各重传 1 次，分别 72 和 1448 字节。`-12` 在第 2 个 TLS 样本 lane 4 重传 1 次、296 字节。被拒收样本的传输和 CRC32C 校验成功，样本窗口内 NIC/HCA 错误增量均为零；重传门保持硬拒收，所有证据保留且不计入年度结果。核对确认运行参数仍为四条 TCP 流、每流 1.5 GB/s pacing、256 KiB frame、CRC32C、1 MiB TCP receive buffer 请求、1 MiB chunk 和每 16 块 checkpoint。重传发生在不同安全模式和 lane，尚无足够证据证明是软件缺陷，不改产品行为或重传门槛。上一矩阵使用 security `2026-11-27-r770-annual-comfort-security-1g-13`、正向 `2026-11-27-r770-annual-comfort-main-forward-1g-13`、反向 `2026-11-27-r770-annual-comfort-main-reverse-1g-13`。

### 2026-11-27 RX 缓冲与 lane 节奏

矩阵 `-13` 在第 6 个 clear 样本完成 1 GiB 传输和 CRC32C 校验，实测 34.80 Gbps，但按硬门拒收。R1 接收侧 `rx_missed_errors` 和 mlx5 `rx_out_of_buffer` 各增加 508；R2 的 lane 1 和 lane 3 记录了多段 TCP 重传。物理 CRC、符号错误和丢弃计数没有增量。队列快照显示接收包主要落在 RX queue 9、27 和 29。服务 pacer 记录四条 lane 的首帧 admission 相差约 4 微秒，说明四路 256 KiB 数据帧以近乎同相的节奏启动。Linux mlx5 文档将 `rx_out_of_buffer` 定义为接收队列没有可供网卡使用的软件 buffer。依据这些证据，本轮在发送端对首帧 admission 按 lane 均分最大帧周期相位，随后维持原来的每 lane 1.5 GB/s no-catch-up 节奏。该变化不修改 frame、接收缓冲、NIC/IRQ/ring 设置或硬门，当前只验证相位展开假设，尚不能视为性能修复已证实。

新完整年度矩阵使用 security `2026-11-27-r770-annual-comfort-security-1g-14`、正向 `2026-11-27-r770-annual-comfort-main-forward-1g-14`、反向 `2026-11-27-r770-annual-comfort-main-reverse-1g-14`。必须先确认接收端 RX OOB/missed 为零、四条 TCP socket 零重传及全部数据完整，再以固定提交完成 security 15/15 和正反向各五对。`-01` 至 `-13` 的历史记录和拒收样本保持独立，不能拼接入新矩阵。
