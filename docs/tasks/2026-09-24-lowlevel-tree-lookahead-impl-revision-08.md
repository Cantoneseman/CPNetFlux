# LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-08

## 目标

修复 05 在规范化 Linux 构建中复现的两个真实单元测试失败：重复 handoff 应返回 `AlreadyConsumed`，同时继续拒绝不同 owner/generation/fingerprint 的陈旧请求。

## 固定输入与范围

- 输入实现分支：`codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`
- 独立 worktree：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`
- 允许修改：`src/core/io/tree_lookahead.cpp`、`tests/unit/tree_lookahead_test.cpp`；可写本任务回执
- 禁止修改协议、网络路径、默认 lookahead 深度、其他模块或共享 Git index；不提交 Git，不启动云端实验

## 验收

在 Linux 固定构建上运行 lookahead 相关 CTest，覆盖 full-control 和 metadata-only 的首次 handoff、同请求重复 handoff、错误 owner/generation/fingerprint；既有资源归属和取消/失败测试不得回归。记录命令、退出码和剩余限制。

## 交接

完成后写 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-08-result.md`，通知 00；随后由 04 做独立质量复核。
