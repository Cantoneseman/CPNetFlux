PLAN-06 已完成：[结果文件](</D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-06-result.md>)。

已补齐：

- 四 scope 的完整字段、状态、reason、null/forbidden 及 ID 矩阵；
- 6 组、44 行完整 JSONL golden fixtures；
- paired N/A、late stream ID、retry 同 control、idle 四类生命周期；
- logger 四类故障注入及五状态轴断言；
- loopback upload/download 的 fresh、no-range、resume 样本、随机交错、10 对重复和 overhead 门限。

注意：既有资料把 `stream_identity` 称为 19 键，但逐项字段实际为 18 键；报告已记录该计数矛盾，未擅自添加被架构资料禁止的 `span_id`。

最终门禁通过：输入哈希未变，HEAD 为 `a076c532…`，index 为空，固定三源码差异为 0，UTF-8/LF/尾随空白检查通过，fixture lint 退出码 0，`git diff --check` 退出码 0。构建、parser、logger 注入、真实传输和性能实验均 `NOT_RUN`；等待 04 独立复审。