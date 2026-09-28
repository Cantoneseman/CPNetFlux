# LOWLEVEL-TREE-DENSE-PROFILE-LOCAL-01：密集目录本地阶段画像

- 路线/任务版本：R2026-09-24.9 / v1
- 执行角色：00 总指挥；验收：00 总指挥，04 对 telemetry 实现的独立复核并行进行
- 目标：在本机 WSL2 Linux 的固定 CPNetFlux telemetry 候选上，观测 dense 目录上传/下载在 1 与 8 file workers 下的阶段分布，为目录数据路径架构优化选型提供机制证据。
- 非目标：不改源码、不改协议、不启动深圳/上海/广州服务器、不 SSH、不跑 GridFTP 对照、不宣称 WAN/100Mbps 收益、不清理历史证据、不提交 Git。
- 输入：遥测候选源归档 SHA-256 `04fef0a5f848afd1d9c4148e93a9dec415339c448ebdf20724b2d2666dd31d1e`；build `cpnetflux-tree-upload-client` SHA-256 `07ecbd9d3af4c9ae787c4f03df7b582d5f59f8a35acd5213d35b3449117009d4`，download client `dfdcb494e40a052686c10a1244bc6b026fa7ceddee60af0630917853564bc7a4`；源码实现基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 加 telemetry 候选改动。构建位于 `/home/sumu/CPNetFlux-LT-20260924/build`。
- 固定环境/配置：WSL2 Ubuntu-22.04、ext4 本地文件系统、loopback；确定并报告 CMake/g++/内核/CPU；scheduler off、compression off、checksum none、POSIX、control reuse worker、1 connection per file；telemetry on。该矩阵只作本机机制剖析。
- 数据集：固定 seed `CPNetFlux/dense-128x1MiB/v1`；128 个各 1 MiB 文件，共 128 MiB；记录生成算法与完整 tree SHA-256。每个 case 新建服务根和下载目标，不复用旧 payload。
- 矩阵：upload/download × file-parallelism 1/8 × 3 次，共 12 个 case。记录命令、wall、退出码、源/目标 hash、stage JSONL、每格 count/sum/median/p95/max 与不适用/缺失数量。失败、无阶段或 hash 不匹配单独列明，不补零、不纳入速度比较。
- 验收：所有 case 均 exit 0 且源/目标 tree SHA 一致；每个阶段从原始事件复算且过滤起始/null duration；报告实际计时边界。阶段 sum 不视为互斥 wall，12 个本地 case 不外推跨域或因果。
- 证据目录：`D:\Project\CPNetFlux-evidence\LOWLEVEL-TREE-DENSE-PROFILE-LOCAL-01`。保留生成/执行脚本、环境摘要、每 case stdout/stderr、原始 JSONL、summary JSON/CSV 和 SHA-256 manifest，不保留测试 payload。
- 唯一仓库写入：本任务单及完成后 `docs/tasks/2026-09-24-lowlevel-tree-dense-profile-local-01-result.md`；不改 BOARD/ROSTER（任务结果交付后由 00 整合）。无 git add/commit。

## 本地执行脚本

脚本由 00 写入上述外部证据目录并在 WSL2 构建/运行；保留退出状态及日志。开始运行前检查候选二进制 SHA 未变。完成后验证证据文件清单 SHA-256、JSON 可读及 12 个 case 数量。

## 解释边界与交接

此任务测量的是 WSL2 loopback，不提供网络 RTT/丢包/限速条件，也不是质量最终验收。若结果显示固定阶段占用明显，只形成下一项低层架构任务的候选；需由 01/04 核对契约后再实现。若仅观察到传输或 runner wall 变化，不能从相关性宣称某阶段是因果根因。云端仍须由 05 重新执行准入和安全预检后才能计划。
