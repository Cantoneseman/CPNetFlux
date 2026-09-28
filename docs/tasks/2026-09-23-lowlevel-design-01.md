# LOWLEVEL-DESIGN-01：单文件与目录底层优化选型

- 状态：in_progress
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：04 测试与质量
- 目标及理由：把已复核的历史并发响应和当前 C++ 调用路径转成一项可实施、可度量的底层/架构方案，分别服务单文件与目录传输，避免将源码上的可能成本误写成已证明瓶颈。
- 非目标：不修改源码、测试、默认配置、wire protocol、runner、决策/任务板；不创建 worktree、不构建、不运行 benchmark/smoke/实验；不 SSH、不清理云端、不访问受保护路径。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。执行前核对 HEAD；共享文档和任务文件有其他未提交修改，暂存区不得操作。
- 必读资料和证据路径：本任务；`docs/DECISIONS/2026-09-23-lowlevel-design.md`；`docs/tasks/2026-09-23-hist-forensic-01-result.md`；`docs/tasks/2026-09-23-hist-qa-01-result.md`；`docs/tasks/2026-09-23-dir-perf-source-01-result.md`；按需读取实际涉及源码及现有测试。
- 允许修改的文件/目录；禁止修改的资源：仅新增 `docs/tasks/2026-09-23-lowlevel-design-01-result.md`。禁止写角色回执、BOARD/ROSTER、源码、测试、runner、原始证据或云端。
- 工作分支/worktree：只读设计，不创建分支/worktree，不切换分支，不操作 Git index。
- 前置条件、环境占用和停止条件：依赖上述历史取证、04 复核、03 源码映射。若输入 HEAD、实现基线或源码映射存在变化，指出差异并停止以旧事实推导；不做远端动作。
- 验收标准及实际可执行命令：对比跨文件 framed data session/connection reuse、worker 文件流水化/控制往返重叠、明文 vectored DATA write、manifest checkpoint 等候选。对单文件与目录分别给出推荐次序、收益机制、证据等级、量化指标/可证伪结果、复杂度、TLS/认证、协议兼容、失败/取消/resume/重试语义、测试与回退。指出哪些收益需要真实 100 Mbps 跨域测试才能确认、哪些可在本地机制测试。必须作出有条件但明确的首选方案，不能只罗列选项。执行 HEAD/status、定向源码读取、`git diff --check`；不运行代码。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用，禁止 SSH/构建/实验。
- 预期产物路径：`docs/tasks/2026-09-23-lowlevel-design-01-result.md`。

## 派给角色的消息

请续接 ROSTER 中现有 03 实现聊天。先核对 HEAD 和结果文件；只读完成候选架构比较，在结果文件交付上述验收内容。重点调查跨文件复用数据 socket/session 能否兼容当前 transfer ID、每连接状态和 framed protocol；说明升级/回退及 TLS/resume 风险。单文件需单独分析 vectored 明文发送、buffer/chunk 或其它有机制依据的选择，避免默认 SENDV。结果完成后通过 CLI `--output-last-message` 写入不同的 `docs/tasks/2026-09-23-lowlevel-design-01-last-message.md`，避免摘要覆盖报告。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 单文件首选方案/证据边界：
- 目录首选方案/证据边界：
- 兼容、失败恢复、TLS、校验/resume 风险：
- 失败/跳过/阻塞及原因：
- 下一角色可直接执行的下一步：

## 验收与路线变化

04 独立审查；00 验收。未通过前不派发实现任务。若设计要求改变 wire/持久化语义，先冻结版本化规格与兼容策略，再开始实现。
