# V3-LANE-DATA-FAULT-RECOVERY-01：数据连接断开后的快速恢复

- 状态：实现与验收命令已完成，待独立质量复核；不表示生产 readiness。
- 路线/任务版本：R2026-10-16-V3-UNIFIED-MULTILINK / V3-LANE-DATA-FAULT-RECOVERY-01。
- 权威开发端：深圳 gridflux-beta-shenzhen；上海仅作传输对端。
- 任务输入提交：4af82db824d7d4a7c02accb4f63a40b62e3caa9a；本次最终双云测试输入：7d0787b984e164abe4087713c08847bfde2795c9。
- 实现 worktree/分支：/root/projects/CPNetFlux-worktrees/v3-lane-data-fault-recovery-01 / codex/V3-LANE-DATA-FAULT-RECOVERY-01。

## 这次解决了什么（易懂版）

以前，传文件用的某条数据连接断了，如果它发生在其他通道还没准备好时，服务端不一定能及时察觉，会傻等到 30 秒超时。更严重的是，如果一个已经传完的文件在等待期间被改坏，程序也可能只报告网络超时，没有指出文件内容已经损坏。

现在等待阶段会同时检查控制连接和数据连接，发现哪条 lane 断开就只让那条 lane 退出并重连。重试之前还会重新核对已经提交的文件和分块摘要：文件完整就继续恢复；文件已损坏则尽快明确失败，不会把错误伪装成网络重试。通俗地说，程序现在能更早发现“哪根传输线断了”，并确认已经到达的文件没有被悄悄改坏。

## 范围、非目标与验收标准

- 包含：data-only reset 的上传/下载快速恢复；批次/lane 生命周期；数据完成但终端控制回复丢失时的重试层选择；已提交输出完整性复核；预算消耗和错误结果可核验。
- 不包含：物理 NIC/local bind、源地址/路由/交换机或多物理路径验证；吞吐优化；生产 TLS/认证 readiness；全量生产 readiness 声明。
- 验收：编译成功；单元和相关 CTest 通过；本地故障矩阵中正常恢复后的树校验一致且预算有界；损坏输出在 3 秒内以 committed-output checksum mismatch 明确失败，不能等 30 秒 deadline；深圳、上海分别作服务端时，上传和下载在真实两机 TCP 链路上完成同一检查。

## 改动文件

- CMakeLists.txt
- include/cpnetflux/core/io/persistent_directory_batch.h
- include/cpnetflux/core/io/persistent_tree_transfer.h
- src/core/io/persistent_directory_batch.cpp
- src/core/io/persistent_tree_transfer.cpp
- src/core/io/tree_transfer_client.cpp
- src/protocol/control/control_server.cpp
- tests/unit/dynamic_file_queue_test.cpp
- tests/unit/persistent_data_session_test.cpp
- tools/test/run_cpnetflux_lane_recovery_interop.py
- tools/test/run_cpnetflux_reconnect_smoke.py

## 根因与修复

严格复验发现，原 upload-fixed-corrupt 只检查命令是否非零退出，因此把“等到 30 秒传输期限后失败”误认为完整性检查成功。收紧后的红测证实：一个数据 socket 在批次 waitReady 阶段重置，批次等待器过去只检查控制 socket，没发现已接受的数据 socket 已断开；已提交文件的 checksum audit 又位于 waitReady 之后，因而也没机会执行。

修复后，等待器用不消费应用帧的 socket peek 检查数据连接，并把断开定位到具体 lane。waitReady 对故障 lane 返回可恢复的 transport error；服务端在重挂之前审计已提交输出。checksum 不匹配时返回明确的完整性错误并终止此批次；内容完整时仅重置该 lane，保留健康 lane 和已提交分块。对应单元回归测试验证“早期 reset 快速发现且可重挂”。

## 验收证据

- 构建：cmake --build /tmp/cpnetflux-runs/V3-LANE-DATA-FAULT-RECOVERY-01/build -j4，退出码 0。
- 完整单元：cpnetflux_unit_tests，344/344 通过，耗时约 1.3 秒；日志 /tmp/cpnetflux-runs/V3-LANE-DATA-FAULT-RECOVERY-01/unit-final-completion.log。
- 定向 CTest：cpnetflux_data_fault_recovery_smoke、cpnetflux_committed_output_integrity_audit_smoke、cpnetflux_terminal_reply_recovery_smoke，3/3 通过。
- 本地故障矩阵：tools/test/run_cpnetflux_reconnect_smoke.py，17/17 按预期通过；已损坏文件场景要求 3 秒内明确报错。Python py_compile 与 git diff --check 通过。
- 双云互传：测试矩阵输入提交 7d0787b984e164abe4087713c08847bfde2795c9，运行号 20261018T201443Z-1792354483426917865；证据文件 /tmp/cpnetflux-runs/V3-LANE-DATA-FAULT-RECOVERY-01/interop-final-early-reset/20261018T201443Z-1792354483426917865/matrix.json。
  - 上海作服务端：上传 15.93 秒、下载 9.88 秒，均成功。
  - 深圳作服务端：上传 7.34 秒、下载 12.60 秒，均成功。
  - 四项的实际树摘要与预期一致：SHA-256 5816ca45fdc866157f2c8c9d0097009af11a3132eee66cd2fcdd745ab696c092，3 个文件、67,230,715 字节。每项都观测到 1 次 lane-local recovery，预算为 2，data connect count 为 3，错误码为 ok。
- 测试前核对深圳 /tmp 约 48 GiB 可用、上海约 79 GiB 可用；测试专用端口 24600–24799 均无监听。实际服务端口 2121 只做只读观察并保持运行，未写入其数据目录。测试进程结束后没有本轮专用监听进程。
- 证据只证明当前构建在这些定向故障注入下可恢复并保持内容一致；不证明物理多链路、吞吐提升或生产 readiness。

## 独立复核状态

质量角色派单曾被服务端返回 429 Too Many Requests；CLI 续接随后报告同一会话已有 active writer。状态检查未返回独立审查内容。因此本记录不声称已通过独立代码审查；实现和运行验收结果可复核，独立质量意见仍待回收。

## 下一阶段计划：V3-LANE-MULTI-FAULT-BUDGET-01

易懂地说：这次验证的是“一条 lane 断开时，能否只修复这一条”。下一步要验证“两条 lane 几乎同时断开时，程序会不会把同一次重试名额算两遍，或者超出用户设定的总重试次数”。

计划先增加可重复的双 lane 故障注入，再用 N=0、1、2 三种总预算测试上传和下载，并让深圳、上海轮流作服务端。验收要求是总恢复次数始终不超过 N；N=0 不重试；N=1 两条 lane 同时要求恢复时最多只恢复一次并明确失败；N=2 可完成两次恢复且最终树摘要一致。若测试证明现有代码不满足，再做最小修复；不能先假定代码一定有错。

不包含物理网卡、真实不同物理链路和吞吐优化。具体任务单将在本阶段提交并推送后另立隔离 worktree。
