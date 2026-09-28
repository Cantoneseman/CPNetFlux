复核结果为 **PARTIAL**，已写入[完整报告](D:/Project/CPNetFlux/docs/tasks/2026-09-23-lowlevel-sendv-qa-01-result.md)和[执行摘要](D:/Project/CPNetFlux/docs/tasks/2026-09-23-lowlevel-sendv-qa-01-last-message.md)。

静态检查确认 DATA-only 调用范围、64-byte header 拼接、短写推进、EINTR、零写、errno、`MSG_NOSIGNAL` 及 TLS fallback 符合设计。按 00 的汇总，DATA 写调用从 8,192 降到 4,096，CPU/GiB 在 1/8 connections 分别下降约 26.3%/17.4%；但 8-connection wall 中位数从 0.23 s 升至 0.25 s。每格仅三次，且本轮因 WSL 与 `\\wsl$` 的 `E_ACCESSDENIED` 无法读取原始 CSV/trace，所以退出码、hash 与 trace 明细仍未独立验真。

建议暂留未提交隔离 worktree，当前不合入；待原始证据可读、8-connection wall 波动得到复核后由 00 收口。没有据此宣称跨域或 GridFTP 性能收益。本轮未运行构建或测试；HEAD 未变、两处 index 为空，`git diff --check` 和新文件 UTF-8/空白检查通过。