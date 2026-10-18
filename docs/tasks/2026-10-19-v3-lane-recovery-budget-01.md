# V3-LANE-RECOVERY-BUDGET-01：统一局部与整批恢复预算

- 状态：done
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-RECOVERY-BUDGET-01
- 权威开发端：深圳 `gridflux-beta-shenzhen`；上海仅作传输对端。
- 输入提交：`6d1e11cf31cafb6bd316f1bc6754096439ec7e6b`（下载控制恢复阶段的实现和验收记录）。
- worktree/分支：`/root/projects/CPNetFlux-worktrees/v3-lane-recovery-budget-01` / `codex/V3-LANE-RECOVERY-BUDGET-01`。
- 实现提交：`0cc1696dffda995b078516ffece8a7e47834cbd3`；已推送到 `origin/codex/V3-LANE-RECOVERY-BUDGET-01`，`git ls-remote` 回读相同 SHA。

## 要解决的问题（易懂版）

用户设置 `--reconnect-attempts N` 是在说“这次传输最多自动恢复 N 次”。目前代码里，lane-local 恢复和整批恢复分别拿这个数字做自己的上限：一条 lane 可以先用完 N 次局部重连，之后外层还可能再用 N 次整批重连。持续断网时，实际动作可能超过用户看到的 N 次，统计也难以表达“预算还剩多少”。

本阶段让所有自动恢复动作共用一个预算。一次 lane-local control/data 恢复或一次整批重建都消耗一个名额；预算用完后立即返回明确的 `reconnect attempt budget exhausted`，不偷偷升级更多重连。`N=0` 保持当前默认行为：不自动重连。

## 范围、非目标与验收标准

- 包含：persistent dynamic tree 的 lane-local data/control 恢复与 outer whole-batch 恢复共用预算；上传/下载统计实际消耗；预算耗尽原因；现有 transfer deadline 继续优先；相关单元测试、跨云 fault harness/CTest（如需要）。
- 不包含：物理 NIC、local bind、源地址/路由/交换路径；TLS/生产认证互操作；吞吐和 production readiness；static/per-file 自动重连入口；调度器策略重写。

验收最低标准：

1. `--reconnect-attempts 0` 不自动重连，原有默认行为不变。
2. `N=1` 时，无论先发生 lane-local 还是整批 transport 故障，最多执行一次恢复动作；第二次故障返回明确预算耗尽原因。
3. `N=2` 时最多两次恢复动作，summary 的 `reconnect_attempts`、lane-local/整批计数和失败原因一致。
4. 上传、下载和现有双向跨云控制恢复回归通过，树 SHA 与 manifest 不变。

## TDD/开发顺序

- 先写一个连续故障注入红测：旧代码在局部恢复后再次进入整批恢复时，实际恢复动作数大于 N 或失败原因不明确。
- 再设计一个共享的、线程安全的预算对象；lane worker 和 outer loop 通过同一 `tryConsume` 取得名额，不能由各自的局部计数绕过。
- 生产代码修改后补单元/集成断言，确保 deadline、非 transport 错误和 `N=0` 语义保持。

## 预计受影响文件

- `src/core/io/tree_transfer_client.cpp`
- `src/core/io/tree_transfer_client.h` 或相关内部接口（若需要传递预算）
- `tools/test/cpnetflux_fault_proxy.py`
- `tools/test/run_cpnetflux_lane_recovery_interop.py`
- `tests/unit/persistent_data_session_test.cpp` 或新增最小预算单元测试
- `docs/DIRECTORY_TRANSFER.md`
- 本任务单与 `docs/coordination/BOARD.md`

## 验收产物

完成时补充：红测输出、设计和实际改动文件、构建/CTest 命令、预算消耗 JSON、双向回归矩阵、资源端口清理、代码与文档 commit SHA、GitHub push 和 `git ls-remote` 回读、未覆盖边界与下一步交接。

## 实际结果

本次解决的问题：把“局部修一条传输通道”和“整批重新建立连接”统一算进用户设定的 --reconnect-attempts N。以前这两层各自计数，可能一个传输总共用掉两份 N；现在任何恢复动作都要先从同一个线程安全预算中拿名额。名额用完后客户端报告 reconnect attempt budget exhausted，JSON 同时记录预算上限和实际使用次数。N=0 不自动恢复。

红测记录：旧实现遇到连续两次控制断开时，第一次 lane 恢复后又遭遇第二次故障，只报 work retry limit exceeded，无法看出是恢复次数已经耗尽。原始证据在 /tmp/cpnetflux-runs/V3-LANE-RECOVERY-BUDGET-01/red-control/20261018T174909Z-1792345749450698698/shenzhen-upload-control/。

代码与测试范围：src/core/io/tree_transfer_client.cpp 实现共享预算和 reconnect_attempt_budget / recovery_attempts_used 摘要字段；tools/test/cpnetflux_fault_proxy.py 与 tools/test/run_cpnetflux_lane_recovery_interop.py 支持连续注入控制断开并验证结果；CMakeLists.txt 固化 N=0/1/2 的双云预算验收（N=1 上传和下载，N=2 上传，N=0 上传）；docs/DIRECTORY_TRANSFER.md 说明用户能观察到的行为。

云端实测：N=1 上传双向（上海服务端、深圳服务端）、N=1 下载双向，以及 N=2 上传双向均通过预算断言。N=1 连续注入两次断开后，两个恢复方向都精确记录 recovery_attempts_used=1、lane_local_recovery_count=1；N=2 注入三次后精确记录 2 和 2；N=0 注入一次后 recovery_attempts_used=0、lane 恢复计数为 0。相关矩阵：
- N=1 上传：/tmp/cpnetflux-runs/V3-LANE-RECOVERY-BUDGET-01/build/cpnetflux-lane-recovery-budget-interop-evidence/20261018T180722Z-1792346842809827554/matrix.json
- N=1 下载：/tmp/cpnetflux-runs/V3-LANE-RECOVERY-BUDGET-01/build/cpnetflux-lane-recovery-budget-download-interop-evidence/20261018T180749Z-1792346869048229574/matrix.json
- N=2 上传：/tmp/cpnetflux-runs/V3-LANE-RECOVERY-BUDGET-01/budget-two-attempts/20261018T181132Z-1792347092381786648/matrix.json
- N=0 上传：/tmp/cpnetflux-runs/V3-LANE-RECOVERY-BUDGET-01/build/cpnetflux-zero-recovery-budget-interop-evidence/20261018T180817Z-1792346897085857511/matrix.json

验证：Release CMake 配置和目标编译通过；完整单元测试 342/342；cpnetflux_lane_recovery_smoke 1/1；上传控制恢复、下载控制恢复、N=1 上传预算、N=1 下载预算、N=0 双向互操作各 CTest 通过。N=2 双云上传预算 CTest 1/1。cpnetflux_reconnect_smoke 本轮在 data-only reset 分支 30 秒后失败；用输入提交的旧构建 /tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01/build 重跑，同样在相同分支失败并得到 transfer total deadline exceeded，因此确认是本任务开始前已有的 data-fault 恢复问题，不归因于预算实现。该问题作为下一任务处理。证据在 /tmp/cpnetflux-runs/V3-LANE-RECOVERY-BUDGET-01/reconnect-debug/ 和 /tmp/cpnetflux-runs/V3-LANE-RECOVERY-BUDGET-01/reconnect-debug-baseline/。

资源与边界：深圳剩余磁盘 57 GiB、上海 79 GiB；两端保留的服务端口正常在监听，测试使用独立的 24xxx/45xxx 高位端口范围。未做物理 NIC/local bind/真实多物理链路测试，按用户要求留待软件开发完成后进行。

## 下一步开发计划：V3-LANE-DATA-FAULT-RECOVERY-01

### 要解决的问题（易懂版）

模拟器只断开数据连接、保留控制连接时，客户端能检测到某些 lane 断线，但在 global scheduler 组合场景里仍可能等到整个 30 秒传输期限结束才退出，没能按 --reconnect-attempts 快速完成修复或清楚失败。相同问题在预算改动前的旧二进制中也能复现。本轮预算已经准确限制恢复次数；下一轮集中查清“数据断了之后哪一层还在等旧任务”，并让该场景按预算恢复或及时失败。

### 范围与验收

- 先以 /tmp/cpnetflux-runs/V3-LANE-RECOVERY-BUDGET-01/reconnect-debug/ 为基准证据，新增最小稳定复现并对旧输入提交确认红测。
- 查明数据连接 reset 后 sender/receiver、队列租约、控制 FINAL/DRAIN 的等待关系；只修复确认导致 30 秒等待的状态/唤醒路径。
- 固化上传和下载两种数据-only fault 测试；要求在配置总 deadline 内及时结束，成功时树 SHA 一致、失败时错误明确，lane 恢复和总预算统计一致。
- 不包含物理 NIC/local bind、多物理链路、生产认证/TLS readiness、吞吐优化或对 scheduler 策略重写。
