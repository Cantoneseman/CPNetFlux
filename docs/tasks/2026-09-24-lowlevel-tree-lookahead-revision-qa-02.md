# LOWLEVEL-TREE-LOOKAHEAD-REVISION-QA-02：修订契约独立复审

- 路线/任务版本：R2026-09-24.10 / v1
- 责任：04 测试与质量；验收：00 总指挥
- 输入：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`；原 QA `docs/tasks/2026-09-24-lowlevel-tree-lookahead-qa-01-result.md`；固定源码基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。

## 目标

独立判断 REVISION-02 是否闭合 QA-01 的 Critical/Important 阻塞，是否足以授权 03 先提交窄实现计划（不是代码实现）。

## 必查项

1. 下载候选阶段是否严格禁止 SIZE/MDTM/EPSV/REST/RETR，handoff 后是否保持既有 `processDownloadFile` 顺序；上传同理。
2. 是否完全移除新增 lookahead summary/JSON key/JSONL event/reason/schema/version/control_lease_id；旧消费者和旧输出是否保持兼容。
3. 状态机是否区分 metadata_reserved/control_pending/control_ready/in_use/cancelled/failed，且 owner/generation/handoff/cancel race 可机械断言。
4. 资源硬门是否明确 pending≤2、extra FD≤2、data FD=0、candidate memory≤2 MiB、getrlimit 失败回退 depth=0；CPU/RSS 是否正确标为观测项。
5. 实现白名单和禁止范围是否具体、可审查；是否仍允许第二 worker control、未来 data socket 或修改 wire/manifest/resume/scheduler/compression/IO。
6. 验收矩阵是否涵盖 control_reuse=off、多 worker、服务端 cap、拒绝/取消/断连、fresh/empty/no-range/resume/changed/manifest failure，以及 hash/wire/旧 JSON 兼容。

## 结论规则

- `PASS`：允许 03 创建“实现计划”任务，但不允许直接写代码或创建实现 worktree。
- `PARTIAL/BLOCKED`：列出必须修改的最小条款，继续阻止 03。
- 任何发现只能靠新增 schema/key/event 才能表达的需求，必须判阻塞。

## 非目标与写入范围

不改源码、BOARD、ROSTER、决策或原设计；不构建、不测试、不 SSH、不实验；只写 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-revision-qa-02-result.md` 和 CLI 摘要。复核 HEAD、空暂存区、源码 parity、UTF-8/LF、`git diff --check`。
