# LOWLEVEL-TREE-LOOKAHEAD-QA-01 独立质量审查

日期：2026-09-24
路线/任务版本：R2026-09-24.9 / v1
角色：04 测试与质量；验收：00 总指挥

## 独立结论

结论：**PARTIAL，暂不授权 03 创建实现 worktree**。设计已正确限制为默认关闭、`lookahead_depth<=1`、每 worker 一个候选、全局最多两个 pending control，且明确不预建未来 data socket；但下载候选的 SIZE/MDTM 时序和 telemetry summary 扩展尚未形成可机械验收的单一契约。资源预算、候选状态与实现白名单也仍缺少可执行断言。设计审查不等于代码、测试或性能通过。

## 输入、状态与门禁

- `git rev-parse HEAD`：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，退出码 0。
- `git diff --cached --name-only`：空，退出码 0；未操作 index。
- 设计结果 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-01-result.md`：SHA-256 `2164EF84AEBFCD58F83C92928A8CDA9991048593330BE346355D506E3E3968AE`；读取长度 18,380 bytes，重复读取期间 mtime/哈希稳定。
- 固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 与资料 HEAD 间指定的三个源码文件无差异；相对当前 HEAD 的同三文件也无差异，两个 `git diff --quiet` 均退出 0。
- `git diff --check`：退出码 0。共享工作树已有他人文档改动，未覆盖、暂存或清理。
- 未运行 CMake、CTest、传输、profile、SSH、云端实验或清理；以下是文档/源码只读审查。

## 分项结论

| 审查项 | 结论 | 依据与缺口 |
|---|---|---|
| 目标、非目标、默认 off、depth 及资源上限 | PASS | 设计 §1–2（约第 11、25–34 行）冻结 depth 0/1、每 worker 1、全局 pending≤2、data FD=0，并拒绝无界池和第二条 worker control。 |
| 控制候选状态、ID/generation/attempt | PARTIAL | §4（约第 52–69 行）定义 candidate/generation/lease 和 stale 回退；但 `plan_only` 与 `control_pending` 共用 `ready=登录成功`，前者没有登录控制，状态含义和重复 reservation 的原子边界需拆开。候选到既有 event/summary 的 ID 映射也未冻结。 |
| upload/download 边界与副作用 | BLOCKED | §3 第 44 行禁止候选发所有 SIZE/MDTM；第 48 行又允许下载候选使用远端 SIZE/MDTM preflight 记录。这会改变候选 ready、权限失败、stale 判定和重试语义，必须选择一个时序。 |
| data socket、transfer ID、range、wire/manifest | PASS（设计层） | 第 11、38、44、48 行明确 data socket=0，EPSV/传输命令、transfer ID、SessionInit/ResumeResponse、range 均留在原路径；不改 wire、manifest、checksum/resume、scheduler/compression。实现尚未验证。 |
| control reuse、并发、取消和旧服务回退 | PARTIAL | 第 69、81、87 行限制 worker reuse 不开第二 control，控制拒绝/TLS/FD/内存失败回退；但 control_reuse=off、多 worker 竞争、服务端 control cap、异步关闭/取消等待的可执行向量未完整冻结。 |
| telemetry/summary 兼容 | BLOCKED | 第 91–101 行新增可选 `lookahead` 对象并要求“04 确认字段白名单”，同时允许 strict validator 不接受时写旧字段且 `evidence_status=partial`。QA-08 仍为 PARTIAL，未冻结 schema/version、严格 key/null/type、未知字段、旧消费者行为，以及 `control_lease_id` 与既有 `control_id` 的关系。不能以此进入实现。 |
| 验收矩阵与资源门 | PARTIAL | 第 111–124 行覆盖 dense、hit、fresh/no-range/resume/changed、失败取消、FD/RSS/CPU 和 hash/manifest；但 dense 仅 12 个 WSL loopback case，正式 10-pair overhead 未完成；缺 control_reuse=off 的 fp8/多 worker/服务端 cap 组合、可重复 auth/TLS reject 和明确 CPU/RSS/FD 取样断言。 |
| 实现白名单、停止条件、可回退性 | PARTIAL | 第 87、105–109 行给出类型和函数边界，但没有逐文件白名单、禁止改动清单及遇到 schema/data-FD/早发 STOR/RETR/manifest 改写时的硬停止条件。 |

## 源码时序核对

当前固定源码中，`ensureControlReady` 在 `tree_transfer_client.cpp:475` 负责连接、认证和登录；`controlForFile` 在 `:1151–1179` 只返回现有 worker control 或临时 control。worker 调度在 `:2302` 后通过 `nextIndex`（`:1046–1050`、`:2347–2350`）分配文件。下载文件在 `:1479–1534` 先取得 control，再进行远端 SIZE/MDTM、EPSV、REST/RETR。源码没有 lookahead 状态或 pending pool。

因此设计可以安全保留“只准备 control、handoff 后沿用原文件顺序”的方向，但必须解决上述 SIZE/MDTM 矛盾；否则无法判断候选是否已产生远端副作用，也无法证明 stale/取消/失败回退不改变旧语义。

## 证据边界

设计引用的 dense profile 是 WSL2 loopback、本机隔离服务、128×1 MiB、upload/download×fp1/fp8×3，共 12 case，退出和 hash 均成功，但 fp8 没有 wall 收益。它只能支持候选假设，不支持 WAN、GridFTP、100 Mbps 或生产 readiness。`LOWLEVEL-TREE-TELEMETRY-QA-08` 仍缺 no-range/resume on/off 等价、正式 10-pair overhead 以及 wire/frame/manifest 运行时等价；这些不能被本设计矩阵的静态条款替代。

## 阻塞与风险分级

### Critical

1. 统一下载候选的 SIZE/MDTM 政策：建议候选阶段不发远端文件级命令，handoff 后严格沿用原 `processDownloadFile` 顺序；若坚持 preflight，必须定义权限/失败/mtime 竞态、副作用和重试边界，并给出对应 fixture。
2. 冻结 lookahead summary/telemetry 契约，或移除扩展：schema/version、严格键集、null/type、未知键行为、depth=0 序列化、旧消费者兼容及 `control_lease_id`/`control_id` 映射必须有可执行向量。QA-08 未提供这些运行态证据。

### Important

1. 将 `metadata_reserved` 与 `control_pending/ready/in_use` 分离，规定同一 manifest/file/generation 的原子 reservation、owner、lease double-use、cancel race 和 stale generation 断言。
2. 具体化资源门：active FD 的测量点、`getrlimit` 不可用时的行为、TLS/登录失败后的 close/wait、RSS/候选内存采样和超限强制 depth=0；证明 pending 不越过 2 且 data FD 始终为 0。
3. 扩充矩阵：`control_reuse=off` 的 fp1/fp8、多 worker 竞争、服务端 control 上限、认证/TLS 拒绝、取消和首/末文件 idle；逐项断言 exit、每文件/tree hash、canonical manifest、attempt、wire/frame 计数。CPU/FD/RSS 阈值若暂不冻结，应明确仅为观测项。
4. 写出实现白名单（允许的头/源/测试/构建文件）和硬停止条件：任何早发 EPSV/REST/STOR/RETR、未来 data FD、manifest/checksum/resume/scheduler/compression 改动或 strict schema 不一致即停止并回退。

### Minor

- 明确 `unsupported_combination` 的稳定错误码/summary 表示及旧服务端拒绝时的可观测原因；明确 `candidate_id` 跨 run 不复用的持久化/日志位置。

## 最小修订后门禁

00/01 先冻结上面两个 Critical 契约；03 再提交仅含白名单文件的实现计划，包含状态机/资源/ID 的纯函数和故障向量。04 复审通过后，才可创建独立 worktree。实现阶段必须运行 parser/状态回退/旧消费者、telemetry on/off hash/frame/manifest、fresh/no-range/resume/changed、取消/拒绝/断连及资源/overhead 门；本轮不授权其中任何一项。

## 执行回执

- 实际输入/输出 commit：输入资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本轮仅新增本结果和独立 CLI 摘要，未提交。
- 实际改动：仅本任务允许的两个文档文件；未改源码、BOARD、ROSTER、任务单、index 或云端。
- 实际命令、退出码与证据：`git rev-parse HEAD` 0；`git diff --cached --name-only` 0/空；三源码 parity 两次 `git diff --quiet` 均 0；`git diff --check` 0；设计结果 hash/稳定性如上。
- transfer / integrity / evidence / wire accounting：仅核对设计契约和源码时序；无运行态 transfer、integrity、evidence 或 wire 结果。
- 失败/跳过/阻塞：动态构建、测试、传输、profile、SSH、实验均未运行；因两个 Critical 契约及资源/白名单缺口阻塞实现授权。
- 下一角色：00 收口 Critical 修订后，派 03 写窄实现计划；04 再独立复审，不得把本回执写成产品或性能验收。

验收人：00 总指挥（待验收）。
