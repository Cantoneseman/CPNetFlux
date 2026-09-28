# LOWLEVEL-TREE-TELEMETRY-ARCH-REVIEW-01

路线版本：`R2026-09-23.6`  
任务版本：`v1`  
固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`  
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`（只作协作文档来源，不是实现基线）

## 目标

由 01 架构与需求对目录阶段 telemetry 契约做最终架构裁定，处理 04 在 `LOWLEVEL-TREE-TELEMETRY-QA-02` 中列出的关键阻塞，给实现和分析器一套唯一、与固定源码调用顺序一致的事件/字段契约。

## 非目标

- 不修改源码、测试、runner、CMake、环境或云端。
- 不开始 telemetry 实现，不创建 worktree，不运行构建、CTest、传输或性能实验。
- 不改变协议帧、manifest/resume/checksum 行为、默认配置或 scheduler/IO backend。
- 不重写历史实验结论，不批准深圳—上海实验。

## 输入资料

- `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02-result.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-02-result.md`
- 固定源码 `3b0820d` 中的 tree/file upload/download、event log 与 `tools/demo/run_alpha_demo.py` 消费者。

## 允许修改范围

01 只写自己的结果文件：`docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-01-result.md`。如认为语义决策本身必须改变，停止并向 00 报告具体建议，不直接修改决策文件或 BOARD。

## 必须给出的裁定

1. 按 upload/download 的真实调用顺序，明确 `data_channel_finalize`、控制面 `transfer_complete_wait`、下载 mtime/最终文件提交与 tree `manifest_finalize` 的起止事件。明确哪些 span 可重叠、哪些只是报告值不可相加；不得给出互相矛盾的端点。
2. 明确 `tree_stage_v1` 与 `stream_identity` 各自完整的 JSON 字段集合、必填/null 规则、时间字段、scope/state、关联键、错误码及版本策略。每个事件类型均需有可机械验证的最小有效样例。
3. 冻结 run/file/attempt/worker/control/stream/span/skip decision/idle 的 ID 类型、作用域、起始值、预留时点、唯一性和失败/重试行为；指定完整 resume skip、空文件、非空 `no_missing_range`、setup 早退的确定记录形状与不生成的 span。
4. 冻结 append-only logger 的并发整行写入、重复 terminal、孤立 start、写失败隔离和可保证的 flush 层级；明确 summary/process result 怎样暴露 evidence partial 与写失败计数。
5. 给出最小 parser/validator 正反样例，含精确 uint64 JSON 数字、null/0 区分、错配/重复/坏行、identity 有效/重复/错 slot/冲突 ID、各生命周期状态互斥。
6. 说明该映射与旧 `run_alpha_demo.py` 消费者兼容的方式。不得把理论规格通过表述为测试已运行。

## 验收

- 结果明确列出每条 QA-02 BLOCKED/PARTIAL 条目的结论及是否闭合。
- 新契约不与固定源码的执行顺序冲突；时间端点、event 公共字段和 null 组合可被 validator 唯一判定。
- 如不能在本任务范围内解决，逐项标 BLOCKED 并列出需要 00 决策的最小问题，不得自行放宽质量门。
- UTF-8 可读、无尾随空白；写前后固定源码差异不变、HEAD 不变、index 为空；`git diff --check` 通过。
- 不以文档门禁声称实现、测试、传输或性能已验收。

## 交付与下一步

交付上述唯一结果文件并等待 00 收口。01 完成后由 00 将修订映射同步到 03 的 PLAN-03，再重新派 04 做独立 QA；除非 QA 明确 PASS，否则不创建 implementation task。
