# MCE-TOPOLOGY-02：明确深圳—上海验证端点拓扑

- 状态：in_progress
- 路线版本、任务版本：`R2026-09-23.4`，MCE-TOPOLOGY-02 v1（仅澄清运行拓扑，不放行实验）
- 发起人：00 总指挥；执行角色：02 实验与证据；验收角色：00 总指挥
- 目标及理由：修订手动跨域实验计划包，明确 Windows 是人工启动/控制入口，实际测量数据直接在深圳和上海 Linux 端点间传输；深圳是默认发送端和主要 Linux 验证端。避免把 Windows 本地计划生成误认为深圳远端实验已经就绪。
- 非目标：不 SSH、不创建远端目录、不构建、不生成 payload、不执行传输、不更改协议/实现/BOARD/ROSTER，不解除任何 05/04 实验门禁。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；工作树存在多项其他角色未提交文档和工具产物，只允许改下列白名单。
- 必读资料和证据路径：`docs/DECISIONS/2026-09-23-lowlevel-design.md`；`docs/tasks/2026-09-24-manual-cross-domain-experiment-package-01.md` 及其 result；当前脚本和说明。
- 允许修改的文件/目录；禁止修改的资源：允许修改 `tools/experiments/gridftp_compare/manual_cross_domain_experiment.ps1`、`tools/experiments/gridftp_compare/MANUAL_CROSS_DOMAIN_EXPERIMENT.md`、本任务 result/last-message。禁止改其他文件、代码、旧证据、云端资源和 Git index。
- 工作分支/worktree：仅文档/计划器修改，使用当前工作区，不切分支、不操作 index；保留共享工作区现状。
- 前置条件、环境占用和停止条件：当前仅计划模式，`-Execute` 必须继续 fail-closed。不得为了本任务调用 SSH。若文档与最新 `R2026-09-23.4` 或真实入口行为冲突，报告并停止扩大范围。
- 验收标准及实际可执行命令（不要只写“所有测试”）：PowerShell Parser 零错误；本地 `-Mode plan` 成功；计划中明确 `controller=windows`、数据路径 `shenzhen<->shanghai`、深圳→上海/上海→深圳各自 source/client 与 destination/server；明确 Windows 不在数据路径；确认运行时 SSH 未调用、无 payload/远端目录；`git diff --check` 通过且 HEAD/index 不变。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用，本任务不得连接云端。
- 预期产物路径：更新的计划脚本和说明，以及 `docs/tasks/2026-09-24-manual-cross-domain-experiment-topology-02-result.md`、`...-last-message.md`。

## 派给角色的消息

请在 `D:\Project\CPNetFlux` 读取本任务和引用资料后执行。脚本仍由 Windows 用户手动启动/编排，但测量流量必须由两台服务器直接收发：深圳→上海的 source/client 在深圳，destination/server 在上海；上海→深圳时角色对调。将这些角色明确写入每个计划 case/manifest 与用法说明。当前脚本只有 plan 分支，保持 `-Execute` 关闭；不得声称“深圳侧实验已运行”或调用 SSH。只改白名单，完成后写 result 和 last-message 并报告确切验证结果。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- transfer / integrity / evidence / wire accounting（适用时）：本任务没有传输，均为 not-run。
- 失败/跳过/阻塞及其原因：
- 剩余风险、未完成事项：05 revision-09 固定构建和 04 动态 QA 仍须独立通过并由 00 放行，才可实现/运行远端实验器。
- 下一角色可直接执行的下一步：由 00 验收拓扑计划；实验执行能力另立任务，不得把本计划器当作执行器。

## 验收与路线变化

验收人、结论和依据：

若路线改变：记录替代任务、停止点、需保留的证据，不能覆写旧实验结果。
