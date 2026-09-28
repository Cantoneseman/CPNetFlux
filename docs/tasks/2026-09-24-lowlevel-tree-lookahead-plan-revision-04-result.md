# LOWLEVEL-TREE-LOOKAHEAD-PLAN-REVISION-04：v2 窄实现计划

- 任务版本：`R2026-09-24.11 / v2`
- 责任：03 核心实现（仅修订计划）；独立复审：04；验收：00
- 状态：**仅修订计划，待 04 复审；不授权代码或 worktree**。
- 固定实现基线：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- 当前资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`

## 1. v1 → v2 的三项修订

本稿读取了任务单、PLAN-03、QA-03、REVISION-02 契约及 QA-02 PASS 结果。QA-03 的 PARTIAL 只指出三个实现前阻塞，本稿逐项收紧：

1. 固定基线的 `ControlClient` 没有稳定的控制连接 ID；事件记录中的 `transferId` 是文件级传输标识，不能作为控制连接身份。v2 删除候选对任何控制 ID 的依赖。候选只在进程内持有不可序列化的 opaque 控制对象所有权/句柄（需要普通 control 登录时）或 metadata-only 状态；外部 summary、JSONL event、reason、schema、旧消费者字节集合完全不变。
2. `reserveCandidate` 的 eligibility 现在是机械的 `record.status == TreeFileStatus::Pending`，再加方向、规范路径、endpoint/options 指纹和可执行的 metadata 校验。`Completed`、`Changed`、`Failed`、`Transferring` 及无法识别的状态永不 reservation、不推进候选游标、不占 pending；普通 worker 对这些记录仍走现有领取和原 `process*File` 路径。
3. v2 不再用未接入 worker lambda 的抽象名称掩盖接缝。统一的锁保护 `claimWorkLocked`、`reserveCandidateLocked`、`cancelCandidateLocked`、`consumeCandidateLocked` 直接替换 `runTreeScheduler` 当前 `state.nextIndex++` 的临界区；同一 `SchedulerState::mutex` 管理普通领取、候选预留、取消归还和一次性 handoff。

除这三点外，REVISION-02 的协议、文件命令、资源和外部输出边界保持不变。

## 2. 固定源码核验和可接入位置

固定三源码相对 `3b0820d` 的 parity 必须为零。当前资料树核对到以下真实位置：

| 固定位置 | v2 计划动作 | 机械边界 |
|---|---|---|
| `src/core/io/tree_transfer_client.cpp:1046` 的 `SchedulerState` | 在现有 `state.mutex` 所保护对象中加入 run 级 `WorkReservationTable` 和每 worker candidate slot；`nextIndex` 只由统一 helper 改动 | 不新增 manifest 状态、不改变现有 first-error/stop 语义 |
| 同文件 `:1109` 的 `TreeWorkerRuntime` | 仅增加当前 worker 的 opaque candidate handoff 句柄/待处理 reservation 引用 | 句柄不可序列化，不写 summary/event，不把文件 `transferId` 代用 |
| 同文件 `:1182-1192` 的 `nextWorkItem` | 若保留，改为调用同一 `claimWorkLocked`；不得保留一条旁路领取逻辑 | helper 必须实际被 worker lambda 调用 |
| 同文件 `:1151-1178` 的 `controlForFile` | 只消费当前 runtime 已验证的 opaque handoff；无句柄时保留原建连路径 | 不要求或生成外部控制连接 ID |
| 同文件 `:2341-2357` 的 `runTreeScheduler` worker lambda | 用 `claimWorkLocked` 替换直接的 `state.nextIndex++` 块；在同一循环内驱动 reserve、candidate prepare、handoff/fallback | 同一 index 不能被普通领取和候选预留同时持有 |
| 同文件 `:1307-1474`、`:1479-1632` | handoff 后分别调用原 upload/download 顺序 | 候选阶段不发文件级命令、不建 data FD |
| 同文件 `:2099-2290` 的 `runGlobalTreeScheduler` | 显式 effective depth=0 | 不接入 global 预取 |
| `include/cpnetflux/config/tree_transfer_options.h:40-73`、`src/config/tree_transfer_options.cpp:162-485` | 增加/解析默认 0、仅接受 0/1 的 `lookahead_depth` | 非 0/1 parser 拒绝；绕过 parser 也 depth=0 |
| `include/cpnetflux/core/io/tree_lookahead.h`、`src/core/io/tree_lookahead.cpp`（如采用独立模块） | 只放纯 eligibility、预算、状态/领取 helper；控制对象用不透明回调或句柄传递 | 不依赖 wire、file client、manifest 写入或可序列化控制身份 |
| `CMakeLists.txt` core/test 列表、`tests/unit/tree_transfer_options_test.cpp`、`tests/unit/tree_lookahead_test.cpp` | 后续实现时只注册上述模块和窄单测 | 不改 runner、旧 smoke、生产协议目标 |

禁改路径仍包括 `src/core/io/file_transfer_client.cpp`、`file_download_client.cpp`、所有 framed/control protocol、manifest/checkpoint/resume/checksum、scheduler/compression/IO backend、服务端、旧 summary/event 消费者、`tools/demo/run_alpha_demo.py`、BOARD、ROSTER、决策和云端数据。任何需要这些路径的方案立即 BLOCKED。

固定源码字段扫描计划门：`rg -n "control_id|controlId" src include` 必须无匹配；现有 `transferId` 只允许在文件传输/REST/事件记录原语中出现，不得流入 candidate identity 或 handoff token。

## 3. 配置、opaque candidate 和 eligibility

### 3.1 配置

- 默认 `lookahead_depth=0`：候选模块不分配、不建额外 control，原行为逐字节保持。
- 显式 `1`：普通 worker scheduler 才能启用 bounded slice；每 worker candidate 至多 1，全 run pending ordinary control 至多 2；future data FD 永远 0。
- `>1`、负数、溢出、缺值：既有 invalid-argument 结果，传输不启动；防御性运行时对非 0/1 一律 effective depth=0。
- `control_reuse=worker`：只允许 metadata-only candidate，不能创建第二 worker control。
- `control_reuse=off`：资源硬门通过时，candidate 可推进一个普通 control 的 connect/auth/login；该控制对象仍只是内存中的 opaque ownership，不能序列化或写入外部输出。
- `scheduler=global`：本 slice 强制 depth=0，不做 global scheduler 预取。

### 3.2 候选对象和生命周期

候选只保存 `run_id`、worker owner、manifest `file_index`、内部 generation/candidate 序号、方向、规范相对路径、manifest metadata fingerprint、endpoint/TLS/options fingerprint、状态和可选的 opaque control handle。候选对象不含控制连接 ID、文件 `transferId`、REST ID、attempt/stream ID 或任何可写 JSON 字段。普通 control 的具体 `ControlClient` 类型继续留在 `tree_transfer_client.cpp` 私有实现中；若拆出 `tree_lookahead.*`，只通过不透明句柄和 close/wait/consume 回调交界，不让私有类型泄露到公共 schema。

状态图保持：

```text
empty -> metadata_reserved
metadata_reserved -> control_pending -> control_ready
metadata_reserved/control_pending/control_ready -> cancelled | failed
control_ready -> in_use -> empty | failed
```

`metadata_reserved` 没有 control FD；`control_pending` 最多一个额外普通 control FD 但不可交付；`control_ready` 只表示 opaque 对象已完成普通 login；`in_use` 表示一次性 handoff；终态对象不得重回 ready。close/wait 在锁外完成，结果带 owner/generation 回锁提交；失败释放名额并回到原路径。

### 3.3 机械 eligibility

`eligiblePending(record, direction, options)` 是纯函数，必须全部成立：

1. `record.status == core::tree::TreeFileStatus::Pending`；其他枚举值（`Completed`、`Changed`、`Failed`、`Transferring`）和解析/内存中的未知值直接 no-candidate。
2. manifest mode 与 upload/download 方向一致；relative path 非空、规范化后仍位于 transfer root 内且没有 traversal/重复规范化差异。
3. endpoint、TLS、控制认证选项和传输选项指纹可计算且与当前 run 相等；缺失或不可信指纹不得 reservation。
4. 上传：候选前 `statRegularFile` 成功；保存 size/mtime 等 metadata fingerprint；resume 或 manifest 有期望值时必须通过现有 `metadataMatches` 语义。handoff 前仍重新 stat，变化即 stale/fallback。
5. 下载：候选只校验 Pending manifest 的路径、size/mtime/checksum 等结构化 metadata 可解析且 fingerprint 可计算；远端 `SIZE/MDTM` 不在候选阶段执行，handoff 后仍按原流程复核。

非 Pending 记录不会因“被跳过”而由 candidate 游标推进，也不占 pending/FD/memory 名额。普通 worker 如领取到它，仍交给旧 `processUploadFile`/`processDownloadFile` 的 Completed/Changed/Failed 逻辑处理；候选 eligibility 失败只返回 no-candidate，不改变 manifest 状态。

## 4. 实际 worker 接缝和不变量

### 4.1 一个锁保护的领取表

在 `SchedulerState::mutex` 下增加 `WorkReservationTable`，记录每个 index 的一种状态：`free`、`candidate_reserved(owner,generation)`、`claimed(owner,normal|handoff)`。`nextIndex` 仍是当前未扫描游标，但不再由 worker lambda 直接自增。建议的实际 helper（名称可等价，但必须在此接缝被调用）如下：

- `claimWorkLocked(state, worker, runtime)`：锁内检查 stop；先识别该 worker 上一次 candidate 的待 handoff，否则从 `nextIndex` 及必要的有序 returned 集合取一个未被 reservation/claim 的 index，并原子记录 `claimed`；在返回当前 token 前，同一锁内调用 `reserveCandidateLocked` 预订下一个 Pending index，并把待准备描述放入 token。它是 `runTreeScheduler` 的唯一普通领取入口。
- `reserveCandidateLocked(state, worker, currentIndex, options)`：在同一锁内寻找下一个尚未 claim/reserve 的 manifest index；先执行 `eligiblePending`，只对 Pending 登记 candidate reservation 和 generation；非 Pending 不移动 candidate 游标。reservation 成功后才允许锁外启动 metadata/普通 control 准备。
- `completeCandidate(state, owner, generation, result)`：锁外准备完成后回锁；generation/owner 不匹配或候选已取消则关闭 opaque 对象、释放 pending，不能改变 manifest。
- `cancelCandidateLocked(state, owner, generation)`：把 candidate 变为 cancelled，摘除其 reservation，并将该 index 一次放入有序 returned 集合；重复 cancel 是幂等，不得插入第二份。
- `consumeCandidateLocked(state, owner, generation, runtime)`：只接受 ready、owner/generation/fingerprint 全匹配且 handoff_count=0；一次性变为 in_use/claimed(handoff)，把 opaque handle 移给 runtime；第二次 consume 返回 stale/double-use，不能调用文件函数。

`runTreeScheduler` 当前 `:2347-2350` 的代码块应被一个循环调用替换。关键是 helper 在返回当前 index 前就完成下一 Pending candidate 的 reservation，使准备工作能与当前文件重叠：

```text
while true:
  lock(state.mutex)
  token = claimWorkLocked(state, worker, runtime)
      // 唯一领取入口；若是普通/已 handoff 的当前 index，
      // 在同一锁内调用 reserveCandidateLocked，返回最多一个 candidate_to_prepare
      // 若 worker 有自己的旧 candidate，保留其 reservation 并返回 candidate_to_handoff
  unlock
  if token.done: return

  if token.candidate_to_handoff:
      wait/finish opaque login outside the lock
      lock; consumeCandidateLocked(...) or cancel and claim the same index normally
      unlock
  if token.candidate_to_prepare:
      prepare metadata or ordinary control login outside the lock
      completeCandidate(...) with owner/generation

  status = processFile(state, token.index, options, runtime)
  if status fails:
      lock; cancelCandidateLocked(state, worker-owned candidate)
      setFirstErrorLocked(state, status)
      unlock; return
```

候选准备因此发生在 `processFile` 当前文件期间，且不持有 scheduler mutex。下一次 `claimWorkLocked` 只消费该 worker 自己的 candidate reservation；若仍 pending/失败/取消，先按 generation 关闭或等待，然后把**同一个 index**转为普通 claimed，调用原 `controlForFile`，不丢失也不复制文件。若 run stop/worker error 发生，所有 candidate reservation 经 `cancelCandidateLocked` 清理。

为避免“当前文件先处理、候选后预留”的抽象落空，实施时的静态门必须证明：worker lambda 不再存在裸 `state.nextIndex++`；所有 index 状态改变都来自上述 helper；helper 在同一 mutex 下同时检查普通 claim 和 candidate reservation。`nextWorkItem` 若保留只能转发到 `claimWorkLocked`，不能形成第二套语义。

### 4.2 必须保持的不变量

- 一个 manifest index 在任意时刻最多处于一种 `free/candidate_reserved/claimed` 状态；普通 claim 与 candidate reservation 不能同时成功。
- 非 Pending 不会被 candidate reservation、candidate cursor 或 pending budget 推进；普通旧路径对已领取非 Pending 的处理不变。
- candidate cancel 只会从 reservation 表删除一次并把 index 放回一次；再次 cancel 不重复，run stop 清理不吞掉未处理的 index。
- successful handoff 只发生一次；opaque handle 只能转移给原 owner 的一次 `runtime`，二次 handoff 永远 fallback 原路径。
- 取消后 returned index 按有序集合重新可领取；manifest vector、file IDs、文件命令顺序和现有并发调度语义不被重排。候选只是内部 reservation，不改变 manifest 内容或序列化顺序。
- owner/generation/fingerprint 任一不匹配只关闭 candidate、释放资源并让该 index 走原路径，不改为传输失败、Changed 或 Completed。

## 5. 两方向 handoff（无控制 ID 依赖）

### upload

候选阶段只做 Pending eligibility、本地 stat/fingerprint；若 `control_reuse=off` 且预算通过，可在锁外做普通 control connect/auth/login，完成后保存 opaque 对象。不得发送 `EPSV`、`REST`、`STOR`，不得创建 data FD 或 transfer ID。

handoff 成功后，`processUploadFile` 按现有顺序执行：本地 metadata/changed 检查、Completed gate（若当前记录已被其他原路径改变则 candidate stale）、transfer slot、`EPSV`、resume 时 `REST`、`STOR`、原 file transfer client、原 wait/226、retry、manifest update。候选失败关闭 opaque 对象，当前同一 index 调用原 `controlForFile`，不创建新外部身份或改变 retry/attempt。

### download

候选阶段只保存 Pending manifest 的规范 path 和 metadata/options fingerprint；不发 `SIZE`、`MDTM`、`EPSV`、`REST`、`RETR`，不创建远端文件副作用、data FD 或 range/transfer ID。

handoff 后，`processDownloadFile` 仍执行 local Completed gate、远端 `SIZE`/`MDTM`、resume/changed 判定、slot、`EPSV`、`REST`、`RETR`、原 download client、wait/mtime/final commit/manifest。远端 metadata mismatch 只在当前真实文件流程处理；候选不得提前判定或修改 manifest。

空文件、完整 skip、no-range、resume partial、changed、首个文件失败和 manifest failure 均沿原路径；候选没有独立的外部事件或 summary 行。文件 `transferId` 仍仅由现有文件协议/REST 语义使用，不能进入 candidate handle。

## 6. 资源硬门和失败回退

保持 REVISION-02 数值不变：depth≤1；每 worker candidate≤1；全 run pending ordinary control≤`min(worker_count,2)`；extra FD peak≤2；data FD=0；每 candidate≤1 MiB、合计≤2 MiB；Linux `getrlimit(RLIMIT_NOFILE)` 失败/不可信或 `active_fd + pending_control + 32 > soft_limit` 时 effective depth=0。worker reuse 不创建第二 control，global scheduler 不创建预取控制。

采样点为 run start、candidate reserve、control socket 建立、handoff 前、candidate close/wait 后和 run end；FD/pending/memory 是硬断言，RSS/CPU 仅观察。TLS/login/server cap reject、connect/断连、cancel、worker first error、close/wait、metadata stale、manifest flush failure 均释放 opaque 对象和预算，回到该 index 的原路径；不得重试产生无限预取或改写传输状态。

## 7. 测试向量与静态门（本任务全部 NOT_RUN）

### 7.1 单元向量

- parser：默认/0/1、>1/负数/溢出/缺值；worker/off/global effective depth。
- eligibility：upload/download Pending 正常；`Completed`、`Changed`、`Failed`、`Transferring`、未知枚举、方向不符、非规范路径、endpoint/options fingerprint 缺失/不符、upload stat 失败/metadata mismatch、download manifest metadata 无效。每个拒绝均断言 no reservation、no candidate cursor advance、no pending/FD，并回到旧路径。
- reservation table：两个 worker 对同 index 竞争；普通 claim 与 candidate reserve 竞争；candidate cancel 后 index 恰好回归一次；handoff 成功一次、二次 handoff 失败；owner/generation/fingerprint stale；cancel-vs-handoff 两种胜者；close/wait 成功/失败；run cancel/worker error 清理。
- opaque identity：candidate 无可序列化控制身份；文件 `transferId`、REST ID、attempt ID 均不能作为 handoff token；固定源扫描没有控制 ID 字段依赖。

### 7.2 后续实现任务的链路向量

fresh、empty、Completed skip、no-range、resume partial、changed、manifest failure，upload/download，control reuse worker/off，server cap/TLS reject、cancel、control/data disconnect；验证退出码、源/目标 tree hash、manifest 最终状态、attempt/transfer ID、DATA frame/wire accounting 和旧 summary/event 消费者字节等价。所有动态结果在本计划中保持 NOT_RUN。

### 7.3 静态禁止门

- `runTreeScheduler` worker lambda 不得再直接写 `state.nextIndex++`；只能调用实际统一 helper。
- `claimWorkLocked`、`reserveCandidateLocked`、`cancelCandidateLocked`、`consumeCandidateLocked` 必须共享同一 `SchedulerState::mutex` 和 index 状态表。
- lookahead 源不得引用 file data client/connect、manifest 写入、framed wire 或文件级命令字符串；不得新增外部 summary/event/reason/schema 字段。
- 固定源码的 `rg -n "control_id|controlId" src include` 无匹配；`transferId` 扫描结果只能落在文件级现有位置。
- fixed-source diff 只允许本任务白名单；wire/framed、manifest/checkpoint、resume/checksum、scheduler/compression/IO、旧 runner/consumer 路径均为空。

## 8. 停止条件、性能边界和交接

出现以下任一情况立即 BLOCKED：需要新增可序列化控制身份或外部字段；候选阶段发文件级命令、建立 data FD、提前分配 transfer/attempt ID；非 Pending reservation；worker lambda 仍有旁路 `nextIndex++`；同一 index 可重复/丢失；cancel/handoff 无法在同一锁内判定；修改 wire、file clients、manifest/checkpoint/resume/checksum、scheduler/compression/IO、默认 control reuse、服务端或旧消费者；资源硬门、close/wait 或 stale fallback 无法证明。

本 slice 仍只是机制候选：worker reuse metadata-only 可能无收益；off 仅可能重叠普通 control 认证/建连；不能由计划或历史 loopback 推出 100 Mbps/100G 性能结论，也不能提高默认并行度伪装收益。

### 执行回执

- 实际输入：固定实现基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；PLAN-03 与 QA-03 已读取。
- 实际改动：只新增本结果和同名 `-last-message.md`；没有修改 PLAN-03、源码、头文件、测试、CMake、BOARD、ROSTER、决策或 index；没有创建 worktree。
- 未执行：CMake/CTest、编译、单元测试、真实 upload/download、故障注入、性能、SSH、云端和 Git 提交，均为 **NOT_RUN**。
- 实际只读门禁：`git rev-parse HEAD` 退出 0；`git status --short --branch` 退出 0；`git diff --cached --name-only` 退出 0 且为空；固定三源码 `git diff --quiet 3b0820d..HEAD` 退出 0；`rg -n "control_id|controlId" src include` 退出 1（无匹配）；PLAN-03 SHA-256 保持 `D802FBF0023FD6BFCF36635A293CF92CF411E77056F441C6F5A5020090601ECA`；本结果/摘要 UTF-8/LF/无尾随空白检查退出 0；`git diff --check` 退出 0（仅共享既有文档行尾转换提示）。
- 当前结论：v2 计划已针对 QA-03 三个阻塞收紧，等待 04 独立复审；未授权实现任务。

只有 04 PASS 且 00 另发代码任务后，才可创建独立 `codex/<task-id>` worktree。