# LOWLEVEL-TREE-LOOKAHEAD-IMPL-05 独立质量验收

## 目标

对 03 已完成的低层 tree lookahead 窄实现做独立质量验收。输入固定为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`，路线版本 `R2026-09-24.12/v1`。

## 实现位置

- Worktree：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`
- 分支：`codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`
- 实现回执：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-05-result.md`

不得修改该 worktree、共享主工作树、BOARD、ROSTER 或云端；只写本任务结果和摘要。

## 验收范围

1. 核验白名单与禁止修改路径，确认共享主工作树和 index 未被碰触。
2. 在可用 Linux 环境配置并编译；运行新增 lookahead/options 单测、受影响 tree 单测、完整 CTest 和必要 loopback smoke。若环境不可用，明确 `NOT_RUN/BLOCKED` 及原因。
3. 静态审查 `runTreeScheduler` 的 claim/reserve/handoff/cancel/finish 接缝；确认 worker 与 global scheduler 行为边界、严格 Pending、metadata fingerprint、owner/generation、候选失败回退、重复领取和空 manifest 不会死循环或重复处理。
4. 审查候选阶段不得发送文件控制命令、创建 data FD、写 manifest/event/summary 或生成 transfer ID；确认 `control_reuse=worker` 只 metadata 预热，`off` 才建立普通控制连接，默认 depth=0 行为不变。
5. 动态或静态确认候选线程异常、scheduler stop、TLS/认证失败、resume/changed、checksum/manifest 失败、并发重叠和资源门不会破坏原始语义。
6. 不做性能结论，不 SSH，不提交；报告明确剩余风险和是否允许进入 05 固定 Linux 构建任务。

## 证据要求

结果写入 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-qa-05-result.md`，摘要写入同名 `-last-message.md`。记录实际命令、退出码、测试清单、未运行限制、输入 HEAD、worktree、静态/动态结论。不得把静态通过写成产品或性能通过。
