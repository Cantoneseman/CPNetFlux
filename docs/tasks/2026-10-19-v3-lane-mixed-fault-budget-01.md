# V3-LANE-MIXED-FAULT-BUDGET-01：数据与控制通道并发故障预算验收

- 状态：done；12 组混合故障跨机矩阵与 8 项相关回归全部通过。
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

## 实际解决的问题

这一阶段补上了上一阶段没有覆盖的故障组合：一条 lane 的数据连接和另一条 lane 的控制连接同时断开。测试代理现在会等两类故障都准备好后再近同时注入，并记录故障所属 lane、连接和时间；runner 会同时检查数据恢复、控制重连和共享预算，而不是只看退出码。

跨机实测没有发现预算被两种恢复分支重复消费。实际确认 N=0 不恢复并明确失败，N=1 只使用一次恢复预算后失败，N=2 使用两次预算完成数据恢复和控制重连并成功传输。过程中没有修改生产状态机。

## 最终验收结果

- 代码提交：6182067f2dc88ccf7ddae49bc27b9ff13d0ed53f，已推送到 origin/codex/V3-LANE-MIXED-FAULT-BUDGET-01，远端 SHA 回读一致。
- 构建：Release、TLS 开启、跨机测试开启、io_uring 关闭；构建成功。源码归档 /tmp/cpnetflux-runs/V3-LANE-MIXED-FAULT-BUDGET-01/source.tar SHA-256：b151ce888bf507dcd47acb2b4d653a30f5e87c2e8dfde9216a341f1822c77eb7。服务端二进制 SHA-256：3c49230d1290c6aa6bcfe15e1cb7bd479e030c0d26a795b30c1704af69d1fb98；上传客户端：afd66df74853e920d3226079a41cccea963ace941b8888ee13dd9c1658829190；下载客户端：e08221936020d524dfd01056889b048fef7981da5e40d0e67dc5588377e76f7c。
- Python 门禁：代理/runner py_compile 通过；混合门控单元测试共 6/6 通过。
- 跨机矩阵 CTest cpnetflux_lane_mixed_fault_budget_interop：1/1 通过，167.04 秒，实际包含深圳/上海服务端 × 上传/下载 × N=0/1/2 共 12 个 case。证据目录：/tmp/cpnetflux-runs/V3-LANE-MIXED-FAULT-BUDGET-01/build/cpnetflux-lane-mixed-fault-budget-interop-evidence/20261018T212313Z-1792358593470296053/。
- 12 个 case 的结果：N=0 的恢复使用数为 0；N=1 的恢复使用数为 1 并明确失败；N=2 的恢复使用数为 2 并成功。N=2 每个成功 case 的树校验值为 crc32c:f6f80ad4；控制重连计数为 1，数据连接重建也计入同一总预算。代理日志证明数据 lane 与控制 lane 不同，并且两次 reset 间隔小于 500 ms。
- 既有回归 CTest：单数据恢复、控制恢复、N=1/N=2/N=0 预算共 8/8 通过，用时 218.59 秒，日志 /tmp/cpnetflux-runs/V3-LANE-MIXED-FAULT-BUDGET-01/regression-ctest.log。完整 C++ 单元测试为 344 项中 343 通过、1 项 io_uring 条件跳过；代理 CTest 1/1 通过。
- 资源和清理：测试前深圳可用磁盘约 44 GiB、上海约 77 GiB，可用内存约 2.5/2.8 GiB；结束后 24600–24799 无本任务监听，runner/代理已退出；两端生产端口 2121 仍在监听且未停止或改配。

## 边界与下一步计划

本阶段只证明软件层数据/控制双故障注入下的共享预算和恢复计数；没有证明物理网卡、不同物理链路、local bind、吞吐、TLS/生产认证或生产 readiness。

下一步 V3-LANE-FAULT-STORM-BUDGET-01 要解决的问题是：现在只验证了同一时刻的两种故障，还没有验证故障连续到来时预算是否仍然严格受限。例如先断一条数据 lane，恢复过程中再断另一条数据 lane 或控制连接。下一阶段将按时间顺序注入 3 个故障，覆盖 N=0/1/2/3，确认每次恢复都只消耗一个预算单位，预算用完后立即清楚失败；只有红测证明状态机或错误摘要有问题才修改生产代码。物理 NIC、local bind 和不同物理链路继续按用户要求暂缓。
