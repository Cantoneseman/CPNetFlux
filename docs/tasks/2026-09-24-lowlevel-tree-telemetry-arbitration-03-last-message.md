已完成架构收口，结果写入 [ARBITRATION-03 结果](D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md)。

唯一推荐契约：

- no-range 保留 connection-complete 起点；允许 `first_payload` 先写 start、确认 no-range/empty 后追加配对 N/A terminal。`payload_io` 仍为单行 N/A；首块前失败按 failed/skipped。
- retry 使用新 attempt/span/stream，但复用实际 control 的 `control_id`；只有真实重连才新建 control。
- interfile idle 连续文件写 completed terminal；尾部/取消写配对 N/A；交接错误写 failed；不回写。

门禁通过：HEAD 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，index 未变，固定源码差异为 clean，`git diff --check`、UTF-8、LF、尾随空白检查通过。动态测试均 `NOT_RUN`。当前实现闸门仍 BLOCKED，等待 03 更新 PLAN-04、00 统一路线版本并由 04 独立复审。