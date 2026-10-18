# V3-LANE-MULTI-FAULT-BUDGET-01：并发 lane 故障下重试预算验收

- 状态：done；12 组跨机并发故障矩阵与 8 项相关回归全部通过。
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-MULTI-FAULT-BUDGET-01。
- 执行位置：深圳主机独立 worktree `/root/projects/CPNetFlux-worktrees/v3-lane-multi-fault-budget-01`，分支 `codex/V3-LANE-MULTI-FAULT-BUDGET-01`；上海只作传输对端。
- 输入 commit：`73b1fc2dcf78860536769b8c245d35ad80e662f7`；创建任务 worktree 时干净。

## 为什么做这一步（易懂版）

上一阶段解决了单条数据通道断开后，服务端有时要等满 30 秒才发现，并在恢复前增加已提交文件校验的问题。下一阶段要看两条 lane（两条并行传输通道）几乎同时断开时，`--reconnect-attempts N` 是否仍真的是整个传输共用的总次数：例如 N=1 时最多只能恢复一次，然后明确失败；不能因为两条通道同时请求恢复而变成两次，也不能悄悄超过 N。

当前代码已经有共享且加锁的恢复预算，所以不先认定生产代码有缺陷。本任务先把“双通道同时断开”做成可重复注入并实测；若预算行为已正确，则保留该实现，只补代理、矩阵和回归门禁。仅当失败证据能复现超额消费或错误结果时，才按红测做最小生产代码修复。

## 范围、非目标与验收标准

- 范围：扩展 CPNetFlux 故障代理，使它能等两条指定数据连接都传过一定数据后近同时重置两条连接；跨机 runner 用 N=0、1、2 分别验收；两种服务器位置（深圳/上海）及上传/下载方向都覆盖。
- N=0：两条目标连接都确实被重置；传输快速、明确失败；预算消耗 0，恢复计数为 0。
- N=1：两条目标连接都确实被重置；传输明确因预算耗尽而失败；恢复预算和实际恢复最多为 1，且摘要字段与客户端错误一致。
- N=2：两条目标连接都确实被重置；两次 lane 恢复后传输成功；预算消耗恰为 2，下载/上传结果树校验值与预期相同。
- 全部 12 个失败/成功场景均不能被“仅看退出码”误判；代理日志需记录数据连接编号及重置时刻，证明确有两个不同的初始连接在小于 500 ms 窗口内重置。
- 运行结束要停止本任务启动的服务/代理，核对端口/PID；证据保存在 `/tmp/cpnetflux-runs/V3-LANE-MULTI-FAULT-BUDGET-01/`，Git 只提交任务记录和脚本/配置，不提交 payload 或构建目录。

非目标：不改多 lane 调度策略；不做吞吐优化、TLS/生产认证验收、生产 readiness 结论；不验证物理 NIC、local bind 或不同物理链路（按用户要求继续暂缓）；不触碰历史 `/root/projects/GridFlux-Beta`、`/root/projects/CPSS(DCC)` 或现有生产服务。

## 允许修改的文件

- `docs/tasks/2026-10-19-v3-lane-multi-fault-budget-01.md`
- `docs/coordination/BOARD.md`
- `tools/test/cpnetflux_fault_proxy.py`
- `tools/test/run_cpnetflux_lane_recovery_interop.py`
- 新增 `tools/test/test_cpnetflux_fault_proxy.py`
- `CMakeLists.txt`
- 已授权的最小产品修复范围：`src/core/io/tree_transfer_client.cpp` 及其必要的行为验收；仅在跨机红测证明错误时修改，并先更新任务单影响范围。本轮首个红测已证明 N=0 错误原因没有说明预算耗尽。

禁止改动其他工作树、主仓库共享 index、凭据、生产服务配置或历史实验目录；不使用 `git add .`。

## 环境与验收命令

运行前在两端复核 `/`、`/tmp` 空间（各自至少保留 10 GiB 加本次峰值需求）、内存、依赖、实验端口 `24600–24799` 是否空闲、已有本任务进程。空间或端口不满足就停止实验，先记录实际阻塞，不做未知目录清理。生产控制端口 2121 只读观察，不停止或改配。

- Python 语法/代理测试：`python3 -m py_compile tools/test/cpnetflux_fault_proxy.py tools/test/run_cpnetflux_lane_recovery_interop.py tools/test/test_cpnetflux_fault_proxy.py`；`python3 -m unittest tools/test/test_cpnetflux_fault_proxy.py`。
- 构建：使用新建的 `/tmp/cpnetflux-runs/V3-LANE-MULTI-FAULT-BUDGET-01/build`，源目录为本 worktree；保存 CMake 配置、构建命令、输入 commit、源码归档和二进制 SHA-256。
- 行为门禁：`ctest --test-dir /tmp/cpnetflux-runs/V3-LANE-MULTI-FAULT-BUDGET-01/build --output-on-failure -R 'cpnetflux_fault_proxy_unit|cpnetflux_lane_multi_fault_budget_interop'`。
- 交叉验收需包含服务器位置 × 上传/下载 × N=0/1/2 共 12 个 case；若因公共链路抖动失败，保留原始 case 和代理日志，先单独判定是否证实两次 reset，不能把环境失败冒充预算缺陷。

## 实施中发现与处理的问题

- 恢复预算本身使用加锁共享计数；实测没有发现并发 lane 恢复超过配置预算。
- 首个 N=0 双云用例在 10.24 秒内确认代理重置两条不同数据连接，间隔约 53 微秒；预算和实际恢复数均为 0，但用户看到的错误只说明 worker 重试次数耗尽。根因是零预算早期路径绕过了 lane 级错误映射。
- 只改 lane 映射后，下载方向仍显示底层 recv 连接重置。最终在 N=0 外层同时映射 worker retry limit 和传输连接断开，统一报告预算耗尽；固定提交已重建并通过 upload/download 双云复验。
- 原代理不能同时对两个数据连接注入故障。本轮加入可重复目标与同步门控；门控等待两个连接都达到数据阈值后，再在 500 ms 内重置两条连接，并防止双向 pump 重复计数。
- 全部 12 个场景都确认两个初始数据连接确实重置。预算 N=0 时消耗/恢复 0 次并清楚失败；N=1 时消耗/恢复 1 次后明确失败；N=2 时消耗/恢复 2 次后上传和下载均成功，树校验值相同。
- 本轮只发现并修复了零预算错误信息不一致的问题；未发现共享预算超额或传输内容错误。

## 执行记录与下一步交接

- 最终零预算错误提示修复提交为 c2031d221a94186057e4317f75db955b4de30ae5，固定提交重建和跨机矩阵均已通过。
- 任务计划提交：`e0fb8a57de12c7f2d758cdfcd799582e851fc2e7`，已推送并回读 origin 同 SHA。
- 测试代理/runner 实现提交：`c0361322f9c5c950e0883388a0cb745ae4e2b21f`，已推送并回读 origin 同 SHA；固定输入构建成功。
- 代理同步门控 RED→GREEN：`python3 tools/test/test_cpnetflux_fault_proxy.py`，先因缺少门控类失败，实现后 3/3 通过；`cpnetflux_lane_recovery_smoke` 与 `cpnetflux_fault_proxy_unit` CTest 2/2 通过。
- 跨机矩阵首次红测：CTest `cpnetflux_lane_multi_fault_budget_interop` 在首个用例按预期中止；证据目录 `/tmp/cpnetflux-runs/V3-LANE-MULTI-FAULT-BUDGET-01/build/cpnetflux-lane-multi-fault-budget-interop-evidence/20261018T204324Z-1792356204499525037/shanghai-upload-multi-data-budget-0/`。`proxy.jsonl` 确认两个独立目标连接均重置，间隔约 0.000052 秒；`summary.json` 确认预算 0 / 使用 0 / 恢复 0 / 失败，但错误消息不够清楚。
- 修正根因：第一次仅删除 `used()!=0` 判断后，固定构建的 N=0 跨机用例仍返回原始错误；证据确认 N=0 早期分支未创建共享预算对象。随后 upload 的 worker retry limit 路径已映射清楚，但 download 红测证实直接的 TransportError 也必须在同一外层标注零预算耗尽；当前实现已扩展到两类错误，已在固定构建和完整矩阵中验收。

## 最终验收结果

- 固定测试输入：c2031d221a94186057e4317f75db955b4de30ae5，Release 构建成功。源码归档 SHA-256：2f0fc1e81421b86c4a7c5f4fa4b756337755c4103f14d7931c44e04dc90efe62；服务端二进制 SHA-256：3c49230d1290c6aa6bcfe15e1cb7bd479e030c0d26a795b30c1704af69d1fb98；上传客户端：afd66df74853e920d3226079a41cccea963ace941b8888ee13dd9c1658829190；下载客户端：e08221936020d524dfd01056889b048fef7981da5e40d0e67dc5588377e76f7c。
- 跨机矩阵 CTest cpnetflux_lane_multi_fault_budget_interop：1/1 通过，167.16 秒；包含深圳/上海服务端 × 上传/下载 × 预算 0/1/2 共 12 个 case。证据目录：/tmp/cpnetflux-runs/V3-LANE-MULTI-FAULT-BUDGET-01/build/cpnetflux-lane-multi-fault-budget-interop-evidence/20261018T205948Z-1792357188869111748/。每个 proxy.jsonl 都记录两条不同数据连接的 reset，时间差小于 0.5 秒。
- N=0 时两条数据连接确实中断，预算消耗和 lane 恢复均为 0，并清楚报告预算耗尽；N=1 时只恢复 1 次后按预期失败；N=2 时恢复 2 次并成功，上传/下载结果树校验值均为 crc32c:f6f80ad4。没有发现并发恢复超出共享预算的问题。
- 相关回归 CTest：数据 lane 单次恢复、控制 lane 上传/下载恢复、N=1/N=2/N=0 预算场景共 8/8 通过，用时 224.09 秒，日志 /tmp/cpnetflux-runs/V3-LANE-MULTI-FAULT-BUDGET-01/regression-ctest.log。代理 Python 单测 3/3；本地恢复与代理 CTest 2/2；完整 C++ 单元测试 344 项中 343 通过、1 项因本构建关闭 io_uring 而条件跳过。
- 矩阵与回归运行前磁盘可用空间分别为深圳 47/45 GiB、上海 78 GiB；可用内存约 2.5/2.8 GiB。结束时测试端口 24600–24799 无监听，本任务 runner/代理进程已结束；生产端口 2121 只读检查，两端服务保持监听。
- 边界：这次证明两条数据 TCP 通道在软件故障注入下的共享预算行为；不代表物理网卡、不同物理链路、吞吐、TLS/生产认证或生产 readiness 已验收。

## 下一步开发计划：V3-LANE-MIXED-FAULT-BUDGET-01

### 要解决的问题（易懂版）

本轮同时断开的是两条数据通道。还没有验证一种更复杂但真实的情况：一条 lane 的数据连接断了，另一条 lane 的控制连接也在差不多同一时刻断开。它们走不同的恢复分支；要确认 reconnect-attempts 仍是整个传输共用的一个总次数，不会因数据恢复和控制恢复分别计数而超额，也不能错误地重复恢复同一 lane。

### 范围与验收

- 为测试代理和跨机 runner 增加可重复、可同步的混合故障注入：一个指定数据连接 reset，另一个指定 lane 的控制连接 reset；日志需证明目标 lane 不同、两种 reset 的间隔小于 500 ms。
- 覆盖深圳/上海作服务端、上传/下载、预算 N=0/1/2；逐项核对 data/control 重连数、lane 恢复数、共享预算消耗和最终树校验值。
- N=0 不做恢复且给出清楚错误；N=1 最多完成一次恢复，第二种故障使传输明确失败；N=2 恢复两种故障并完成传输，最终树校验值一致。若实测证明现有行为已正确，只增加测试与日志，不改生产状态机。
- 非目标：调度算法重写、吞吐优化、TLS/生产认证、物理 NIC/local bind/不同物理链路及 readiness。
