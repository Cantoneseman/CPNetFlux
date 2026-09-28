已交付手动实验包，状态保持 `READY_PENDING_DYNAMIC_QA`。

产物：

- [PowerShell 入口](D:/Project/CPNetFlux/tools/experiments/gridftp_compare/manual_cross_domain_experiment.ps1:1)
- [使用说明](D:/Project/CPNetFlux/tools/experiments/gridftp_compare/MANUAL_CROSS_DOMAIN_EXPERIMENT.md:1)
- [交付回执](D:/Project/CPNetFlux/docs/tasks/2026-09-24-manual-cross-domain-experiment-package-01-result.md:1)
- [CLI 摘要](D:/Project/CPNetFlux/docs/tasks/2026-09-24-manual-cross-domain-experiment-package-01-last-message.md:1)

验证结果：

- PowerShell parser：通过，`PARSER_ERRORS=0`
- `-Mode plan`：通过，退出码 0
- 生成 24 个 case，覆盖 single/dense/mixed、双方向、CPNetFlux/GridFTP、两次重复、seed 和 lookahead 配置
- 未连接 SSH、未创建远端目录、未生成 payload
- `git diff --check` 通过，HEAD 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，index 为空

脚本当前没有真实执行分支，`-Execute` 会 fail-closed。必须等待 05 revision-09 固定构建和 04 动态 QA 通过后，由 00 明确放行。