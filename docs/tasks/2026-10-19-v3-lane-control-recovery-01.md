# V3-LANE-CONTROL-RECOVERY-01：控制连接断开后的单 lane 恢复

- 状态：done
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-CONTROL-RECOVERY-01
- 权威开发端：深圳 `gridflux-beta-shenzhen`；上海仅作传输对端。
- 输入提交：`5aa0396d3b47bb2fc6d6d403b28ced62b2cd98ab`。
- worktree/分支：`/root/projects/CPNetFlux-worktrees/v3-lane-control-recovery-01` / `codex/V3-LANE-CONTROL-RECOVERY-01`。
- 规划文档提交：`3feb706d85a28a3be87fe344efa955df58e80f3e`，已推送并由 `git ls-remote` 回读。
- 实现提交：`c5352189ef15c6d1f65a09a281b19ef799e92c95`，已推送并由 `git ls-remote` 回读。

## 解决的实际问题（易懂版）

之前如果一条 lane 的数据连接和控制连接同时断开，客户端虽然最终能靠“整批重连”把文件救回来，但会把本来正常的另一条 lane 也暂停并重新挂接。这会造成不必要的重复工作，传输越大、lane 越多，干扰越明显。

本阶段让客户端先只重建发生故障的那一条 lane：重新建立它的控制连接，重新协商能力，再用原来的 batch ID、lane 编号和检查点接回原批次。健康 lane 保持原会话；只有这条 lane 的局部恢复确实失败时，才保留原来的整批兜底。因此现在能区分“局部修复”和“整批重试”，并把两者分别计数。

## 范围、非目标与验收标准

- 包含：动态 persistent tree 的 lane-local control reconnect；沿用同一批次身份、channel/lane 和已有检查点；局部恢复失败时保留整批恢复；单独统计 lane 控制重连；控制连接 RST 故障注入和双向跨云验收。
- 不包含：物理 NIC、local bind、源地址/路由/交换路径配置；TLS/生产认证互操作；吞吐/生产 readiness 结论；重写调度器或服务端批次协议。

验收要求是：深圳和上海分别作为服务端完成一次定向控制连接断开；文件树和 SHA-256 一致；故障 lane 局部恢复一次；lane 控制连接只增加一次；批次级控制重连和全局 reconnect attempts 均为零；健康 lane 的完成/range 状态保留。

## 先失败、再修复的证据

先用旧客户端运行新控制连接故障代理，故障代理确实只命中了目标 lane 的控制连接，但旧客户端最后只能整批恢复：`lane_local_recovery_count=0`、`control_reconnect_count=2`、`reconnect_attempts=1`，控制连接数由 2 增到 5。该基线证据保留在：

`/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-01/baseline/20261018T170848Z-1792343328121209293/shanghai-upload-control/proxy.jsonl`

这证明测试先捕捉到了原问题，而不是只验证新代码的成功路径。

## 实现内容

- `src/core/io/tree_transfer_client.cpp`：控制通道也断开时只 reset 当前 lane 的 ControlClient；重新登录、协商 persistent 能力，并用同一 batch/lane 重新启动目录数据连接；局部失败才进入原有整批兜底；新增 `lane_control_reconnect_count` 统计。
- `tools/test/cpnetflux_fault_proxy.py`：根据 EPSV 返回的数据端口将数据连接映射到对应控制连接，定向 RST 目标 lane 的控制 socket。
- `tools/test/run_cpnetflux_lane_recovery_interop.py`：新增 `--fault-kind control`，校验同一 batch ID/lane 重挂接、健康 lane 只出现一次 XDIRD 且没有 retry attempt，并比较双向树摘要。
- `CMakeLists.txt`、`docs/DIRECTORY_TRANSFER.md`：加入独立 CTest 和统计字段说明。

## 验收命令与结果

配置和构建：

```text
cmake -S /root/projects/CPNetFlux-worktrees/v3-lane-control-recovery-01 -B /tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-01/build -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_DESKTOP=OFF -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_CROSS_DOMAIN_TESTS=ON -DCPNETFLUX_ENABLE_IO_URING=ON -DCPNETFLUX_ENABLE_TLS=ON
cmake --build /tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-01/build --target cpnetflux-gridftp-server cpnetflux-tree-upload-client cpnetflux-tree-download-client cpnetflux_unit_tests --parallel 2
```

两条命令均退出码 0。相关 CTest：

```text
CPNETFLUX_INPUT_COMMIT=5aa0396d3b47bb2fc6d6d403b28ced62b2cd98ab ctest --test-dir /tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-01/build -R "^cpnetflux_lane_(recovery_smoke|recovery_interop|control_recovery_interop)$" --output-on-failure
```

结果为 3/3 通过，用时 79.96 秒；完整单元测试 `cpnetflux_unit_tests --gtest_brief=1` 为 342/342 通过。

双向控制连接故障矩阵：

`/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-01/build/cpnetflux-lane-control-recovery-interop-evidence/20261018T172309Z-1792344189085800644/matrix.json`

两方向都得到同一树 SHA-256：`5816ca45fdc866157f2c8c9d0097009af11a3132eee66cd2fcdd745ab696c092`；3 个文件、67,230,715 字节；`lane_local_recovery_count=1`、`lane_control_reconnect_count=1`、`control_reconnect_count=0`、`reconnect_attempts=0`、`control_connect_count=3`、`data_connect_count=3`、`persistent_lane_range_completed=[2,2]`。故障代理确认目标 lane 复用了同一 batch ID 和 lane 编号，健康 lane 只有一次 XDIRD 且所有文件 `attempt=0`。

实验前后重新检查资源和端口：深圳约 60 GiB、上海约 80 GiB 可用磁盘，均有约 2.5 GiB 以上可用内存和 2 个 CPU；测试端口已释放；深圳 `:2121`/`:22310`、上海 `:2121`/`:2811` 既有服务保持运行。实验输出仅写入 `/tmp/cpnetflux-runs/V3-LANE-CONTROL-RECOVERY-01`，未进入 Git。

## 下一步计划：V3-LANE-CONTROL-RECOVERY-DOWNLOAD-01

现在已经证明“上传时控制连接断开”可以只修故障 lane。下一步要验证同一套恢复逻辑在下载时也安全，重点解决三个问题：

1. 下载端的控制连接断开后，接收端能否用原 batch/lane 身份继续请求剩余 range。
2. 已经写入磁盘的 range 是否不会被重复写坏，未完成 range 是否能从检查点继续。
3. 健康 lane 是否仍保持完成状态，最终下载树 SHA 是否与源端一致。

本阶段会扩展现有跨云故障代理和 CTest，分别运行深圳服务端、上海服务端的 download control reset；如果测试暴露下载专用状态问题，再修改恢复状态机。物理 NIC、local bind 和不同物理链路仍按用户要求后置。