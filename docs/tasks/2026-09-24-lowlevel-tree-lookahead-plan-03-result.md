# LOWLEVEL-TREE-LOOKAHEAD-PLAN-03：窄实现计划

- 路线/任务版本：`R2026-09-24.10 / v1`
- 执行角色：03 核心实现；独立复审：04 测试与质量；验收：00 总指挥
- 计划状态：**仅设计，待 04 复审；不授权代码、worktree、构建或实验**。
- 固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- 当前资料工作树 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`

## 1. 输入、边界与源码事实

本计划读取了任务单、`docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-lookahead-revision-qa-02-result.md`、`docs/coordination/BOARD.md`，以及固定基线中的选项、树调度和文件处理源码。QA-02 的结论是 PASS，但只允许形成窄实现计划；动态测试、真实传输、性能和故障注入仍为 NOT_RUN。

当前共享工作树仍是资料树，包含其他角色的 dirty 文档；没有操作 index。三个固定源码文件相对固定输入的 parity 已核对为相同。源码中的行号是当前资料树锚点，实施前须再次对固定 commit 校验，若函数边界改变即停止。

| 位置 | 当前函数/范围 | 计划接缝 | 不能触碰的语义 |
|---|---|---|---|
| `include/cpnetflux/config/tree_transfer_options.h:40-73` | `TreeTransferOptions` | 只增加 `lookaheadDepth`（默认 0）的内部配置字段 | 现有 `controlReuseMode`、scheduler、compression、checksum、resume、TLS 默认不变 |
| `src/config/tree_transfer_options.cpp:162-466`、`:469-485` | `parseTreeTransferOptions`、usage | 只解析 `--lookahead-depth 0|1`；usage 同步显示 | 不改变既有选项解析或单文件选项 |
| `src/core/io/tree_transfer_client.cpp:1032-1060` | `SchedulerState`、`nextIndex` 互斥区 | 挂接一个 run 级 lookahead owner/budget；仍由原 `state->mutex` 保护 manifest/index | 不改 manifest 状态、失败优先级或 worker 数量 |
| 同文件 `:1151-1183` | `controlForFile` | 在获得当前文件控制对象前尝试一次候选 handoff；失败回到原函数 | 不改变 `ControlClient` 命令时序和 `control_id` 语义 |
| 同文件 `:1307-1474` | `processUploadFile` | 只由 scheduler seam 传入已验证控制对象/候选；handoff 后执行原 upload 顺序 | 不在候选阶段发 `EPSV/REST/STOR`，不改 retry、manifest 或 resume |
| 同文件 `:1479-1632` | `processDownloadFile` | 同上；远端检查只能在 handoff 后原地执行 | 不在候选阶段发 `SIZE/MDTM/EPSV/REST/RETR` |
| 同文件 `:2099-2290` | `runGlobalTreeScheduler` | 显式保持 depth=0；不接入 global prefetch | 不做 global scheduler 预取、第二 worker control 或无界池 |
| 同文件 `:2302-2364`、入口 `:2425`/`:2509` | `runTreeScheduler`、upload/download 入口 | 仅普通 worker scheduler 创建一个 run 级 bounded lookahead | 不改变 scheduler mode、parallelism、summary 或旧事件 |
| `include/cpnetflux/core/io/tree_lookahead.h`（新增，若采用独立模块） | 纯状态、指纹、预算、handoff API | 不依赖 data client、control protocol 命令或 manifest 写入 | 不导出外部 schema、reason、summary 或 `control_lease_id` |
| `src/core/io/tree_lookahead.cpp`（新增） | 上述实现 | 同一 mutex 下管理状态和资源硬门；网络 close/wait 在锁外完成 | 不创建 data socket；worker reuse 不创建第二 control |
| `tests/unit/tree_transfer_options_test.cpp` | 现有选项单测 | 增加 depth 0/1/>1 向量 | 不把 parser 单测当真实传输证明 |
| `tests/unit/tree_lookahead_test.cpp`（新增） | 纯函数/状态及预算单测 | 覆盖 race、stale、close/wait 和资源回退 | 不替代真实 upload/download 链路测试 |
| `CMakeLists.txt:85-139` 与测试源列表 `:289-340` | 将新增 `tree_lookahead.cpp` 纳入 core、注册窄单测 | 只增加本模块和上述单测的必要构建/测试条目 | 不改 runner、旧 smoke、协议或其他生产目标 |

`src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_client.cpp`、任何 framed/control protocol、manifest/checkpoint/resume/checksum、服务端、`tools/demo/run_alpha_demo.py`、旧消费者和默认配置均不在白名单内。若实现需要改其中任何文件，本计划立即停止，不能通过“顺便修复”扩大范围。

## 2. 选项和有效配置

新增选项只为目录客户端：`lookahead_depth`/`--lookahead-depth`，默认 `0`，显式值只接受 `0` 或 `1`。解析 `>1`、负数、溢出和缺值均返回既有 invalid-argument 结果，传输不启动；即使调用方绕过 parser，运行时也必须把任何非 0/1 值的 `effective_depth` 设为 0，而不是向上放宽。这个双层保护不能改变现有默认值。

- `depth=0`：完全走当前 worker/control 路径，候选模块不分配候选、不建额外 control。
- `depth=1, control_reuse=worker`：每 worker 最多一个 **metadata-only** candidate；不建立第二 worker control，不进入 `control_pending`。
- `depth=1, control_reuse=off`：预算通过时每 worker 最多一个 candidate，可异步推进一个普通 control 的认证/login 到 `control_ready`；全 run `control_pending` 不超过 2。候选不会携带新的外部 lease/id。
- `scheduler=global`：本 slice 强制 depth=0；不把 global scheduler 的计划队列变成预取器。

选项值只影响内部候选行为；summary JSON、event log、旧键集合、reason 枚举和 wire 字节均不增加字段或事件。

## 3. 内部对象、状态机和并发边界

候选对象建议包含：`run_id`、`worker_id`、`file_index/file_id`、`candidate_id`、`generation`、方向、规范相对路径、manifest metadata fingerprint、endpoint/TLS/transfer-options fingerprint、`owner`、可选既有 `control_id`、当前状态和 handoff 次数。内部 ID 为 `uint64`：`file_id/worker_id` 从 0 开始，`candidate_id/generation` 从 1 开始且 run 内不复用；真实 `attempt_id`、stream slot 和服务端 stream ID 只在当前文件 attempt 创建，candidate 不抢占这些 ID。

状态是以下闭合图，终态对象不复用；释放后的 slot 重新 reserve 必须递增 generation/candidate_id：

```text
empty
  -> metadata_reserved
  -> control_pending       (仅 control_reuse=off 且资源闸门通过)
  -> control_ready         (普通 control login 成功，带唯一既有 control_id)
  -> in_use                (一次性 handoff)
  -> empty | failed        (当前文件收尾后释放 slot；对象本身不回生)

metadata_reserved/control_pending/control_ready -> cancelled | failed
```

严格断言：`metadata_reserved` 没有 control ID/FD；`control_pending` 可以有一个尚未 ready 的普通 control FD，但不可交付；`control_ready` 必须有唯一既有 `control_id`、handoff 次数为 0；`in_use` 必须有唯一 owner、handoff 次数为 1；`cancelled/failed/empty` 不能传给文件函数。

所有 reserve、状态转换、owner/generation/fingerprint 校验、pending 计数和 handoff/cancel/stale 决定在同一个 `LookaheadState` mutex 下完成。`reserveNextCandidate` 也必须在这个锁内从尚未 claim/reserve 的 manifest index 中选取 j，登记 `(file_id, owner, generation)`；普通 `nextWorkItem` 领取时跳过已登记的 j。这样两个 worker 不能预订同一个文件，候选取消时再在锁内归还 j，handoff 成功时由该 owner 消费 j。handoff 采用“校验后状态从 ready 到 in_use 的单次逻辑 CAS”；二次 handoff、重复 reservation、owner/generation/fingerprint 不匹配直接返回 stale/double-use，并关闭候选、回到 depth=0。若实现采用原子 pending 计数，只允许在同一状态转换边界用 `compare_exchange` 抢占名额，失败必须回滚计数且不能留下 candidate；禁止在锁外修改对象状态或计数。

网络 connect/auth/login、close/wait 和可能阻塞的等待不持有状态 mutex：先在锁内占用/转移名额，锁外做操作，再带 generation/owner 回锁提交结果。cancel 与 handoff 同锁竞争，先成功者决定：cancel 先赢则 handoff 返回 stale；handoff 先赢则候选成为 in_use，随后取消只终止当前文件的原有流程。close/wait 失败将候选标为 failed、释放名额并触发 depth=0 fallback，不能伪装成当前文件传输失败或改写 manifest。

`fingerprint` 必须覆盖方向、规范路径、manifest 记录关键元数据、endpoint、TLS 和与 control 建连相关的 options。上传 handoff 前仍需重新 `stat`；下载远端 `SIZE/MDTM` 仍在 handoff 后获取。指纹不相等只表示 stale/fallback，不得覆盖 manifest 或提前标记 Changed。

## 4. 受控插入顺序和两方向伪代码

候选的唯一目的，是在当前文件仍运行时保存下一文件的 metadata 或（只在 `control_reuse=off` 时）普通 control login。候选阶段禁止任何文件级命令、data FD、transfer ID、range、SessionInit 或文件副作用。

### 4.1 worker 流程

```text
worker w:
  i = 原 nextIndex 互斥分配（领取时跳过已被其他 worker reserve 的 j）
  j = 在同一 mutex 中从未 claim/reserve 的下一个 manifest index 预订（最多一个）
  若 depth=1 且 j 不是 Completed/不越界：reserve(j, fingerprint, generation)
  若 mode=worker：只保留 metadata_reserved
  若 mode=off 且 resource_gate()：启动一个有界普通 control connect/auth/login
      pending -> ready(existing control_id)，否则 failed/fallback
  processFile(i) 完全按旧路径运行
  processFile(i) 返回后，worker 尝试 handoff(j)：
      只有 owner/generation/fingerprint/status=ready 全部匹配才 CAS ready->in_use
      任何不匹配均关闭候选，按 depth=0 调用原 controlForFile
  下一循环只使用 j 的既有候选或原路径；不跳过 manifest index
```

若候选准备需要后台任务，只能由 lookahead 模块持有每 worker 一个 bounded task；其生命周期必须在 run cancel/worker error 时 close/wait。不能以额外 worker control、全局线程池或 global scheduler 队列代替这个上限。

### 4.2 upload

```text
candidate(j):
  读取并保存本地 manifest metadata fingerprint（不发 control/data 命令）
  handoff 前重新 stat；路径/mtime/size/fingerprint 变化 => stale/fallback

handoff(j):
  取得现有 worker control 或已 ready 的普通 control
  回到 processUploadFile 原顺序：
    Completed gate/已有 remote 完成校验
    acquireTransferSlot
    EPSV -> (resume 时 REST) -> STOR
    runFileTransferClient（原 checksum/resume/chunk/TLS）
    waitTransferComplete（原 226/完成等待）
    updateRecord/manifest 与原错误、retry、释放顺序
```

上传候选不允许预发 `EPSV`、`REST`、`STOR`，也不允许预创建 data socket。handoff 失败只关闭候选并让原 `controlForFile` 重新建连；不增加 attempt、不改变 transfer ID 或 retry 语义。

### 4.3 download

```text
candidate(j):
  只保存 manifest file_id、规范 remote path、方向、endpoint/TLS/options fingerprint
  不发 SIZE/MDTM/EPSV/REST/RETR，不做 remote changed 判定

handoff(j):
  取得现有 worker control 或 ready 的普通 control
  回到 processDownloadFile 原顺序：
    local Completed gate
    remote SIZE -> MDTM -> resume/changed 判定
    acquireTransferSlot、建本地父目录
    EPSV -> (resume 时 REST) -> RETR
    runFileDownloadClient（原 checksum/resume/TLS）
    waitTransferComplete，mtime/final commit，manifest update
```

空文件、Completed skip、no-range、resume partial、changed file 和 manifest failure 都必须经过原文件状态机；candidate 不能把这些情况折叠成“准备成功”，也不能提前创建 data FD。`control_reuse=worker` 仅 metadata reservation，`control_reuse=off` 才可能有最多两个额外 pending control；两方向均保留既有 `control_id`。

## 5. 资源硬门和采样

有效 depth 只有在所有硬门同时成立时才为 1，否则立即 depth=0 并保留原输出：

- `depth <= 1`；每 worker candidate/reservation `<= 1`。
- 全 run `control_pending <= min(worker_count, 2)`；不得因为 worker 数增加而扩容。
- `fd_extra_peak <= 2`，只允许 pending ordinary control；**data FD 永远为 0**。
- 每个 pending candidate 的可计量内存预算 `<= 1 MiB`，全 run candidate 状态合计 `<= 2 MiB`；无法可靠计量时不进入 `control_pending`，至少 metadata-only 或 depth=0。
- Linux 启用前调用 `getrlimit(RLIMIT_NOFILE)`；调用失败、返回不可信值，或 `active_fd + pending_control + 32 > soft_limit`，显式 depth=1 直接回到 depth=0，不猜测 soft limit。
- server cap、TLS/login reject、connect/close/wait error、cancel、worker first error、manifest flush failure、owner/generation/fingerprint mismatch 都释放候选名额并回到原路径；不得重试制造预取风暴。

采样点只用于实施后证据，不是通过门：run 启用前读取 soft limit/active FD；candidate reserve、control socket 建立后、handoff 前、candidate close/wait 后记录 active FD、pending 数和峰值；candidate allocator reserve/release 记录预算；Linux 可在 run start、pending peak、run end 采样 RSS/CPU。RSS/CPU 不增加 summary/event，不取代 FD、内存和 data-FD 硬断言。

## 6. 测试和静态边界清单（本计划均 NOT_RUN）

### 6.1 纯函数和选项

1. parser：默认/显式 `0` 保持旧行为；显式 `1` 只打开 bounded slice；`>1`、负数、溢出、缺值和未知 token 拒绝；`control_reuse=worker/off` 与 scheduler global 的 effective depth 矩阵。
2. 状态：正常 `reserve -> pending -> ready -> in_use -> empty`；metadata-only；duplicate reservation；double handoff；owner/generation/fingerprint stale；cancel-vs-handoff 两种先后；close/wait 成功与失败；终态不可复用。
3. 预算：per-worker=1、global=2、worker 数 1/2/8 竞争；FD、memory、`getrlimit` 失败和回退；data-FD 计数始终 0。

### 6.2 定向错误向量

- service control cap、TLS/auth/login reject、connect 断连、candidate cancel、worker 首错、run cancel、close/wait 失败；验证候选关闭、名额归还、原文件路径继续、无孤立线程/FD。
- upload/download：empty、Completed skip、no-range、resume partial、changed source/remote、manifest write/flush failure；验证候选不提前命令，handoff 后原顺序、attempt/transfer ID、manifest/resume/mtime/hash 语义不变。
- multi-worker：不同 owner 竞争同一 next index、generation 过期、两个 worker 同时争全局第二 pending；必须保持一个 owner 和最多两个 pending。

### 6.3 静态和后续链路门

实现任务应提供窄静态检查：lookahead 模块不得引用 data client/connect、`SIZE/MDTM/EPSV/REST/STOR/RETR` 或 manifest 写入；变更路径只能来自本计划白名单；固定基线对 wire/framed、manifest/checkpoint、resume/checksum、scheduler/compression/IO 文件的 diff 必须为空；summary/event JSON 键集合、旧 reason 和现有 `control_id` 编码不变。用源代码断言验证 global scheduler 未接入预取和默认 depth=0。

上述单测不能证明真实链路。只有后续独立实现任务才可按现有 CTest/树 smoke 入口运行 upload/download、control reuse、resume、changed、edge case，并对 depth=0/1 做退出码、源/目标 tree hash、manifest 最终状态、DATA frame/wire accounting 和旧 summary/event 消费者等价；动态测试在本计划中全部 **NOT_RUN**。

## 7. 停止条件、风险和性能边界

以下任一事实立即停止计划，不进入实现：出现未知 schema/key/event/reason 或必须新增 summary/JSONL 字段；候选阶段发送任一文件级命令；创建未来 data FD、第二 worker control、global scheduler 预取或无界连接池；修改 framed wire、manifest/checkpoint/resume/checksum、scheduler/compression/IO backend、单文件 API、默认 control reuse 或服务端；改变 retry/attempt/transfer ID/取消/失败语义；`getrlimit`、预算、close/wait 无法可靠处理；静态白名单或 fixed-source parity 不成立。

本 slice 的性能含义仅是候选假设：`control_reuse=worker` 为 metadata-only，可能完全没有收益；`control_reuse=off` 最多把普通 control 的认证/建连等待与当前文件工作重叠，实际收益取决于服务端 cap、TLS 和 RTT。任何 wall、CPU 或吞吐改善都必须由后续固定数据集和真实链路测量证明，不能从本计划或历史 dense profile 推出 100 Mbps/100G 结论，也不能通过提高默认 parallelism 伪装收益。

## 8. 执行回执与门禁状态

- 实际输入：源码固定 commit `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 实际改动：仅新增本结果与 CLI 摘要两个文档；未创建 worktree，未改源码、测试、CMake、BOARD、ROSTER、决策或 index。
- 已执行只读命令：`git rev-parse HEAD`（0）、`git status --short --branch`（0）、`git diff --cached --name-only`（0，空）、三个固定源码 `git diff --quiet 3b0820d..HEAD`（0）、`git diff --check`（0；仅有共享既有文档的行尾转换提示）。结果和摘要 UTF-8/LF/无尾随空白检查均为 0。
- 未执行：CMake、CTest、编译、parser/状态单测、真实 upload/download、控制拒绝/取消/断连、hash/manifest/resume、wire/frame 对等、RSS/CPU/FD 性能、SSH、云端动作和实验，均为 **NOT_RUN**。本轮没有暴露可调用的外部 `--output-last-message` CLI；摘要按任务指定路径直接写入，未伪造 CLI 退出码。
- 当前结论：计划可供 04 独立复审；不授权实现。只有 04 PASS 且 00 另发实现任务后，才可创建 `codex/<task-id>` worktree。

## 9. 后续交接

04 先审查选项 >1 处理、状态 race、global scheduler depth=0、普通 control login 的资源上限、两方向 handoff 后命令顺序和静态禁止项。若 04 发现契约与固定源码冲突，保留本计划和证据，标记 BLOCKED，等待 00/01 更新路线；不得在本计划内自行改协议或实现。
