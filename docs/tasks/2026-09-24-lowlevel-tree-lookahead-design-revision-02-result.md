# LOWLEVEL-TREE-LOOKAHEAD-DESIGN-REVISION-02 结果

日期：2026-09-24（Asia/Shanghai）
角色：01 架构与需求；独立复审：04 测试与质量；交付：00 总指挥
路线/任务：`R2026-09-24.10` / `v1`
固定实现输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`

## 1. 裁定和证据边界

本文件修订 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-01-result.md` 中被 QA-01 指出的 Critical 和 Important 缺口。唯一推荐 slice 仍为 `lookahead_depth=0`（默认）或显式 `1`，每 worker 一个候选、全 run 最多两个 pending control，未来 data FD 永远为 0。`control_reuse=worker` 只做 manifest metadata reservation；`control_reuse=off` 才可以在预算允许时将普通 control 登录推进到 ready。两种模式都不提前执行文件级命令。

本任务的源码依据是固定基线中 `tree_transfer_client.cpp` 的 `controlForFile`（约 1151–1178）、`processUploadFile`（约 1307–1474）、`processDownloadFile`（约 1479–1632）和 worker `nextIndex` 分配（约 2302–2360），以及 `file_transfer_client.cpp` `sendStream`（约 390–487）和 `file_download_client.cpp` `receiveStream`（约 374–425）、最终提交（约 690–758）。dense profile 只有本机 WSL loopback 的 12 个成功 case；它支持候选假设，不证明因果、WAN/GridFTP 或性能 readiness。动态实现、构建、测试、传输、SSH 和实验均未运行。

## 2. 五项闭合决策

### 2.1 下载候选绝不执行远端文件级命令

下载候选阶段只保存 manifest 中已有的 `file_id`/规范相对路径、方向、端点和选项指纹；不发送 `SIZE`、`MDTM`、`EPSV`、`REST` 或 `RETR`，不创建远端文件副作用，也不把权限失败作为候选 ready 的依据。候选 handoff 后，必须重新进入现有 `processDownloadFile` 顺序：`controlForFile` → 本地 Completed gate → 远端 `SIZE`/`MDTM` → acquire slot → `EPSV`/`REST`/`RETR` → 数据客户端 → 226 → mtime/最终提交 → manifest 更新。

上传也不在候选阶段发 `EPSV`、`REST` 或 `STOR`。上传可把扫描时的本地 manifest 元数据作为候选指纹，但 handoff 前必须重新 `stat`；变化即 stale/fallback。两方向唯一可提前执行的是标准 control 建连、认证和登录（仅在 `control_reuse=off` 且资源门通过时）。

### 2.2 外部 telemetry/schema 完全不扩展

本 slice 不新增 lookahead summary 对象、JSON key、schema version、JSONL event type、`control_lease_id` 或新的 reason token。真实文件若命中候选，继续使用现有事件和已有 `control_id`；候选 metadata reservation 没有 attempt/span/stream，也不造阶段行。当前严格 validator 若没有现成的命中/未命中字段，则命中、未命中、失败、取消、stale 只在实现内部计数；summary、事件日志和旧消费者输出保持原字节字段集合与旧语义。

`control_id` 是唯一控制连接生命周期标识，沿用现有类型和编码。相同 control 在 retry 中继续使用同一 `control_id`；只有真实连接关闭并重新建立才产生新的既有 `control_id`。不得用别名、lease ID 或候选 ID 替代它。现有 `control_acquire` 仅在真实文件控制阶段照原契约记录；lookahead 准备时间不塞入 `control_prepare`、`data_connect` 或 wall 分解。

### 2.3 状态、reservation、owner 和 lease 原子化

候选状态严格拆为：

```text
empty
  -> metadata_reserved
  -> control_pending
  -> control_ready
  -> in_use
  -> empty

metadata_reserved -> cancelled | failed
control_pending   -> cancelled | failed
control_ready    -> cancelled | failed
in_use            -> empty | failed
```

`metadata_reserved` 只占用一个 `(file_id, generation)` reservation，不占 control FD；`control_pending` 已调用普通 control 建连但尚未 ready；`control_ready` 有一个真实且未交接的既有 `control_id`；`in_use` 表示 lease 已一次性移交给当前文件流程。`cancelled` 和 `failed` 是终态，终态候选永不复用。`empty` 重新占用时必须递增 candidate/generation；不能把旧对象原地改回 ready。

每个候选内部保存 `run_id`、`worker_id`、`file_id`、`generation`、`candidate_id`、方向、规范相对路径、manifest 元数据指纹、端点/TLS/transfer options 指纹、owner 和可选既有 `control_id`。内部 ID 均为非负 `uint64`，`file_id` 从 manifest 下标 0 开始，`worker_id` 从 0 开始，`candidate_id` 和 generation 从 1 开始且 run 内不复用；实际 `attempt_id`、`stream_slot` 和服务端 `stream_id` 只在文件 attempt 启动后创建。外部输出不新增这些候选字段。

reservation、handoff、cancel 和 stale 检查必须在同一 scheduler mutex/原子状态转换下完成：

1. reservation 只接受同一 `(run_id, worker_id, file_id, generation)` 的一个 owner；重复 reservation 返回 `duplicate`，不覆盖原 owner。
2. handoff 只接受 `owner`、generation、路径/方向/指纹、manifest status 仍匹配且状态为 `control_ready`；成功的唯一动作是 CAS 到 `in_use` 并把既有 `control_id` 移交一次。
3. cancel/stale 与 handoff 同锁竞争，先成功者生效；失败者只能得到 `fallback`，不得关闭另一个已 in_use 的 control。
4. 第二次 handoff、owner 不匹配、generation 不匹配、指纹冲突或 `control_id` 已被消费均触发 double-use/stale 断言、关闭候选并按 depth 0 继续。
5. 当前文件结束后，候选 slot 回到 `empty`；旧 `control_id` 是否继续由原 worker runtime 持有由既有 control reuse 规则决定，不重新登记为候选 ready。

### 2.4 硬停止和 depth=0 回退

以下任一事实出现，立即关闭候选、释放 pending 名额，并以原 `lookahead_depth=0` 路径继续；不得把候选错误升级成传输失败：

- 发现或无法排除未来 data FD，或任何 data socket 在文件 transfer ID/SessionInit/ResumeResponse 前建立；
- 候选线程或接口试图提前发送 `SIZE`/`MDTM`/`EPSV`/`REST`/`STOR`/`RETR`；
- 改变 framed wire、manifest 写入顺序、checksum/resume 判定、scheduler/compression/IO backend 或默认 control reuse；
- 输出未知 telemetry key、未知 JSONL event、未知 reason token，或需新增 schema 才能表达命中；
- `control_id` 不可唯一识别、lease 不匹配、重复 handoff、owner/generation/path 指纹冲突；
- pending control 超过每 worker 1、全局 2、额外 FD 2 或内存预算；
- `getrlimit(RLIMIT_NOFILE)` 不可调用、返回失败/不可信值，或 `active_fd + pending_control + 32 > soft_limit`；
- 候选 control 登录、TLS、服务端拒绝、关闭/等待失败，或 run 取消/worker 错误无法在 terminal 前清理。

CPU 和整个进程 RSS 在本规格中只作观测项，不设通过阈值；实现不能以它们替代候选内存硬预算。

### 2.5 资源和验收向量

硬上限固定为 `depth<=1`、每 worker pending≤1、全局 pending control≤`min(worker_count,2)`、`fd_extra_peak<=2`、pending data FD=0。每个 pending control 的候选内存预算为 1 MiB，合计不超过 `2 MiB`；无法建立可靠的分配计数即不创建 `control_pending`，至少回到 metadata-only 或 depth 0。`getrlimit` 不可用时不得猜测 soft limit：显式 lookahead 直接 depth 0，保留旧输出。

最小向量必须包含 `control_reuse=off` 的 fp1/fp8、多 worker 竞争、服务端 control 上限、TLS/登录拒绝、取消、数据断连和首/末文件 idle；同时保留 upload/download、fresh、空文件、no-range、resume_partial、changed file、manifest flush 失败。CPU/RSS 采样不作为 pass/fail；FD、pending 数、hash、manifest、attempt、wire/frame 和旧输出是硬门。

## 3. 统一 upload/download 时序

候选和当前文件的时序等价伪代码如下；`prepare_candidate` 绝不调用文件级远端命令：

```text
worker claims current file i
reserve atomically next file j -> metadata_reserved(owner, generation)
if control_reuse == off and resource_gate():
    connect/auth/login ordinary control -> control_pending -> control_ready(control_id)
else:
    keep metadata_reserved only

finish current file i
claim j only if owner + generation + fingerprints still match
if control_ready: CAS control_ready -> in_use and pass existing control_id
else: cancel candidate and call original controlForFile

upload(j):  stat local path; Completed gate; EPSV; optional REST; STOR;
             runFileTransferClient; wait 226; update manifest as before
download(j): controlForFile; local Completed gate; SIZE/MDTM NOW;
             EPSV; optional REST; RETR; runFileDownloadClient;
             wait 226; mtime/final commit; update manifest as before

on any mismatch/error/cancel: close candidate, release budget, use depth=0 path
```

上传的本地 stat 仅是快速指纹，不能替代 handoff 前 stat。下载的远端 `SIZE`/`MDTM` 明确在 handoff 后执行，候选不承担远端权限或 changed 判定。`EPSV`、`REST`、`STOR`/`RETR`、transfer ID、data SessionInit、ResumeResponse、missing range、stream ID、226 和文件提交都属于当前 attempt；候选阶段没有 data span。空文件按原成功路径，不打开 pending data FD；完整 Completed skip 不创建候选或在 gate 前取消；no-range 仍是实际 attempt，不改写成 attempt 0。

## 4. 纯函数与状态机验收表

实现前可在不触碰网络的单元测试中验证以下纯函数/断言。表中的 `fallback` 不改变原传输结果，只关闭 lookahead。

| 输入/操作 | 预期状态/返回 | 必须拒绝或断言 |
|---|---|---|
| Pending record、空 slot、owner=w、generation=g | `metadata_reserved` | Completed/Changed、路径不规范、方向或 options 指纹不符 |
| metadata reservation + `control_reuse=worker` | 保持 `metadata_reserved`（plan-only） | 创建第二 control FD |
| metadata reservation + off + 预算通过 | `control_pending`，成功登录后 `control_ready(control_id)` | data FD、文件级远端命令 |
| 同一 file/generation 第二 owner reservation | 返回 `duplicate`，原 owner 不变 | 覆盖 owner 或推进 manifest index |
| ready handoff，owner/generation/fingerprint 全等 | 原子到 `in_use`，handoff_count=1 | 产生新 control ID |
| ready handoff 二次调用或 owner/generation 错配 | `fallback`，候选 cancelled/failed | 二次交付同一 control |
| cancel 与 handoff 并发 | mutex 先赢者唯一生效；后者 fallback | 回写状态、关闭已 in_use control |
| upload stat 或 download handoff 后 SIZE/MDTM 改变 | candidate stale/cancelled，原流程判 Changed/失败 | 复用 stale control 到 retry |
| control/TLS/服务端拒绝 | candidate failed，当前文件 depth=0 | 重试预取风暴、改 transfer status |
| `getrlimit` 失败、FD 或内存超预算 | 不进入 control_pending，depth=0 | 猜测预算继续开 socket |
| run cancel、worker 首错、manifest flush 失败 | pending/ready cancelled 并 close/wait；in_use 由当前文件收尾 | 孤立 thread/FD/lease |

生命周期互斥断言：`metadata_reserved` 不得有 `control_id`；`control_pending` 可以有未 ready 的内部 socket，但不得交付；`control_ready` 必须有唯一既有 `control_id` 且 handoff_count=0；`in_use` 必须有唯一 owner 且 handoff_count=1；`cancelled/failed/empty` 不得被传给文件函数。候选内部计数只能在 mutex 下递增，外部不序列化。

## 5. 实现白名单与禁止范围

后续 03 计划只能申请以下文件范围，且每项需在计划中列出具体函数和测试：

- `include/cpnetflux/config/tree_transfer_options.h`、`src/config/tree_transfer_options.cpp`：增加 `lookahead_depth` 选项，默认 0，仅接受 0/1；不改现有默认、scheduler、compression、IO 或单文件选项。
- `include/cpnetflux/core/io/tree_lookahead.h`、`src/core/io/tree_lookahead.cpp`（如采用独立模块）：仅保存 metadata reservation、control candidate 状态、预算和原子 handoff；不得调用数据客户端或发送文件级命令。
- `src/core/io/tree_transfer_client.cpp`：只在 worker scheduler 与 `controlForFile` 的 control 对象交界处接入可选 candidate；upload/download 的文件级调用顺序必须保持现状。
- 与上述纯函数直接对应的现有 `tests/unit`/tree scheduler 定向测试文件，以及必要的 CMake 测试注册；测试不得修改协议 fixture 或生产默认。

禁止修改 `src/core/io/file_transfer_client.cpp`、`src/core/io/file_download_client.cpp`、任何 framed data/control protocol、manifest/checkpoint/resume/checksum 实现、scheduler/compression/IO backend、服务端、`tools/demo/run_alpha_demo.py`、旧结果、BOARD、ROSTER、决策文件或云端目录。禁止新增 `control_lease_id`、lookahead JSON 对象、JSONL event type、reason token、未来 data socket、预发 SIZE/MDTM/EPSV/REST/STOR/RETR、第二 worker control、global scheduler 预取、无界连接池和 retry 语义改变。

## 6. telemetry、资源采样与兼容

外部输出保持现有 schema、键集合和旧消费者可读性：depth=0 和 depth=1 都不新增 lookahead 字段。候选内部可保留非持久计数 `prepared/ready/hit/miss/fallback/failed/cancelled/stale`，仅供调试器或测试内存对象读取；实现不把它们写入 summary/JSONL。若既有 `control_acquire` 已定义可选 reason 且 validator 已接受某个 lookahead reason，03 才能沿用该既有枚举；本资料没有证明有这样的枚举，因此默认不发新 reason。真实文件的既有 event 继续带既有 `control_id`，候选没有 event。

资源采样位置固定为：启用前读取 `getrlimit(RLIMIT_NOFILE)`；创建 control socket 后、candidate handoff 前、候选关闭后分别记录内部 active FD/extra FD 峰值；每次 transition 计数当前 pending；allocator 创建/释放 control candidate 状态时计入 1 MiB/候选预算。`getrlimit` 失败即 depth 0。Linux 上 RSS/CPU 可在 run 开始、pending 峰值和 run 结束采样到实验报告，但它们只是观测值，没有规格阈值，不能替代 FD/内存硬门。

因为 lookahead 不扩展 wire 或 telemetry schema，旧服务端只看到原 control 登录和后续原文件命令；额外 control 被服务端拒绝时直接关闭候选并回退。旧 `run_alpha_demo.py` 继续只读取既有字段，无法看到命中也不会因未知字段或新 event 失败；这正是本修订选择“内部计数、外部输出不变”的兼容策略。

## 7. 验收顺序和硬门

### 实现前门

1. 04 独立确认下载零远端预取、无新增 schema/key/event、`control_id` 唯一映射和状态/资源纯函数表。
2. 03 提交只含白名单文件的实现计划；计划必须列出每个硬停止分支、close/wait 顺序、`getrlimit` 失败回退和原 controlForFile fallback。
3. parser/状态单测、选项单测和静态源码审查先通过；任何未知 telemetry key、文件级早发命令或 wire/manifest/resume diff 都停止，不创建性能实验任务。

### 实现后最小矩阵

| 向量 | 组合 | 硬断言 |
|---|---|---|
| A/B dense | 原 128×1 MiB，upload/download × fp1/fp8 × 3，depth 0/1 | exit、源/目标 tree hash、file_count、logical bytes、manifest 终态、wire/frame 计数和旧 JSON 字段一致；worker reuse 不增加额外 control/data FD |
| control off | upload/download，control_reuse=off，fp1/fp8，多 worker | 至少覆盖 ready hit、reservation 竞争、服务端 control cap；超 cap 只 fallback，绝不改变 transfer/integrity |
| rejection/cancel | TLS/登录拒绝、control 断连、数据断连、run cancel、首/末文件 idle | pending≤2、extra FD≤2、data FD=0、close/wait 完成、无 double-use/孤立资源 |
| file semantics | fresh、空文件、Completed skip、no-range、resume_partial、changed file、manifest flush 失败 | 原 attempt/manifest/resume/mtime/hash 语义不变；no-range 不是 attempt 0，changed 不复用 stale |
| resource | connections=1/8、fp1/fp8 | `fd_extra_peak<=2`、candidate memory≤2 MiB；`getrlimit` 不可用时 depth=0；CPU/RSS 只记录不判通过 |

本修订不把任何上述运行结果写成已通过；当前所有动态项均 `NOT_RUN`。A/B wall、阶段和 CPU/RSS 的观察应在 correctness/资源硬门后进行，不能以阶段 sum 伪造 wall；本地结果也不能直接外推 WAN/GridFTP。

## 8. 旧设计的删除和替换清单

1. 删除旧 §3 的句子“下载候选的远端 SIZE/MDTM 只能作为 preflight 记录，交接时仍需按原路径复核”；替换为本文件 §2.1 的“候选阶段一律不发 SIZE/MDTM，handoff 后才按 `processDownloadFile` 执行”。
2. 删除旧 §6 的整段 `{"lookahead":...}` summary 示例、`prepare_elapsed_ns`、`pending_peak` 等新增外部字段；替换为 §2.2/§6 的“内部计数、外部输出不变”。
3. 删除旧 §6 中 `lookahead_hit`、`lookahead_miss`、`lookahead_fallback` 可直接写入 `control_acquire` 的表述；除非已有 validator 枚举明确允许，否则不发任何新 reason token。
4. 删除旧 §4/§6 所有 `control_lease_id` 作为新增标识的表述；替换为既有唯一 `control_id`，候选 ID 只在内存内部存在。
5. 将旧 §5“resume_partial 可以预建 control”的宽泛句收紧为：仅 control 登录可预建；不做任何远端元数据、数据连接或 range 决策，handoff 后原流程复核。
6. 将旧验收中 `memory_peak_bytes`、`fd_extra_peak` 作为外部 summary 字段的句子替换为内部采样和硬门；CPU/RSS 明确为观测项，不设通过阈值。

## 9. 当前交付和下一步

- 实际输入：本任务单、原设计结果、QA-01 结果、固定源码路径；固定实现基线 `3b0820d...`，资料 HEAD `a076c532...`。
- 实际输出：仅新增本文件；未修改源码、测试、runner、CMake、决策、BOARD、ROSTER、云端或 Git index。
- 实际动态工作：构建、CTest、传输、性能、SSH、故障注入和 RSS/CPU/FD 运行采样均 `NOT_RUN`。
- 当前状态：本结果提供唯一推荐契约，但不授权 03 直接实现；等待 04 独立复审。若 04 仍发现现有 schema 无法表达而实现试图添加字段，硬停止并回到 depth 0。
- 下一步：00 将本文件交 04 复审；只有 QA PASS 后，才允许 03 依据白名单形成窄实现计划，再由 00 决定是否创建 worktree。
