# LOWLEVEL-TREE-LOOKAHEAD-PLAN-QA-03：实现计划独立复审

- 路线/任务版本：R2026-09-24.10 / v1
- 责任：04 测试与质量；验收：00 总指挥
- 输入：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-03-result.md`；契约 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`；QA PASS `docs/tasks/2026-09-24-lowlevel-tree-lookahead-revision-qa-02-result.md`；固定输入 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。

## 目标

独立判断 03 的 PLAN-03 是否忠实实现已通过的 REVISION-02 契约，是否足以让 00 另发代码实现任务。结论只能是 PASS（允许另发实现任务）或 PARTIAL/BLOCKED（列出最小修订）。

## 必查项

1. 逐文件白名单是否与契约一致；CMake 只纳入新增 lookahead 源/窄单测，禁止触碰 file clients、协议、manifest/resume/checksum、scheduler/compression/IO、服务端和旧消费者。
2. depth 0/1/>1、worker/off/global 配置是否安全；global 强制 depth=0；默认行为不变。
3. reserveNextCandidate 是否与 nextIndex 在同一锁下，避免两个 worker 重复预订/跳过文件；状态图、owner/generation/fingerprint、handoff/cancel race、close/wait 是否可测。
4. upload/download 候选阶段是否绝不发送 SIZE/MDTM/EPSV/REST/STOR/RETR，handoff 后是否保持原顺序；data FD、transfer ID、range、attempt 和 manifest 语义是否不变。
5. pending≤2、extra FD≤2、data FD=0、candidate memory≤2 MiB、getrlimit 失败 depth=0、TLS/cap/取消/断连回退是否写入实现门。
6. 测试向量是否覆盖选项、状态、资源、拒绝/取消/断连、empty/skip/no-range/resume/changed/manifest failure、旧输出/wire/frame/hash 等价，并明确动态项尚未运行。
7. 计划是否明确本 slice 可能无收益，未把设计或旧 loopback profile 写成 100 Mbps/GridFTP 结论。

## 非目标与写入范围

不改源码、BOARD、ROSTER、计划或契约；不创建 worktree；不构建、测试、SSH 或实验；只写 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-qa-03-result.md` 与 CLI 摘要。复核 HEAD、空 index、源码 parity、UTF-8/LF、`git diff --check`。
