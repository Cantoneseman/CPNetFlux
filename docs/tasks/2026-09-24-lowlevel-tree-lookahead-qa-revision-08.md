# LOWLEVEL-TREE-LOOKAHEAD-QA-REVISION-08

## 目标

独立验收 03 的 revision-08 修复，确认 lookahead handoff 重复请求语义正确，且不回归资源归属、取消竞态和默认关闭行为。

## 输入

- 代码 worktree：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`
- 分支：`codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`
- 修复回执：该 worktree 的 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-08-result.md`
- 既有运维证据：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-08-result.md`

## 范围与限制

只读审查，不改代码、任务板或角色回执，不提交 Git，不启动性能实验、不 SSH 清理。可在已存在的深圳固定 Linux 构建/工作目录中运行定向 CTest；若无法使用，必须记录原因。

## 验收

1. 静态确认 `consumeCandidate` 身份校验先于重复判定；同身份重复返回 `AlreadyConsumed`，错误 owner/generation/fingerprint 返回 `Stale`。
2. 在 Linux 上运行 lookahead 定向测试，目标 #180/#181 通过；同组测试无新失败。
3. 检查默认 depth=0、unsafe budget/socket gate、cancel/failure/release 相关用例未回归。
4. 输出 PASS/FAIL/BLOCKED，附命令、退出码、证据路径和剩余限制。

交付 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-qa-revision-08-result.md` 与 `...-last-message.md`。
