# V3-LANE-MIXED-FAULT-BUDGET-01：数据与控制通道并发故障预算验收

- 状态：planning；先锁定范围与验收标准，提交后再改测试工具或产品代码。
- 路线：R2026-10-16-V3-UNIFIED-MULTILINK。
- 执行位置：深圳独立 worktree /root/projects/CPNetFlux-worktrees/v3-lane-mixed-fault-budget-01；分支 codex/V3-LANE-MIXED-FAULT-BUDGET-01；上海仅作传输对端。
- 输入提交：5ffb0827c11fe27727539d4026501b486756ceb8。

## 为什么做这一步（易懂版）

上一阶段已经验证两条数据通道几乎同时断开时，N=0/1/2 的重试总数符合预算。本阶段要检查另一种可能：一条 lane 的数据连接断了，另一条 lane 的控制连接也同时断开。数据连接和控制连接走不同的恢复代码；需要确认它们仍共用同一个重试总数，不会每种故障各算一次，也不会重复恢复同一条 lane。

## 范围与验收标准

- 扩展测试代理和 runner，使其等待指定数据 lane 与另一条控制 lane 都到达注入点，再在 500 ms 内注入两类断开。代理日志必须显示两种故障分别发生在哪条 lane、准确时间及恢复前后的连接事件。
- 覆盖深圳/上海作服务端 × 上传/下载 × reconnect budget N=0/1/2，共 12 个实际跨机 case。
- N=0：两种故障都实际发生；不恢复；预算使用数为 0；客户端和摘要清楚报告恢复预算耗尽。
- N=1：两种故障都实际发生；只能恢复一次后明确失败；恢复预算使用数必须恰为 1。因两个故障并发，哪个恢复分支先拿到预算不作固定要求。
- N=2：两种故障都恢复；总预算使用数恰为 2；传输成功，上传与下载的最终树校验值均与源一致。
- 所有用例都核对 summary 的 recovery_attempts_used、lane_local_recovery_count、lane_control_reconnect_count、control/data connection counts，并从代理日志证明两类故障确实落在不同 lane。
- 若现有恢复逻辑全绿，只提交测试工具和验收门禁；仅在红测证实用户可见错误、预算计数或数据结果错误时，才扩大任务单范围并修生产状态机。

## 允许修改的文件

- docs/tasks/2026-10-19-v3-lane-mixed-fault-budget-01.md
- docs/coordination/BOARD.md
- tools/test/cpnetflux_fault_proxy.py
- tools/test/run_cpnetflux_lane_recovery_interop.py
- tools/test/test_cpnetflux_fault_proxy.py
- CMakeLists.txt
- 如红测证实产品缺陷，先更新本任务单并记录证据，再允许最小修改 src/core/io/tree_transfer_client.cpp。

不要修改其他工作树、主仓库共享 index、凭据、生产服务配置、历史实验目录或大 payload；不用 git add .。

## 非目标

本阶段不重写调度算法、不做吞吐优化、不验收 TLS/生产认证或 readiness；不配置物理 NIC、不做 local bind 或不同物理链路实验（按用户要求在软件故障恢复开发完成后再进行）；不触碰 GridFlux-Beta 与 CPSS(DCC)。

## 环境与测试流程

- 跨机测试前，在深圳和上海分别检查 / 与 /tmp 可用空间（各至少保留 10 GiB 加上本次峰值构建/测试需求）、内存、实验端口 24600–24799 和本任务遗留进程。生产端口 2121 只读观察，不停止或改配服务。
- 使用本任务独立目录 /tmp/cpnetflux-runs/V3-LANE-MIXED-FAULT-BUDGET-01/build；记录输入提交、CMake 配置、构建日志、源码归档及二进制 SHA-256。
- Python 检查：py_compile 与 test_cpnetflux_fault_proxy.py 单测。
- 构建与本地检查：Release 构建、完整 C++ 单元测试、故障代理与 lane recovery smoke CTest。
- 跨机检查：新增 cpnetflux_lane_mixed_fault_budget_interop CTest 跑完 12 个场景；再运行单数据恢复、控制恢复和既有恢复预算 CTest 回归。
- 运行结束后确认本任务自启 PID 已结束、24600–24799 无遗留监听、2121 服务仍在监听；保留 case summary、client/server/proxy logs 与 CTest 输出。

## 已知情况与待验证问题

- 上一阶段两个数据 lane 并发 reset 的 N=0/1/2 矩阵全部通过；零预算的两条错误路径已统一报告耗尽，未发现共享预算超额。
- 目前代理可同步重置多条数据连接，也能在某条数据连接达到阈值后重置关联控制连接，但不能把一个数据 reset 和另一个控制 reset 放进同一个同步门控。runner 目前也分别支持 multi-data 与 pure-control，未覆盖混合事件。
- 关键未知项是混合事件到来时，数据 lane 恢复与控制 lane 恢复是否会竞争或重复消耗共享预算；还要检查预算只允许一次时，失败方向和错误摘要是否清楚。

## 交接记录

本阶段计划须先独立提交并推送，读取 origin 分支 SHA 确认一致，再开始测试工具/产品代码修改。每个阶段结束时记录 commit、CTest/单测结果、跨机证据目录、资源前后状态及下一步计划。
