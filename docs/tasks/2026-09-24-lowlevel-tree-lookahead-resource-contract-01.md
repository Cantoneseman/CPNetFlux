# LOWLEVEL-TREE-LOOKAHEAD-RESOURCE-CONTRACT-01

- 状态：ready
- 路线版本、任务版本：`R2026-09-24.15 / v1`
- 发起人：00 总指挥；执行角色：01 架构与需求；验收角色：00 总指挥，后续由 04 独立审查
- 目标及理由：根据 QA-REVISION-09 发现的 fail-closed 门，收口 lookahead 候选内存、预连接线程、TLS/控制对象和文件描述符的可审计资源边界。已有设计规定候选状态 1 MiB/个、合计 2 MiB，但当前 `tree_transfer_client.cpp` 将 `reliableCandidateMemory` 固定为 false，且候选准备使用 `std::thread`。交付一个能直接指导实现和独立验收的决定；若不能证明符合上限，明确保持 effective depth=0 并暂停本功能性能实验。
- 非目标：不改源码/测试/runner/协议、wire、manifest/resume、默认 control reuse 或外部 schema；不 SSH、不构建、不跑传输/性能实验、不改云端、不提交 Git；不把候选性能说成已验证。
- 输入 commit（完整 SHA）、工作树状态：项目资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；实现基底 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 上的未提交源码快照位于 `build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`，含 5 个 tracked 修改、3 个 untracked 源文件。该快照的深圳证据包 SHA-256 为 `2AF5C2DC166615FD4F0924BA335408624E8620B437B07B3528A98E08674A07C6`；不得称其为完整 Git commit。
- 必读资料和证据路径：本任务；`docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`；`docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-05.md`；`docs/tasks/2026-09-24-lowlevel-tree-lookahead-qa-revision-09-result.md`；`docs/coordination/BOARD.md`；快照中的 `src/core/io/tree_transfer_client.cpp`、`src/core/io/tree_lookahead.cpp`、`include/cpnetflux/core/io/tree_lookahead.h` 和 `tests/unit/tree_lookahead_test.cpp`。
- 允许修改的文件/目录；禁止修改的资源：只新增 `docs/DECISIONS/2026-09-24-lowlevel-tree-lookahead-resource-contract.md` 和本任务 `...-result.md`。不得改 BOARD、ROSTER、源码、测试、任务单、旧报告或证据包；不得进入云端历史项目目录或 `/root/projects/CPSS(DCC)`。
- 工作分支/worktree：只读当前项目资料；不创建分支/worktree。
- 前置条件、环境占用和停止条件：QA-REVISION-09 已独立判定 lookahead 性能实验 blocked，因为显式 depth=1 当前仍变成 effective depth=0。设计 revision-02 已定候选/FD 硬上限。若线程栈/TLS/系统分配无法给出可靠上界，不得以 RSS 采样、平均值或“通常很小”替代，不得建议简单把门置 true；停止在 fail-closed 方案。
- 验收标准及实际可执行命令：1) 决策逐项定义预算包含/排除对象及理由，覆盖 `Candidate`/fingerprint 字符串容量、`CandidateRuntime`/shared ownership、准备线程栈与 TLS/控制库对象；2) 给出上界可计算、失败回退 depth=0 的实现策略，或明确本机制在当前架构下不满足上限并建议停用/重选优化；3) 保留 `depth<=1`、每 worker candidate<=1、pending control<=min(worker_count,2)、extra FD<=2、data FD=0 和取消/失败清理约束，任何变更需量化依据；4) 列出实现文件白名单、资源计数点、测试向量、独立质量门以及何时才可申请跨域 A/B；5) UTF-8 文档门禁和 `git diff --check` 通过，记录实际命令与输入状态。文档任务不运行 CMake/CTest。
- 云端运行目录、端口、资源上限、清理与归档方案：不适用；禁止云端操作。
- 预期产物路径：`docs/DECISIONS/2026-09-24-lowlevel-tree-lookahead-resource-contract.md`、`docs/tasks/2026-09-24-lowlevel-tree-lookahead-resource-contract-01-result.md`，CLI `-o` 另写同名前缀 `...-last-message.md`。

## 派给角色的消息

请续接 ROSTER 中既有 01 架构与需求聊天，阅读本任务及明确列出的资料，只输出资源契约决策和执行回执。先区分“候选状态堆内存”与“准备线程栈/TLS/控制库分配”等真实资源，判断原 1/2 MiB 上限是否适用。不能证明上界时保持 fail-closed，并把下一步改成更可测的替代方向；不要通过扩大数字、读取进程 RSS 或复述设计来假装解决。不得委派他人或修改共享任务板。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 做出的资源预算决定及依据：
- 失败/跳过/阻塞及其原因：
- 剩余风险、未完成事项：
- 下一角色可直接执行的下一步：

## 验收与路线变化

00 核对决定与代码事实；04 后续独立审查。若 lookahead 资源上限不可实现，则保留原实验阻塞并由 00 选择已观测支持的其他底层优化，不覆盖旧构建或实验结果。
