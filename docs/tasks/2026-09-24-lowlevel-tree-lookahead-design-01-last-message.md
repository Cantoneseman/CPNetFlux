已完成并写入：

[lookahead 设计结果](D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-01-result.md)

核心裁定：默认关闭，深度最多 1；每 worker 一个候选、全局最多 2 个 pending control；`control_reuse=worker` 仅 plan-only；当前协议不预建未来 data socket，所有 transfer ID、resume、stream 和文件命令仍按原顺序执行，并可回退原路径。

已核验：UTF-8、无尾随空白、末尾 LF、`git diff --check` 通过；HEAD 仍为 `a076c532…`，固定源码无差异，Git index 为空。未运行构建、测试、实验或 SSH。