# V3-LANE-RECOVERY-BUDGET-01：统一局部与整批恢复预算

- 状态：in_progress
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-RECOVERY-BUDGET-01
- 权威开发端：深圳 `gridflux-beta-shenzhen`；上海仅作传输对端。
- 输入提交：`6d1e11cf31cafb6bd316f1bc6754096439ec7e6b`（下载控制恢复阶段的实现和验收记录）。
- worktree/分支：`/root/projects/CPNetFlux-worktrees/v3-lane-recovery-budget-01` / `codex/V3-LANE-RECOVERY-BUDGET-01`。

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