已完成 v2 修订计划：

- [修订结果](D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-revision-04-result.md)
- [摘要](D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-revision-04-last-message.md)

已闭合 QA-03 三项阻塞：

- 删除稳定控制连接 ID 依赖，候选仅持有进程内 opaque 句柄。
- eligibility 严格限定为 `Pending`，并覆盖方向、路径、指纹和 metadata 校验。
- 统一 worker 领取、candidate reservation、取消归还和 handoff 消费；明确替换 `runTreeScheduler` 中的裸 `state.nextIndex++`。

门禁结果：HEAD 未变、暂存区为空、固定源码 parity 通过；`control_id/controlId` 扫描无匹配；PLAN-03 哈希保持不变；文档 UTF-8/LF/尾随空白检查和 `git diff --check` 均通过。当前源码仍保留原有递增点（未修改），已明确为后续实现替换接缝。构建、测试、传输、SSH 和实验均为 `NOT_RUN`，等待 04 独立复审。