# V3-COMPOSITION-STABILITY-01

Date: 2026-10-20 (Asia/Shanghai)
Status: complete
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

## 实际完成

- 故障代理支持任意长度的连续 data/control 计划，本轮新增四步序列 `data#2 -> control#3 -> data#4 -> control#5`。
- runner 新增 `sequential-mixed-long` 类型和 CTest 入口，覆盖两端服务端、上传/下载、N=0..4。
- 单测增加长序列不能跳步、不能重复 claim 的断言；本轮没有修改生产 C++ 恢复状态机。

## 验收证据

- 固定输入提交：`a596970c636b32a05789bb90f663150928e6f897`；源码归档 `/tmp/cpnetflux-runs/V3-COMPOSITION-STABILITY-01/source-a596970.tar.gz`，SHA-256 `c5a6932e86acb00579b97b4d8366df04e46273e7b8ace1684b318aadeba30212`。
- 深圳和上海均使用 Release、TLS ON、io_uring ON、desktop OFF、tests ON、cross-domain ON 构建；六个关键二进制 SHA-256 完全一致，server 为 `e952ea928fd6c7753df6cbe4b558e829232df40b481d0f36cbbe4a860827f412`。
- Python 语法和代理单测 `12/12` 通过；深圳本地 lane recovery/fault proxy 相关 CTest `2/2` 通过。
- 两端非跨域完整 CTest 均 `391/393` 通过；唯一失败是既有 `cpnetflux_gridftp_control_token_smoke` 和依赖它的 `cpnetflux_event_log_smoke`，原因是环境未提供 `CPNETFLUX_TEST_TOKEN`，不是本轮代码失败。
- 新增跨云 CTest `cpnetflux_lane_composition_stability_interop` `1/1` 通过，用时 `267.90` 秒，固定矩阵 `/tmp/cpnetflux-runs/V3-COMPOSITION-STABILITY-01/shenzhen-cross-build/cpnetflux-lane-composition-stability-interop-evidence/20261019T171927Z-1792430367115390883/matrix.json`，SHA-256 `404e43e2bf8ae358a5c90353c961461788263c75f5ea43ed78a66738da9e515f`。
- 矩阵共 `20/20`：深圳/上海服务端 × 上传/下载 × N=0/1/2/3/4。N=0..3 分别恢复 0..3 次并明确失败；N=4 四次均恢复，最终树哈希一致，成功摘要中的 `lane_control_reconnect_count=2`、`control_reconnect_count=0`、`data_connect_count=6`、`persistent_lane_range_completed=[2,2]`。
- 临时代理、服务、监听端口、任务 payload 和上海解压源码已清理；构建目录、矩阵、summary、proxy/server 日志保留。正式 `cpnetflux-demo.service` 未被本任务改动。

## 边界

- 本轮证明的是 V3 软件层连续混合故障下的共享预算、lane 控制恢复和最终完整性；没有证明吞吐、production readiness、物理 NIC、local-bind、不同物理链路或上海直连深圳生产 `TCP/2121`。

## 下一步开发计划

`V3-RELEASE-STABILITY-01`：把组合恢复矩阵与取消、超时、服务重启、损坏 checkpoint、目标冲突和重复提交放到一个 V3-only 稳定候选门禁中，解决“单项故障都通过但组合操作仍可能互相影响”的剩余问题。继续遵守先同步源码、两端构建和哈希一致后再互传的规则；V1/V2 运行兼容不重新引入。
