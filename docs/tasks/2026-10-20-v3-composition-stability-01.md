# V3-COMPOSITION-STABILITY-01

Date: 2026-10-20 (Asia/Shanghai)
Status: in_progress
Owner: current commander, single serial developer
Input commit: eb45795f2e3eaca50b4b69fc3a66dd8403184ca5
Branch: codex/V3-ONLY-NO-PREDEPLOYMENT-COMPATIBILITY-01

## 易懂版问题

目前已经分别验证了数据 lane 断开、控制连接断开和重试次数限制。但真实传输可能连续遇到多种故障：数据断开后控制连接断开，再次数据断开，最后控制连接又断开。现在要确认这些恢复动作仍然共用一个总次数，不会重复计算，也不会把健康 lane 重新启动；传输成功时文件内容和批次身份必须保持一致。

## 本轮范围

- 扩展现有故障代理和跨机 runner，支持四步连续混合故障：`data#2 -> control#3 -> data#4 -> control#5`。
- 覆盖深圳/上海作为服务端、上传/下载和 `--reconnect-attempts N=0..4`，共 20 个跨机场景。
- 增加可重复的代理单测和 CTest 入口，检查故障顺序、唯一 batch ID、恢复次数、控制连接计数、健康 lane 的完成状态和最终树哈希。
- 如果现有生产恢复逻辑通过红测，不修改生产状态机；只有真实行为失败才扩大到最小产品修复。
- 构建完成后，使用同一提交和同一构建配置同步上海，再运行跨机互传验收。

## 非目标

- 不改调度算法、吞吐策略、TLS/生产认证、物理 NIC、local-bind 或不同物理链路。
- 不验证 V1/V2 运行逻辑、历史 checkpoint 互操作或生产 readiness。
- 不触碰 `/root/projects/GridFlux-Beta`、`/root/projects/CPSS(DCC)`、其他 worktree、凭据和生产配置。

## 验收标准

1. Python 代理和 runner 语法、单测通过；新的四步计划只能按顺序推进，不能跳过故障或重复注入。
2. N=0/1/2/3 时分别恢复 0/1/2/3 次并明确预算耗尽；N=4 时四次均恢复，上传和下载最终树哈希与源一致。
3. 所有通过场景的 `control_reconnect_count=0`，lane 控制重连次数与控制故障数一致，健康 lane 不重复提交已完成文件，四次 reset 共用一个 batch ID。
4. 深圳、上海从同一提交完成 Release/TLS/io_uring 构建，相关 CTest 和完整 CTest 通过；二进制 SHA-256 一致后才进行跨机传输。
5. 任务启动的代理、临时服务、监听端口和测试 payload 清理，保留小型 summary、matrix 和日志证据。

## 允许修改的文件

- `tools/test/cpnetflux_fault_proxy.py`
- `tools/test/run_cpnetflux_lane_recovery_interop.py`
- `tools/test/test_cpnetflux_fault_proxy.py`
- `CMakeLists.txt`
- `docs/coordination/BOARD.md`
- `docs/tasks/2026-10-20-v3-composition-stability-01.md`
- `docs/tasks/2026-10-20-v3-composition-stability-01-evidence.json`

## 下一步交接

若本轮通过，继续 V3-only 稳定候选验收；若失败，按“代理/断言问题”与“生产恢复行为问题”分开记录，不用测试失败直接推断产品缺陷。
