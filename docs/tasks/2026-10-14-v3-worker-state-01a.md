# V3-WORKER-STATE-01A：目录连接模式状态真实可观测

- 状态：in_progress
- 输入提交：b13b574886ed9e1930eaca36f9ce703a696c4cb0
- 权威源码：深圳 /root/projects/CPNetFlux
- 父阶段：V3-WORKER-STATE-01

## 目标

修复控制 pipeline 兼容处理把默认 persistent-tree 请求静默改成 per-file，且 JSON summary 只报告改写后选项的问题。用户能够区分请求模式、实际执行模式和降级原因；不支持的显式组合仍须在网络操作前拒绝。

## 范围

- 在选项解析中保留用户请求的 data-session-reuse 模式。
- 默认路径因 control pipeline 自动降级时记录稳定的 fallback reason。
- JSON summary 同时记录 requested_mode、actual_mode 和 fallback_reason。
- 为隐式 fallback、显式冲突拒绝及 JSON 字段增加回归。

## 非目标

- 本阶段不把 control pipeline 或 global scheduler 接入 persistent data worker。
- 不改变 wire protocol、range/checksum/resume 语义、连接生命周期或桌面端。
- 不改默认兼容行为，不进行性能实验，也不产出稳定版安装包。

## 验收

- 默认 persistent-tree 请求搭配 pipeline 时仍按既有兼容行为执行 per-file，但 summary 报告 requested=persistent_tree、actual=per_file 及非空 fallback_reason。
- 显式请求 persistent-tree 与 pipeline 的组合在建立网络连接前明确失败。
- 相关定向测试、完整 CTest 和脚本门禁通过。

## 允许修改路径
- tools/test/tree_smoke_common.py

- include/cpnetflux/config/tree_transfer_options.h
- src/config/tree_transfer_options.cpp
- src/core/io/tree_transfer_client.cpp
- tests/unit/tree_transfer_options_test.cpp
- tools/test/run_gridftp_tree_pipeline_sidecar_smoke.py
- docs/coordination/BOARD.md
- docs/tasks/2026-10-14-v3-worker-state-01a.md
- docs/tasks/2026-10-14-v3-worker-state-01a-result.md

