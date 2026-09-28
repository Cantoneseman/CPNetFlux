# PERF-IMPL-ADAPTER-01：规范化 telemetry 适配与覆盖清单

- 状态：done，映射已交；B5 仍 unknown/ineligible，待 04 复审
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：04 质量、00
- 输入 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；若 HEAD 或源码状态变化，先停止并报告 superseded。

## 目标

在不改代码的前提下，把现有单文件和目录传输的压缩、IO、校验、控制复用和失败路径字段映射成 02 v3 要求的 `CompressionObservationV1` 规范化输入，并列出每个参与者/文件/生命周期的覆盖证据与缺口。目标是让 04 能判断哪些记录可进入 `verified_off`，哪些必须 blocked。

## 非目标与允许修改

- 不改 C++、Python、schema、runner、测试、协议、默认值、校验/resume、scheduler、IO backend 或实验配置。
- 不构建、不运行 CMake/CTest/runner/传输，不 SSH，不改云端，不操作 Git index。
- 只追加 `docs/coordination/receipts/03-implementation.md` 的本任务小节；不要修改 BOARD/ROSTER 或其他角色文件。

## 必须交付

1. 对 single upload/download、tree upload/download、scheduler off、worker reuse、失败/重试/并发分别给出真实源码字段、文件:行号、计数单位和生命周期边界。
2. 对 participant roster、file/worker/lifecycle coverage、effective compression、checksum effective policy、control reuse hit、最终失败记录逐项标记 `available`、`partial` 或 `missing`；不以字段名相似代替语义证明。
3. 给出最小后续实现所需字段白名单、建议归属模块及与 B5 `verified_off` 的阻断关系；不得把建议字段写成当前已有事实。

## 验收

- 回执记录输入 HEAD、实际命令、源码引用、缺口和未运行项。
- 不得宣称 B5/B6/B7/B8 或实现已通过；未执行项保持 NOT_RUN。
- 写后验证 UTF-8、`git diff --check`、HEAD 不变、暂存区为空。

## 执行回执

- 已由既有 03 核心实现聊天完成，CLI session `12137` 正常终态，退出码 0。
- 交付位置：`docs/coordination/receipts/03-implementation.md` 的 `PERF-IMPL-ADAPTER-01 v1` 小节。
- 结论：现有字段仍缺 participant roster、worker 归属、sampling attempts、control reuse hit 和失败路径完整覆盖；只能判 `unknown/ineligible`，未授权代码。
- HEAD 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；源码/测试无差异，暂存区为空，文档门禁通过。
