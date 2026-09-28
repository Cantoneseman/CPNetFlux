# LOWLEVEL-TREE-LOOKAHEAD-PLAN-REVISION-04

- 任务版本：`R2026-09-24.11 / v2`
- 责任角色：03 核心实现（仅修订计划）；独立复审：04 测试与质量；验收：00 总指挥
- 状态：ready；本任务只允许修改/新增任务结果文档，不授权代码、worktree、构建、测试、SSH 或实验。

## 目标

依据 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-03-result.md` 与 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-qa-03-result.md`，形成可被固定源码机械审查的 v2 窄实现计划，为后续 lookahead 代码任务消除 QA-03 的三个阻塞。

## 固定输入与依赖

- 固定实现基线：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- 当前资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`
- 设计契约：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`
- 设计复审：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-revision-qa-02-result.md`（PASS）
- 原实现计划：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-03-result.md`
- 计划复审：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-qa-03-result.md`（PARTIAL）

## 必须修正

1. 删除计划中所有“既有 `control_id`”及同义依赖。固定源码没有稳定的控制连接 ID；`transferId` 是文件级传输标识，不能充当控制连接身份。计划必须明确候选只持有不可序列化的 opaque `ControlClient`/内部候选句柄（如实现需要），外部 summary、JSONL event、reason、schema 及旧消费者字节完全不变；不得新增 control ID 字段来绕过问题。
2. 候选 eligibility 必须机械限定为 `TreeFileStatus::Pending`，并同时满足方向、规范路径、endpoint/options 指纹和元数据校验。`Completed`、`Changed`、`Failed` 及未知/其他状态不得 reservation、不得推进 index、不得占用 pending；为这些状态补单元测试向量并写清回到原路径的行为。
3. 将普通 worker 领取、候选 reservation、取消归还、handoff 消费统一到一个明确的锁保护 helper/状态结构，针对固定源码当前 `runTreeScheduler` 直接 `state.nextIndex++` 的接缝给出替换/包裹方案。必须写出不变量：同一 manifest index 不能同时被普通领取和候选预留；取消归还不丢失、不重复；handoff 消费后只执行一次；不引入未声明的 manifest 顺序变化。计划不能继续用未接入实际 worker 循环的抽象名称掩盖此接缝。

## 保持不变

继续遵守 REVISION-02 的禁止项：不改 wire/framed、file clients、manifest/checkpoint/resume/checksum、scheduler/compression/IO backend、服务端、默认 control reuse、旧 summary/event schema 或协议命令时序；候选阶段不建 data FD、不发文件级命令；动态工作均 NOT_RUN。

## 交付与验收

- 结果文件：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-revision-04-result.md`；CLI 摘要按派单命令写入同名 `-last-message.md`。
- 结果须标明仅修订计划、没有代码/worktree/构建/测试/SSH/实验。
- 结果必须列出白名单/禁改路径、配置/状态机、两方向 handoff、资源硬门、测试向量和停止条件的修订内容。
- 在固定源码上复核不存在 `control_id` 依赖，`Pending` eligibility 和统一领取 helper 可定位；复核 HEAD、index 为空、UTF-8/LF、无尾随空白、`git diff --check`。
- 若固定源码与计划仍矛盾，标记 BLOCKED，不自行改源码或放宽边界。

## 非目标

不要创建实现 worktree；不要修改 `src/`、`include/`、`tests/`、`CMakeLists.txt`、BOARD、ROSTER 或决策；不要运行 CMake/CTest、传输、性能矩阵、SSH、云端操作或提交 Git。
