# MCE-TOPOLOGY-02 revision 2 CLI 摘要

依据 `runner.py` 命令构造修正计划角色：Windows 是人工 launcher；深圳固定为 experiment controller 和 transfer client；上海固定为 peer service。正向 source=深圳/destination=上海，反向 source=上海/destination=深圳，但 client/controller 与 peer service 不随方向变化。v1 角色映射被 supersede，旧回执保留。

完整回执：`docs/tasks/2026-09-24-manual-cross-domain-experiment-topology-revision-02-result.md`。`-Execute` 仍 fail-closed；未 SSH、未传输。动态 QA 与运维门禁未解除。
