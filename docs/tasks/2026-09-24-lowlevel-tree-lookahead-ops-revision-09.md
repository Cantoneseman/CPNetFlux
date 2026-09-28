# LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-09

## 目标

用 03 revision-08 的当前实现重新生成规范化源码归档，在深圳隔离运行根构建并验证 lookahead 修复；补齐动态准入证据。

## 固定输入

- 源 worktree：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`
- 分支：`codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`
- 基线提交：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- revision-08 本地 tree_lookahead.cpp SHA-256：`40F9994B4BF8E5BF1003A488ADAD722FE655AE806FB34F032C71EB5F8DC08A44`

## 允许范围与边界

- 只读复核两端资源/服务，使用深圳隔离根 `/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-09`；上海只做资源快照，不启动实验。
- 归档、部署、CMake 配置、构建、lookahead 定向 CTest 和完整 CTest；运行时 token 仅进程环境，不写日志。
- 不运行性能矩阵、不改远端历史树、不清理未知目录、不触碰 `/root/projects/CPSS(DCC)`、不安装依赖、不提交 Git。

## 验收

1. 记录 worktree HEAD/status、归档 SHA-256、源码与二进制 SHA-256、构建参数、依赖、空间前后和监听/PID。
2. 远端执行源 `tree_lookahead.cpp` SHA 必须等于固定输入，避免复用旧构建。
3. 定向 lookahead 23 项全部通过，尤其 #180/#181；完整 CTest 的 pass/fail/skip 原因分别记录。io_uring skip 不能算通过。
4. 证据目录和结果回执可回读，资源/既有 GridFTP 服务保持不变。

交付 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-revision-09-result.md` 与 `...-last-message.md`。
