# LOWLEVEL-TREE-LOOKAHEAD-PLAN-03：窄实现计划

- 路线/任务版本：R2026-09-24.10 / v1
- 责任：03 核心实现；独立复审：04 测试与质量；验收：00 总指挥
- 固定输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；契约 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`；QA PASS `docs/tasks/2026-09-24-lowlevel-tree-lookahead-revision-qa-02-result.md`。

## 目标

只形成可审查的最小实现计划，供 00 和 04 决定是否创建独立实现 worktree。计划针对目录阶段的 bounded lookahead：`depth=0` 默认保持旧行为；显式 `depth=1` 时，每 worker 至多一个 metadata candidate，全 run 至多两个 pending control；未来 data FD 永远为 0；外部 telemetry/schema/旧输出不变。

## 非目标

不改代码、测试、CMake、BOARD、ROSTER、决策或云端；不创建 worktree；不构建、测试、SSH 或实验。不改 framed wire、manifest/checkpoint/resume/checksum、scheduler/compression、IO backend、服务端、单文件客户端 API 或默认 control reuse。不新增 summary key、JSONL event、reason token、schema version 或 control_lease_id。不在候选阶段发送 SIZE/MDTM/EPSV/REST/STOR/RETR，不预建 data socket，不增加第二 worker control，不做 global scheduler 预取。

## 计划必须包含

1. 逐文件白名单：只允许 options 解析、独立 lookahead 状态/预算模块、tree scheduler/controlForFile 接缝、对应 unit/定向测试和必要 CMake 注册；列出具体函数/行段和每个文件为何需要。
2. 纯函数及状态机：metadata_reserved/control_pending/control_ready/in_use/cancelled/failed，owner/generation/fingerprint，duplicate reservation、double handoff、cancel-vs-handoff、stale、close/wait；明确 mutex/CAS 原子边界。
3. 两方向伪代码：候选阶段只能保存 manifest 指纹或普通 control login（仅 control_reuse=off 且资源门通过）；handoff 后严格执行原 upload/download 顺序。
4. 资源硬门：depth≤1、per-worker≤1、global≤2、extra FD≤2、data FD=0、candidate memory≤2 MiB、`getrlimit` 失败直接 depth=0；说明 FD/RSS/CPU 采样点和失败回退。
5. 测试向量：选项 0/1/>1、worker/off/global 回退、multi-worker 竞争、服务端 cap/TLS reject/cancel/断连、empty/skip/no-range/resume/changed/manifest failure；静态断言无 wire/manifest/resume/schema diff；运行态门留给后续实现任务。
6. 明确停止条件和风险：任何未知 schema/key/event/reason、文件级早发命令、未来 data FD、第二 worker control、协议/manifest/resume/scheduler/compression/IO 变化立即停止计划，不进入实现。
7. 说明本 slice 的预期性能意义和局限：worker reuse=plan-only 可能无收益；control_reuse=off 只可能减少认证/建连等待；不得把计划写成性能结论。

## 验收

- 唯一写入：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-03-result.md` 和 CLI 摘要。
- 只读复核 HEAD、index、固定源码 parity、UTF-8/LF、无尾随空白、`git diff --check`。
- 计划不得授权代码。完成后等待 04 独立复审；只有 04 PASS，00 才能另发实现任务并创建 `codex/<task-id>` worktree。
