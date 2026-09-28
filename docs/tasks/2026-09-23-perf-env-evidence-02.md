# PERF-ENV-EVIDENCE-02：服务归属、持久化与资源证据复核

- 状态：done，证据已交；环境仍 blocked，可释放量 0
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：05 云端运维与发布；验收角色：00、04
- 输入 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；若输入变化，先停止并报告 superseded。

## 目标

在不改变云端状态的前提下，补齐 PERF-ENV-SPACE-PLAN-01 留下的证据缺口：确认上海 PID 768474/25252 监听服务的归属与运行目录，核对相关挂载点空间和批次预算，并只读核验本地历史备份入口是否存在可读的 manifest/hash 索引。形成“可释放量”与“仍未知量”，不自行释放空间。

## 非目标与边界

- 只读 SSH；不停止或启动进程/服务，不删除、移动、清理、覆盖历史目录，不安装软件，不构建、不跑 CTest/runner/实验。
- 完全跳过 `/root/projects/CPSS(DCC)`、science-compressor 和未知项目；不把 dirty `/root/projects/GridFlux-Beta` 当新构建源。
- 不读取或记录凭据、环境 token、完整 cmdline 中的敏感值；若服务归属仍不能确认，明确保留 blocker。
- 只追加 `docs/coordination/receipts/05-operations.md` 的本任务小节；不修改 BOARD/ROSTER 或其他角色文件。

## 必须交付

1. 记录两端带时间的 `/`、`/tmp`、相关运行根和 inode/空间快照；重新计算 `10 GiB + 峰值 payload + build/archive/evidence` 预算，不把约 32 GiB 写成可用实验配额。
2. 对上海 PID/端口给出进程属主、工作目录或可安全确认的服务标识、是否可能占用 CPNetFlux 实验资源；无法确认时列出下一步所需授权/信息。
3. 对 `D:\Project\GridFlux Beta\_server_backups` 做有限只读索引存在性与小型 manifest/hash 可读性检查，不恢复大 payload、不复制凭据；输出证据路径和 SHA-256 是否已核实。

## 验收

- 回执区分已核实、未知、blocked、可释放量；当前没有清理授权，默认释放量保持 0。
- 记录实际 SSH 客户端、命令、退出码和未运行项；不宣称构建/实验就绪。
- 写后验证 UTF-8、`git diff --check`、HEAD 不变、暂存区为空。

## 执行回执

- 已由既有 05 云端运维聊天完成，CLI session `95525` 正常终态，退出码 0。
- 交付位置：`docs/coordination/receipts/05-operations.md` 的 `PERF-ENV-EVIDENCE-02 v1` 小节。
- 结果：深圳约 72.15 GiB、上海约 33.43 GiB，均与 `/tmp` 同盘；上海 PID 768474/25252 已消失，2811 为活动 `gridflux-gridftp-gsi.service`。本地索引和报告可读，但没有上海 payload hash 索引，默认可释放量仍为 0。
- HEAD 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；未构建、未测试、未实验、未清理，暂存区为空，文档门禁通过。
