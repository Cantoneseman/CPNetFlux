# LOWLEVEL-CLOUD-PREFLIGHT-01：深圳—上海 100 Mbps 验证准入只读复核

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：05 云端运维与发布；验收角色：00
- 目标及理由：为后续验证已审查的单文件底层候选及目录路径，收口深圳—上海现有环境的活动进程归属、空间预算、可用端口及两种传输实现的认证/数据 TLS 先决条件；只产出准入结论，不启动性能工作。
- 非目标：不构建、不传输、不启动 GridFTP/CPNetFlux、不安装软件、不创建云端运行目录、不清理、不停止进程、不触碰历史 GridFlux 工作树、`/root/projects/CPSS(DCC)` 或 science-compressor，不读取或输出密钥、口令、token、私钥、完整 SSH 配置或可能含凭据的进程命令行。
- 输入 commit（完整 SHA）、工作树状态：本地当前文档 HEAD 以执行时 `git rev-parse HEAD` 记录；云端历史树保留原样。后续实验若准入，必须另用已验收完整提交和逐端归档/二进制 SHA，不用云端 dirty HEAD。
- 必读资料和证据路径：本任务；`docs/coordination/ENVIRONMENT.md`；`docs/tasks/2026-09-23-env-preflight-02-result.md`；`docs/tasks/2026-09-23-env-space-plan-01.md`；`docs/tasks/2026-09-23-lowlevel-sendv-impl-01-result.md`；`docs/tasks/2026-09-23-lowlevel-tree-stage-profile-01-result.md`。
- 允许修改的文件/目录；禁止修改的资源：05 只新增 `docs/tasks/2026-09-23-lowlevel-cloud-preflight-01-result.md` 和 CLI `--output-last-message` 摘要；远端仅可执行经过审查的只读查询；不得写云端文件或仓库状态。
- 工作分支/worktree：本任务只读，不切分支、不操作 index。
- 前置条件、环境占用和停止条件：使用既有 SSH alias 与严格 host-key 校验；必要时沿用此前已验证的系统 OpenSSH 调用方式并记录客户端路径/版本。若 PID 无法在不读取 cmdline/environ 的情况下确认归属，保持实验 blocked，不猜测、不停止、不覆盖。空间快照须带时间；每个实际输出挂载点分别预算，不能把 `/` 与 `/tmp` 相加。不得因 SSH 可登录就判为实验准入。
- 验收标准及实际可执行命令：重测两端 hostname、df(/,/tmp 和目标相关挂载点)、inode、内存/CPU、历史树 HEAD/status 条数；只用 `ps -o pid,ppid,user,comm,lstart`、`/proc/<pid>/comm`、父子 PID 与 cgroup 等不回显 cmdline/env 的方式调查此前未知 PID，不能证明归属就清楚标未解决；查询 listener/PID 并避让现有控制/数据服务范围；仅从不含凭据的服务状态/文档确认 GridFTP 与 CPNetFlux data TLS/认证需求。给出下一步小矩阵的实际字节峰值预算和每盘保留 ≥10 GiB 后可用余额。报告 `READY/BLOCKED`，逐项引用原始只读命令与退出码；本任务无 transfer/integrity/hash 结果。
- 云端运行目录、端口、资源上限、清理与归档方案：本任务不创建/使用目录、不分配/绑定端口、不清理。仅给下一步候选隔离根和端口规划；任何活动状态、服务安全模式或峰值预算未知都继续阻塞。
- 预期产物路径：`docs/tasks/2026-09-23-lowlevel-cloud-preflight-01-result.md`、`docs/tasks/2026-09-23-lowlevel-cloud-preflight-01-last-message.md`。

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务和指定环境资料，只做安全的只读 SSH 盘点。新鲜快照与活动进程归属、峰值资源/端口、认证和 data TLS 证据全部写明；若不能安全确认归属或配置，不要猜测，保持 blocked。不要构建、传输、创建目录、删除/清理、停止服务或读取凭据。完成后只写任务结果和 CLI 摘要。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- Transfer / integrity / evidence / wire accounting：本只读任务均未运行，不适用。
- 失败/跳过/阻塞及其原因：
- 剩余风险、未完成事项：
- 下一角色可直接执行的下一步：

## 验收与路线变化

验收人、结论和依据：
