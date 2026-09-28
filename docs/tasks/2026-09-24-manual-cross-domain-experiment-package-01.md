# MANUAL-CROSS-DOMAIN-EXPERIMENT-PACKAGE-01

## 目标

准备一个由用户手动启动的深圳↔上海 100 Mbps 跨域性能实验脚本包，覆盖单文件和多文件目录，避免总指挥轮询长时间进程。

## 当前阶段

只准备脚本和使用说明，不启动 SSH 实验、不创建远端实验目录、不生成大 payload、不修改协议实现。脚本交付后必须标为 `READY_PENDING_DYNAMIC_QA`，只有 05 的 revision-09 固定构建与 04 动态验收通过后，00 才能宣布可运行。

## 固定实验口径

- 端点：深圳 `gridflux-beta-shenzhen`、上海 `gridflux-beta-shanghai`；保留现有 GridFTP 服务和端口，避让已知服务范围。
- 对照：CPNetFlux 与真实 GridFTP；单文件和 dense/mixed 目录至少各一组，方向和重复次数由脚本参数显式冻结。
- CPNetFlux 性能基线：POSIX、worker control reuse、scheduler off、compression off、checksum none、fresh/no resume；lookahead depth 0 与获准的 depth 1 A/B 分开运行。
- 完整性：性能路径可关闭内置完整校验，但每个 case 仍保留独立 SHA-256/manifest 证据；失败、blocked、skipped 不补成成功或零耗时。
- 记录固定 commit、源码归档 SHA-256、两端二进制 SHA-256、环境、命令、seed、配置、PID、磁盘前后和证据回收路径。

## 脚本要求

1. Windows PowerShell 入口，显式使用系统 OpenSSH `C:\Windows\System32\OpenSSH\ssh.exe`，BatchMode 与 StrictHostKeyChecking 保持开启；不打印凭据/token。
2. 启动前只读 preflight：两端空间、挂载点、旧 GridFTP PID/端口、run root 归属、输入 commit/hash 和工具链；任一门禁失败立即退出，不启动实验。
3. 提供 `-Mode pilot|full`、`-Direction both|shenzhen-to-shanghai|shanghai-to-shenzhen`、`-LookaheadDepth 0|1`、`-Repeats N` 等显式参数；默认只生成命令计划，不默认执行。
4. 长任务由用户执行一次后脚本自行完成批次；每个 case 写状态文件和独立日志，支持中断后根据状态识别已完成 case，但不得自动重跑未知状态或覆盖旧证据。
5. 结束时生成 summary、失败/跳过清单、源码/二进制/结果 SHA-256 和磁盘/PID 清理报告；清理前确认 manifest/hash/summary 已持久化。大 payload 不回传 Git。
6. 使用说明明确：何时可以运行、如何人工停止、停止后保留哪些证据、如何把结果目录交给 00；脚本本身不上传凭据、不触碰 `/root/projects/GridFlux-Beta` 或 `/root/projects/CPSS(DCC)`。

## 允许修改

- `tools/experiments/gridftp_compare/` 下新增手动入口脚本及其小型配置/说明；
- `docs/tasks/2026-09-24-manual-cross-domain-experiment-package-01-result.md`；
- 不修改传输实现、现有实验原始证据、BOARD/ROSTER，不提交 Git。

## 验收

- PowerShell 语法检查和 `-Mode plan` 本地演练通过，不连接云端、不创建 payload；
- 生成的计划包含单文件与目录 case、CPNetFlux/GridFTP、方向、重复、lookahead A/B、seed 和所有输入哈希字段；
- 失败/blocked/skipped 语义、证据目录和人工交接步骤可读；
- 明确列出动态 QA 尚未通过时的阻塞，不宣称脚本可立即运行。

交付脚本、使用说明、回执和 CLI 摘要；完成后等待 00 在动态 QA 通过后放行。
