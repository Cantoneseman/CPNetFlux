# LOWLEVEL-TREE-LOOKAHEAD-PLAN-REVISION-QA-04 独立复审

日期：2026-09-24
任务版本：R2026-09-24.11 / v2
角色：04 测试与质量；交付：00 总指挥

## 结论

结论：**PASS（允许 00 另发窄代码实现任务）**。PLAN-REVISION-04 已闭合 QA-03 的三个阻塞：候选不再依赖不存在的 `control_id`，eligibility 机械限定为 `Pending`，并明确用实际 `runTreeScheduler` worker lambda 的统一锁内 helper 替换裸 `state.nextIndex++`。其余 REVISION-02 的协议、资源、白名单、兼容和性能边界仍被保留。本结论只允许 00 另发代码实现任务，不授权直接写代码、创建 worktree、构建、测试、SSH 或实验。

## 输入与门禁

- `git rev-parse HEAD`：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，退出码 0。
- `git diff --cached --name-only`：空，退出码 0；未操作 index。
- PLAN-REVISION-04 结果 SHA-256：`C70BE695FFABE086724D744B9109A4F2C46428251A76A4F7EE13699446A9F53E`；摘要 SHA-256：`9970DF66FB729C12F5821FD77D9BBE60952A4C362B04036FDD2F33324B5ABA2B`。两份文件复核期间哈希、长度/修改时间稳定。
- 固定三源码相对 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 的 `git diff --quiet` 退出码 0；固定字段扫描 `rg -n "control_id|controlId" src include` 退出码 1（无匹配）。源码仍是未修改的固定基线；本轮没有把计划描述当成实现证据。
- 结果/摘要均为 UTF-8、LF、无 BOM、无尾随空白并以 LF 结束；`git diff --check` 退出码 0。共享工作树的既有文档改动保持原样。

## QA-03 三个阻塞

| 阻塞 | 结论 | 复核依据 |
|---|---|---|
| 固定源码无 `control_id` | PASS | §1、§2、§3.2、§5、§7 明确候选只持有进程内不可序列化 opaque handle/ownership；候选不含控制 ID、文件 `transferId`、REST/attempt/stream ID；`transferId` 不作 handoff token；外部 summary/JSONL/reason/schema/旧消费者字节集合不变。固定源码扫描无 `control_id/controlId`。 |
| eligibility 必须为 Pending | PASS | §3.3 要求 `record.status == TreeFileStatus::Pending`，并覆盖 Completed、Changed、Failed、Transferring、未知状态、方向/路径/endpoint/options/metadata 失败；非 Pending 不 reservation、不推进候选游标、不占 pending/FD/memory，§7.1 为每类拒绝列出断言。 |
| 真实 worker claim 接缝 | PASS（计划门） | §2、§4 明确 `runTreeScheduler` 当前 `:2341–2357` 的 lambda 裸 `state.nextIndex++` 必须替换；`claimWorkLocked`、`reserveCandidateLocked`、`cancelCandidateLocked`、`consumeCandidateLocked` 共享 `SchedulerState::mutex` 和 index 表，普通 claim、候选 reservation、cancel returned、handoff consume 不得有旁路。代码尚未修改，静态门在实现后验证。 |

## REVISION-02 保留约束

- **协议/文件边界：PASS（规格）**。§5 明确 upload/download 候选阶段不发送 `SIZE/MDTM/EPSV/REST/STOR/RETR`，不建 data FD、不分配 transfer/attempt/range；handoff 后分别恢复现有 `processUploadFile` 和 `processDownloadFile` 顺序。
- **配置：PASS（规格）**。默认 depth=0；显式 1 只进普通 worker；global 强制 depth=0；worker reuse 只 metadata-only、不得第二 control；off 才可 bounded ordinary control pending。
- **资源/失败：PASS（规格）**。depth≤1、每 worker≤1、全局 pending≤2、extra FD≤2、data FD=0、candidate memory≤2 MiB；`getrlimit` 失败/不可信回退 depth=0；TLS/login/server cap、cancel、disconnect、close/wait、stale、manifest failure 回原路径。
- **白名单/禁改：PASS（规格）**。§2、§7 禁止 file clients、protocol、manifest/checkpoint/resume/checksum、scheduler/compression/IO backend、server、旧 consumer、BOARD/ROSTER/decision、云端和新增外部字段；CMake 仅新增 lookahead 源及窄单测。
- **输出兼容：PASS（规格）**。没有新增 summary/event/reason/schema；候选 opaque handle 不序列化，文件 `transferId` 保持文件级语义。
- **动态边界：PASS（记录方式）**。§7.2 将 fresh/empty/skip/no-range/resume/changed、双向、拒绝/取消/断连、hash/manifest/attempt/wire/frame/旧消费者列为后续测试，明确当前全部 NOT_RUN。
- **性能边界：PASS**。§8 明确 metadata-only 可能无收益，off 仅可能重叠认证/建连；不从计划或 loopback 推出 100 Mbps/100G，不提高默认并行度伪装收益。

## 固定源码接缝的独立核对

固定源码 `TreeFileStatus` 只有 Pending、Transferring、Completed、Failed、Changed；PLAN-REVISION-04 对这些状态和未知值定义了候选拒绝。当前 `runTreeScheduler` 的实际 worker lambda 位于 `tree_transfer_client.cpp:2341–2357`，仍有原始 `state.nextIndex++`，而 `nextWorkItem` 在 `:1182–1192` 是另一处 helper；计划明确实现时二者不得保留两套领取语义，所有 index 状态必须经过同一锁内表。该项当前是“计划通过、代码未运行”，不是现状通过。

## 尚未运行与授权边界

以下均为 **NOT_RUN**：代码/worktree、CMake/CTest、parser/状态/资源单测、真实 upload/download、控制拒绝/取消/断连、hash/manifest/resume、wire/frame、旧 JSON/event consumer、FD/memory、CPU/RSS、overhead、SSH、云端和性能实验。计划中的静态禁止门、opaque 生命周期和同锁不变量必须在后续实现任务中实际验证。

允许 00 另发一个窄代码实现任务；该任务必须使用独立 `codex/<task-id>` worktree，并以 PLAN-REVISION-04 白名单和停止条件为准。任何新增可序列化控制身份/外部字段、候选早发文件命令、data FD、非 Pending reservation、worker 旁路 `nextIndex++`、index 重复/丢失或修改禁区文件，都应立即 BLOCKED 并回退，不得扩大实现范围。

## 执行回执

- 实际输入/输出 commit：输入资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本轮新增本结果和 CLI 摘要，未提交。
- 实际改动：仅本任务允许的两个文档文件；未改 PLAN-REVISION-04、源码、测试、CMake、BOARD、ROSTER、决策、worktree 或 index。
- 实际命令与证据：HEAD/index、计划和摘要 SHA-256 稳定性、固定源码 parity、`control_id/controlId` 无匹配、UTF-8/LF/尾随空白和 `git diff --check` 均已核对并通过。
- 结论：PASS，仅允许 00 另发窄代码实现任务；不代表实现、测试、传输或性能验收通过。

验收人：00 总指挥（待验收）。
