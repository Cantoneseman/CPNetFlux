# V3-LANE-MULTI-FAULT-BUDGET-01：并发 lane 故障下重试预算验收

- 状态：in_progress；当前阶段先提交范围与验收计划，行为实现尚未开始。
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
- N=2：两条目标连接都确实被重置；两次 lane 恢复后传输成功；预算消耗恰为 2，下载/上传结果树 SHA-256 与预期相同。
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

## 当前初步排查与待解决点

- 现有 `RecoveryBudget::tryConsume()` 使用互斥锁，共享对象传给 lane 与整批重试；尚无证据显示本身会超额消费。
- 首个真实双云 N=0 用例已在 10.24 秒内失败并确认代理同时重置数据连接 1/2，时间差约 53 微秒；摘要显示预算 0、消耗 0、lane 恢复 0，但客户端/摘要只写 `work retry limit exceeded`。根因是 `tryRecoveringPersistentDirectory` 对 N=0 直接走无预算对象的路径，绕过了 lane 级预算错误映射；仅放宽 lane 映射条件无效。首次零预算修复将 upload 的 `work retry limit exceeded` 映射为预算耗尽；后续 download 红测显示另一条 lane 路径直接返回 `TransportError: recv: Connection reset by peer`。现将 N=0 外层映射扩展为覆盖传输层断连和 worker retry limit exceeded 两种情况；仍待固定提交重建和回归验收。
- 现有代理只支持单一 `--target-data-connection`。即使重复写相同选项，argparse 默认也只保留最后一个；代理还有一个单连接目标字段，无法证明两条 lane 都被重置。这是本轮首要测试工具缺口。
- 需要确认两条连接是否都已达到数据阈值再放行故障，避免把先后两个独立故障误称为“几乎同时”；需防同一连接的双向 pump 对故障计数重复触发。
- 需要确认双 reset 时客户端 N=0/1/2 的实际错误字段、连接重建次数与 lane 恢复计数关系；这是实测未知项。

## 执行记录与下一步交接

- 当前待验收代码已撤回无效的中间条件改动，最终根因修复尚待单独提交并重建。
- 任务计划提交：`e0fb8a57de12c7f2d758cdfcd799582e851fc2e7`，已推送并回读 origin 同 SHA。
- 测试代理/runner 实现提交：`c0361322f9c5c950e0883388a0cb745ae4e2b21f`，已推送并回读 origin 同 SHA；固定输入构建成功。
- 代理同步门控 RED→GREEN：`python3 tools/test/test_cpnetflux_fault_proxy.py`，先因缺少门控类失败，实现后 3/3 通过；`cpnetflux_lane_recovery_smoke` 与 `cpnetflux_fault_proxy_unit` CTest 2/2 通过。
- 跨机矩阵首次红测：CTest `cpnetflux_lane_multi_fault_budget_interop` 在首个用例按预期中止；证据目录 `/tmp/cpnetflux-runs/V3-LANE-MULTI-FAULT-BUDGET-01/build/cpnetflux-lane-multi-fault-budget-interop-evidence/20261018T204324Z-1792356204499525037/shanghai-upload-multi-data-budget-0/`。`proxy.jsonl` 确认两个独立目标连接均重置，间隔约 0.000052 秒；`summary.json` 确认预算 0 / 使用 0 / 恢复 0 / 失败，但错误消息不够清楚。
- 修正根因：第一次仅删除 `used()!=0` 判断后，固定构建的 N=0 跨机用例仍返回原始错误；证据确认 N=0 早期分支未创建共享预算对象。随后 upload 的 worker retry limit 路径已映射清楚，但 download 红测证实直接的 TransportError 也必须在同一外层标注零预算耗尽；当前实现已扩展到两类错误，待重建/复验。
