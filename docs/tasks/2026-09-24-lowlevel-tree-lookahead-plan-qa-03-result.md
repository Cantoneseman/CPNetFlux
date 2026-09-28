# LOWLEVEL-TREE-LOOKAHEAD-PLAN-QA-03 独立复审

日期：2026-09-24
路线/任务版本：R2026-09-24.10 / v1
角色：04 测试与质量；验收：00 总指挥

## 结论

结论：**PARTIAL，暂不允许 00 另发代码实现任务**。PLAN-03 大部分忠实落实 REVISION-02：禁止候选阶段文件级命令、默认/全局 depth 门、状态竞态、资源上限、白名单、动态 NOT_RUN 和性能边界都写得具体。但它把固定源码中不存在的“既有 `control_id`”当作可用契约，并在候选选择条件中只排除 `Completed` 而没有机械要求 manifest 状态为 `Pending`；这两点会使实现无法按固定输入无歧义落地。另有 worker 直接 `nextIndex++` 与计划所称 `nextWorkItem` 的接缝需要收紧。修订完成并由 04 复审前，不得创建代码 worktree。

## 输入与只读门禁

- `git rev-parse HEAD`：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，退出码 0。
- `git diff --cached --name-only`：空，退出码 0；未操作 index。
- PLAN-03 结果 SHA-256：`D802FBF0023FD6BFCF36635A293CF92CF411E77056F441C6F5A5020090601ECA`，18,281 bytes；复核期间输入哈希/修改时间稳定。
- 契约 REVISION-02 SHA-256：`49BFC7846F7D5871270DDCA29FB3EF782C593B52176808290BEDCB74899D8222`；QA PASS SHA-256：`3489A8726E02308A3904128883E93D5B764438CE81AF7C515D8FC1E356603CD2`；两者只读。
- 固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 到资料 HEAD 的三个源码文件 `git diff --quiet` 退出码 0；资料 HEAD 到工作树同三文件也为 0。
- `git diff --check` 退出码 0（共享既有文档仍有行尾转换提示）；本轮未覆盖、暂存或清理共享改动。
- 未运行 CMake、CTest、编译、parser/状态单测、真实传输、SSH、云端实验或性能实验。

## 必查项逐项结论

| 项目 | 结论 | 证据与独立判断 |
|---|---|---|
| 1. 文件/CMake 白名单 | PARTIAL | §1 明列 options、lookahead 模块、tree scheduler 和窄单测/CMake 范围，并禁止 file clients、协议、manifest/resume/checksum、scheduler/compression/IO、服务端及旧消费者；但“既有 `control_id`”不在固定源码白名单可提供的对象中，见 Critical-1。 |
| 2. depth/worker/off/global 配置 | PASS | §2 覆盖默认 0、显式 0/1、>1/负数/溢出/缺值拒绝，运行时绕过 parser 仍 effective depth=0；`control_reuse=worker` metadata-only，off 才可 pending，global 强制 0。 |
| 3. reservation 与 `nextIndex` | PARTIAL | §3 要求同一 mutex、owner/generation/fingerprint、duplicate/double-use、cancel/handoff race 和 pending 回滚；但固定源码 worker 循环在 `tree_transfer_client.cpp:2347–2350` 直接递增 `state.nextIndex`，计划文字主要指向未被调用的 `nextWorkItem`（`:1182–1192`），且归还候选 index 后的单一领取/不重排不变量未写成一个明确 helper 接缝。需补齐后才能机械验收。 |
| 4. upload/download 协议边界 | PASS（规格） | §4.2/§4.3 明确候选阶段禁止 `SIZE/MDTM/EPSV/REST/STOR/RETR`、data FD、transfer ID、range、SessionInit；handoff 后保持原顺序。计划没有要求改 file clients。动态尚未运行。 |
| 5. 资源与失败回退 | PASS（规格） | §5 固定 pending≤2、extra FD≤2、data FD=0、候选内存≤2 MiB、`getrlimit` 失败 depth=0，并覆盖 TLS/cap/取消/断连/close-wait/manifest failure 回退；CPU/RSS 明确仅观测。 |
| 6. 测试向量与动态边界 | PARTIAL | §6 有选项、状态、预算、多 worker、拒绝/取消/断连、empty/skip/no-range/resume/changed/manifest failure、hash/manifest/wire/frame/旧输出断言，且标为 NOT_RUN；但未覆盖“固定源码没有 control_id”这一接缝，且 reservation eligibility 未要求 Pending fixture。 |
| 7. 收益和历史证据边界 | PASS | §7 明确 worker reuse 可能无收益、off 只可能重叠认证等待，旧 dense loopback 不支持 100 Mbps/GridFTP 结论；没有把计划或历史 profile 写成性能通过。 |

## Critical 阻塞

### Critical-1：固定源码没有“既有 control_id”

PLAN-03 §2、§3、§4.3、§6 和 §7 多次要求候选沿用“既有 `control_id`”，但固定输入中 `ControlClient`（`src/core/io/tree_transfer_client.cpp:53` 起）没有 control ID 字段；现有 `EventRecord` 只有 `transferId`（`include/cpnetflux/core/metrics/event_log.h:13–26`），树事件由 `emitTreeEvent`（`tree_transfer_client.cpp:890–905`）写入，现有 JSON summary 由 `writeTreeJsonSummary`（`:907–1039`）写入，源码中不存在 `control_id`/`control_acquire` 字段。`transferId` 是文件传输 ID，不能冒充控制连接生命周期 ID。

这不是允许增加 schema 的理由；REVISION-02 明确禁止新增 key/event/reason，PLAN-03 也禁止修改 telemetry/旧消费者。最小修订必须二选一：

1. 按固定基线删除实现计划中所有“既有 `control_id`”依赖，改为仅使用内存中的不可序列化 `ControlClient`/候选句柄，明确外部事件和 summary 字节完全不变；或
2. 在任务输入中明确一个已经存在且固定的 telemetry 实现提交，并把它列入前置依赖和白名单；若需要新增控制 ID 字段，则本计划不能继续，必须另行修订路线并重新 QA。

在 00/01 选择前，代码任务无法同时满足“沿用既有 control_id”和“固定源码/外部输出不变”。

### Critical-2：候选 eligibility 未严格要求 Pending

REVISION-02 要求只有 Pending 且方向、端点和元数据可验证的记录进入候选；PLAN-03 §4.1 的实际条件写成“`j` 不是 Completed/不越界即可 reserve”。这允许 Changed、Failed 或其他非 Pending manifest 记录进入 reservation，违反原 skip/changed/failure 语义，且 §6 没有对应的预状态拒绝向量。

最小修订：在 `reserveNextCandidate` 和 worker 伪代码中机械要求 `TreeFileStatus::Pending`、有效方向/路径/endpoint/options 指纹；Completed、Changed、Failed、未知状态均返回 no-candidate/fallback，不占用 pending 或推进 index，并增加各状态的单元向量。

## Important 修订项

1. 将 `nextIndex` 的领取、候选 reservation、取消归还和 handoff 消费统一落到一个明确 helper/状态结构；直接针对当前 `runTreeScheduler` 的 `state.nextIndex++` 编写断言，说明两个 worker 不能重复领取/预订，候选取消不丢文件、不重复消费，也不引入未声明的 manifest 顺序变化。
2. 将“control ID”相关测试改成固定基线可验证的 opaque handle/连接生命周期测试；不把 `transferId`、REST transfer ID 或文件 attempt ID 当作 control identity。
3. 在实现计划的白名单检查中补充 `Pending` eligibility、非 Pending fixture 和固定源码字段扫描；仍保持 CMake 只添加 lookahead 源/窄单测，不放宽禁止文件。

## 已通过的契约边界

- 默认 depth=0、显式 depth=1、>1 保护及 global 强制 depth=0。
- worker reuse 不开第二 control；off 模式 bounded pending；未来 data socket 永远为 0。
- 上传/下载候选不发文件级命令，handoff 后恢复原 control/数据/manifest/resume 顺序。
- pending、FD、内存、`getrlimit`、TLS/服务拒绝、取消、断连和 close/wait 的硬门已写入计划。
- 禁止修改 file clients、wire、manifest/checkpoint/resume/checksum、scheduler/compression/IO、服务端和旧消费者；收益边界保持 observation-only。

## 未运行与下一步

动态构建、CTest、纯函数/状态测试、真实 upload/download、old JSON/事件 consumer、hash/manifest/wire/frame、FD/memory、取消/拒绝/断连、CPU/RSS、overhead、SSH 和云端实验均为 **NOT_RUN**。静态计划审查不能替代这些门禁。

00/01 先修订上述两个 Critical 和 worker 接缝；修订结果稳定后再由 04 复审。当前不得创建代码实现任务或 worktree，也不得通过新增 schema/key/event 绕过阻塞。

## 执行回执

- 实际输入/输出 commit：输入资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本轮新增本结果和 CLI 摘要，未提交。
- 实际改动：仅本任务允许的两个文档文件；未改源码、计划、契约、BOARD、ROSTER、决策或 index。
- 实际命令、退出码与证据：HEAD/status/index、输入 hash 稳定性、固定源码 parity、UTF-8/LF/尾随空白和 `git diff --check` 均已复核；未运行代码或实验。
- 结论：PARTIAL；不得另发代码实现任务，先完成 Critical-1/2 与 worker 接缝最小修订。
- 下一角色：00/01 修订 PLAN-03 或固定其 telemetry 前置后，重新派 04 独立复审。

验收人：00 总指挥（待验收）。
