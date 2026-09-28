已完成修订结果：

[LOWLEVEL-TREE-LOOKAHEAD-DESIGN-REVISION-02](D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md)

已闭合：

- 下载候选阶段禁止 `SIZE/MDTM/EPSV/REST/RETR`。
- 不新增 telemetry summary、JSONL 事件、字段或 `control_lease_id`；命中/未命中仅内部计数。
- 冻结 `metadata_reserved → control_pending → control_ready → in_use` 状态及 reservation/owner/lease 原子规则。
- 明确 `fd_extra_peak<=2`、pending data FD=0、内存预算、`getrlimit` 不可用时回退 depth=0。
- 增加逐文件白名单、硬停止条件、纯函数验收表和 upload/download 时序。

门禁通过：UTF-8、无尾随空白、末尾 LF、`git diff --check`、源码 parity；HEAD 未变为 `a076c532…`，暂存区为空。未运行实现、测试、构建或实验，等待 04 独立复审。