# LOWLEVEL-TREE-LOOKAHEAD-REVISION-QA-02 独立复审

日期：2026-09-24
路线/任务版本：R2026-09-24.10 / v1
角色：04 测试与质量；验收：00 总指挥

## 结论

结论：**PASS（仅允许 03 创建窄“实现计划”任务）**。REVISION-02 已逐项闭合 QA-01 的两个 Critical 和主要 Important 阻塞：候选阶段不再发送任何文件级命令；外部 telemetry/summary/JSONL schema 完全不扩展；状态、owner/generation/handoff/cancel race、资源硬门和实现白名单均给出可机械验收条款。此结论不授权写代码、创建实现 worktree、构建、运行测试、传输或性能实验，也不把静态契约写成产品通过。

## 输入与只读门禁

- `git rev-parse HEAD`：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，退出码 0。
- `git diff --cached --name-only`：空，退出码 0；未操作 index。
- REVISION-02 结果 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`：SHA-256 `49BFC7846F7D5871270DDCA29FB3EF782C593B52176808290BEDCB74899D8222`，18,650 bytes；复核期间二次读取哈希与修改时间稳定。
- 原 QA-01 结果哈希：`EE29073B0DB383325DEA36536D735A4192CBEE827361C332F2AB85E3A9F861AF`；未修改原回执。
- 固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 到资料 HEAD 的三文件 `git diff --quiet` 退出码 0；资料 HEAD 到工作树同三文件的 `git diff --quiet` 退出码 0。
- `git diff --check` 退出码 0（仅报告共享既有文档的行尾转换提示）；本轮没有覆盖、暂存或清理共享改动。
- 未运行 CMake、CTest、parser、传输、故障注入、性能、SSH 或云端实验。

## QA-01 条款逐项复审

| 条款 | 结论 | 独立依据与剩余边界 |
|---|---|---|
| 1. upload/download 文件级命令时序 | PASS（规格） | REVISION-02 §2.1、§3 明确候选阶段禁止下载 `SIZE/MDTM/EPSV/REST/RETR`，上传禁止 `EPSV/REST/STOR`；handoff 后分别回到现有 `processDownloadFile`/上传顺序。需要实现后的静态/运行态断言，但设计已无矛盾。 |
| 2. 外部 telemetry/schema 兼容 | PASS（规格） | §2.2、§6、§8 删除 lookahead summary、JSON key、JSONL event、schema version、`control_lease_id` 和新 reason；只保留内部计数，真实事件使用既有 `control_id`，旧输出字段集合和旧消费者保持不变。 |
| 3. 状态、owner/generation/handoff/cancel race | PASS（规格） | §2.3 明确 `metadata_reserved`、`control_pending`、`control_ready`、`in_use`、`cancelled`、`failed` 的转换；同一 mutex/CAS 处理 reservation、handoff、cancel/stale，列出 duplicate、double-use、owner/generation/fingerprint mismatch 向量。 |
| 4. 资源硬门 | PASS（规格） | §2.4–2.5 固定 depth≤1、每 worker pending≤1、全局≤2、extra FD≤2、data FD=0、candidate memory≤2 MiB；`getrlimit(RLIMIT_NOFILE)` 失败/不可信直接 depth=0；CPU/RSS 明确仅观测项。 |
| 5. 白名单与禁止范围 | PASS（规格） | §5 给出 options、lookahead 模块、tree scheduler 的允许文件和测试范围；明确禁止 file data client、wire、manifest/checkpoint/resume/checksum、scheduler/compression/IO backend、服务端、旧消费者及所有未来 data socket/第二 worker control。 |
| 6. 验收矩阵与兼容断言 | PASS（规格） | §2.5、§7 覆盖 control_reuse=off 的 fp1/fp8、多 worker、服务端 cap、拒绝/取消/断连、首末 idle、fresh/empty/skip/no-range/resume_partial/changed/manifest flush failure，并要求 hash、manifest、attempt、wire/frame、旧 JSON 字段等价。全部动态结果仍 NOT_RUN。 |

## 关键契约核对

### 文件命令和协议边界

修订后的候选只保存 manifest 元数据和指纹；唯一可提前执行的是（仅 `control_reuse=off` 且预算通过时）普通 control 建连、认证和登录。下载的远端 `SIZE/MDTM` 明确延后到 handoff 后，随后按 `controlForFile`、Completed gate、SIZE/MDTM、EPSV/REST/RETR、data client、226、mtime/final commit、manifest 的原顺序执行。上传 handoff 前重做本地 stat，随后才 EPSV/REST/STOR。设计没有允许未来 data socket、transfer ID、SessionInit/ResumeResponse、range 或文件副作用提前发生。

### 输出和旧消费者

修订删除了原设计的 lookahead summary 示例、`prepare_elapsed_ns`、`pending_peak` 和 lookahead reason。候选命中/未命中等只存在内存对象，不能写 summary/JSONL；真实文件事件继续使用既有 `control_id`。这闭合了 QA-01 关于新 schema/key/event 的阻塞。是否实际保持旧字节输出，仍需实现后的 on/off golden 和 mixed-consumer 运行门，不能由本复审宣称已验证。

### 状态和资源

`metadata_reserved` 不持有 control ID/FD；`control_pending` 可有未 ready 的普通 control socket 但不得交付；`control_ready` 必须有唯一既有 `control_id` 且 handoff_count=0；`in_use` 只能一次性交付。重复 reservation、二次 handoff、owner/generation/指纹不符、cancel 与 handoff 竞争均有明确 fallback/终止动作。`control_reuse=worker` 保持 metadata-only，不能创建第二 control；`control_reuse=off` 才能使用受 cap 约束的额外 control。

资源条款还规定 close/wait、`getrlimit` 失败回退和超限不进入 pending。CPU/RSS 不再冒充硬门，符合 QA-01 要求；实际 allocator 计量、FD 峰值及服务拒绝仍必须由实现测试证明。

## 动态状态与不可外推项

以下均为 **NOT_RUN**：代码静态 diff、纯函数/状态单测、真实 parser/旧消费者、logger/控制拒绝、fresh/no-range/resume/changed、hash/manifest/wire/frame、on/off、FD/memory、CPU/RSS、10-pair overhead、WAN/GridFTP/跨域实验。此前 dense 12-case WSL loopback 证据仍只是候选假设，不能推出 100 Mbps 或性能收益。

## 允许的下一步与停止条件

允许 00 派发一个“仅写实现计划”的窄任务给 03。该计划必须逐文件列出白名单、纯函数和故障向量，并在实现前再次核对：任何未知 telemetry key/event/reason、文件级命令提前发送、未来 data FD、第二 worker control、wire/manifest/resume/scheduler/compression/IO 改动，或 `getrlimit`/close/wait 失败，都立即 depth=0 回退并停止扩展。当前不允许 03 写代码或创建 worktree；实现仍需新的 QA 和动态门禁。

## 执行回执

- 实际输入/输出 commit：输入资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本轮新增本结果和 CLI 摘要，未提交。
- 实际改动：仅本任务允许的两个文档文件；未改源码、原设计、QA-01、BOARD、ROSTER、决策或 index。
- 实际命令、退出码与证据：`git rev-parse HEAD` 0；`git diff --cached --name-only` 空；输入 hash/稳定性如上；两组三源码 parity 0；`git diff --check` 0；结果文件 UTF-8/LF/无尾随空白检查通过。
- 结论依据：REVISION-02 §2.1–§2.5、§3–§8 对 QA-01 Critical/Important 条款的逐项修订；动态证据全部 NOT_RUN。
- 下一角色：00 可创建 03 的窄实现计划任务；计划完成后再由 04 复审，不能把本回执当作代码或产品验收。

验收人：00 总指挥（待验收）。
