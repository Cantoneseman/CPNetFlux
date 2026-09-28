# LOWLEVEL-TREE-TELEMETRY-QA-04 独立质量复审

路线/任务：`R2026-09-24.8 / v1`
固定源码：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`
复审日期：2026-09-24

## 结论

**BLOCKED；不允许派发 telemetry implementation。** 核心分项审查完成时的 PLAN-04 快照为 00:35:15 +08:00、SHA-256 `6699CF8C38A7B4D80B64D7C03D553908853C5921E267ECA2DAC4ADB454D3F2AB`，其中 §10.1 正确识别了 `first_payload` 起点先于 ResumeResponse、而 no-range 又要求单行 N/A 的 append-only 矛盾；另有 retry control 生命周期与固定源码不匹配、interfile idle 尾部不记/前一文件结束即 start 的时序不闭合。报告写入前，PLAN-04 又在 00:43:32 +08:00 更新为 SHA-256 `4FCC510DFB7EF20D4FAC86D26B2A9A5B26CAA51496FA3365DA52959148924246`，状态改为 `BLOCKED`（download 首 payload 的 stream ID 配对尚未冻结）。按任务的输入变更停止条件，本报告的逐项表只适用于前一快照；当前快照须由 04 重新复审，不能据此给实现设计门 PASS。

此处 BLOCKED 是**规格准入**结论。文档中其他可闭合项（严格键集/数字词法、attempt-0 两种行、四轴与 wire 字段、logger failure 隔离、hash/on-off 门禁）仅属静态契约审查；validator、logger、summary、兼容回归、构建/CTest、真实上传下载和 overhead 均未运行，不能升级为实现或产品正确性 PASS。

## 输入核验与变更边界

- 复审开始时 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务资料 HEAD 一致；`git diff --cached --name-only` 无输出，index 为空。共享工作树已有其他文档改动，均予保留。
- 路线、任务和 BOARD 当前均为 `R2026-09-24.8`。BOARD 第 53–57 行显示 ARCH-REVIEW-01/02 已交付、PLAN-03 superseded、PLAN-04 review、QA-04 in_progress，且实现仍待 QA PASS。
- 本轮读取了 TASK_TEMPLATE、任务单、BOARD、telemetry decision、arbitration-02、PLAN-04 结果、QA-02 结果、ARCH-REVIEW-02 结果及三份固定源码。任务要求的 ARCH-REVIEW-02 路径 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-arch-review-02-result.md` 可读；未改动其内容。
- 复审过程中观察到 PLAN-04 结果文件被原地更新两次：初读内容为 253 行、约 30 KB、开头标记“交付规格，待 QA-04”；第一份审查快照为 288 行、39,140 bytes、SHA-256 `6699CF8C38A7B4D80B64D7C03D553908853C5921E267ECA2DAC4ADB454D3F2AB`，开头明确标记 `BLOCKED` 并新增 §10.1 no-range 矛盾；随后文件在 00:43:32 更新为 39,026 bytes、SHA-256 `4FCC510DFB7EF20D4FAC86D26B2A9A5B26CAA51496FA3365DA52959148924246`，改以 download 首 payload stream ID 配对为阻塞。按输入变更门禁，未对第二份快照重做逐项审查。该文档变化不改变 HEAD；没有把其内容归为 03 聊天已执行或源码改动。
- 固定三份源码对比 `3b0820d..HEAD` 的 `git diff --quiet` 退出码 0；相关代码路径未偏离任务基线。
- 本轮仅新增本结果文件；没有修改 PLAN、decision、源码、测试、runner、BOARD/ROSTER 或 index。Codex CLI 本轮可用但未调用，没有 CLI last-message 文件。

## 分项判定

| 审查面 | 结论 | 证据、判断与最小闭合条件 |
| --- | --- | --- |
| Strict event key/type/schema | PARTIAL | PLAN-04 §2 定义 `tree_stage_v1` 恰好 26 键、`stream_identity` 独立 19 键；限定 UUID、方向、scope/stage/state、非空 error code 和 uint32/uint64 类型，并在 §2.2 给 identity 独立形状。总体映射清楚。需由最终规范/实现任务统一“stream_identity.state=bound/failed”与 tree_stage state 枚举的按事件分支校验，并检查 §2.3 全 scope 矩阵覆盖两个事件的交集。当前只是静态规格，无 parser 证据。 |
| Raw JSON token / validator 向量 | PARTIAL（规格）/ NOT_RUN（实现） | §2 拒绝负值、浮点、小数、指数、字符串、越界和 double 舍入；§7 列正反例，包括 0、`UINT64_MAX`、`UINT64_MAX+1`、配对重复/孤立、未知键和 identity 冲突。§7.1 声称 run start “完整 26 键”，本身是 `tree_stage_v1` 事件，未见把 19 键 identity 正反例逐个写成实际字节 fixture；“列向量”尚非 executable test。最小闭合：实现任务附两个完整事件的 golden JSON bytes/token parser fixture，验证错误路径。 |
| 固定 finalize/226/mtime/manifest 顺序 | PASS（规格映射）/ NOT_RUN（实现） | PLAN-04 §3.1–3.3 将 upload file function 返回与 226 wait 相接，将 download file function 内 flush/verify/rename/commit、随后 226、再 mtime、再 terminal `updateRecord` 分开；固定源码 `tree_transfer_client.cpp:1592–1624` 与 `file_download_client.cpp:665–758` 支持其普通成功顺序。`manifest_finalize` 排除 `updateRecordForTransfer`，覆盖 Completed Changed 仅在真实 `updateRecord` 调用时记录。需补失败/重试路径动态插桩测试；不影响当前映射的静态 PASS。 |
| attempt-0 两种例外 | PASS（规格）/ NOT_RUN（行为） | arbitration-02 与 PLAN-04 §2.3、§4.1–4.2 对入口 Completed decision (`file_attempt_decision`, `attempt_id=0`, `state=skipped`) 与实际 Changed `updateRecord` 的 attempt-0 `manifest_finalize` 给出窄而不同的向量；只允许真实 Changed 保存时输出后者。固定源码 upload `tree_transfer_client.cpp:1346–1358`、download `:1498–1521` 在 Changed 分支调用 `markChanged`，`updateRecord` 定义于 `:1068–1078`。control gate 失败未保存不能伪造 manifest span。 |
| no-range、空文件与 append-only start | **BLOCKED** | PLAN-04 §10.1（最终快照行 245–256）说明固定定义的 `first_payload` 从连接完成开始；上传/下载要到 SessionInit/ResumeResponse 后才能知道该 stream 无 missing range；但 §2.3/§4.2 要 no-range 的 `first_payload`、`payload_io` 为 null 时间单行 `not_applicable`，而已开始的 stage 必须先 append `in_progress` start 且不得回写。同一个 span 不能既有 start 又变为 N/A，等握手后才记又丢真实起点。空文件可在已知长度时 N/A，不会消除此 no-range 矛盾。最小修订由 00/01 冻结一种可观察定义：允许 `in_progress`→N/A terminal 并精确定义终态时间字段；或将 no-range 的观测范围定义为 ResumeResponse 后并声明此前连接等待 excluded；两方向和首 payload 前失败须一致适用。再由 03 更新 schema/向量、04 复审。 |
| Retry control ID / attempt 语义 | BLOCKED（需修正文案） | PLAN-04 §4.2 upload retry 行要求新 `attempt_id`、新 control/stream/span；但固定源码 `tree_transfer_client.cpp:1414–1447` 在原 `control.value()` 上等第一次 transfer 完成，再复用同一 control 做 EPSV/REST/STOR retry；`controlForFile`/worker runtime 复用在 `:1151–1179`。新 attempt 必须区分 span/stream attempt，不能声称创建了新 control connection。最小修订：retry 沿用同一个 `control_id`（除非真实连接重建），仅新建 attempt/span 与 stream slot；明确 retry `transfer_status/process_status`、等待失败和 wire denominator。 |
| Interfile idle 生命周期 | PARTIAL | PLAN-04 §2.3 定义 idle start 在前一 attempt terminal 后、只有领取后继文件才于 terminal 填 `next_*`，§7.1 也规定该形状；但 worker 在末文件结束后才知道无后继，start 已先写而规格说“worker 尾部没有后继文件不造 idle”（§2.3）。这与 append-only 及 start-before-operation 无法同时满足，虽然不在 §10.1 停止清单内。最小修订：将候选 idle 记为可终止为 `not_applicable/no_next_file` 的 start/terminal，或把其观测起点改为已领取后继后的可测等待定义；给正常连续文件、worker 尾部、取消/错误三个 fixture。 |
| 四/五状态轴、wire 与 eligibility | PASS（规格）/ NOT_RUN（证据） | PLAN-04 §4.2、§5 分开 `transfer_status`、原始/`process_status`、`integrity_status`、`evidence_status`、`wire_accounting_status` 和派生 `performance_eligible`。全 hash 缺失为 unknown；Changed metadata 不判内容 fail；no-range logical payload 可为 0，但握手/FIN/226 wire 可非零，范围外为 unknown/N/A；不适用和缺证据不造 wire 0。逐生命周期表覆盖正常、Completed gate、Changed、gate failure、all/partial no-range、空、连接/首块失败、retry、preflight、manifest failure、idle。动态真实性未测。 |
| Logger/write failure 隔离 | PASS（规格）/ NOT_RUN（实现） | §6 定义单 run sink、mutex 单次 `O_APPEND`、poison 后停止追加、独立计数与 summary/process JSON/stderr best-effort，并要求只改变 evidence/证据计数，不改 transfer/process/integrity/wire/frame。覆盖 logger start/terminal failure 的门禁明确；还需实际故障注入确认 summary 自身也失败时 evidence gap 可交付。 |
| 旧 consumer、on/off 与 overhead | PASS（验收设计）/ NOT_RUN（测试） | §7.3 规定新行 error_code 与旧行混合、坏尾先新 validator 隔离；§8 要固定输入真实 tree upload/download on/off，核退出码、双端 file/tree hash、manifest/resume、计数、DATA frame 逻辑内容；随机交错 30 样本、CI、wall median ≤5%、p95 ≤10% 是计划门，不是已测结果。代码 `run_alpha_demo.py:267–286` 的逐行 error_code 行为与此相容性要求相符，但未跑回归。 |
| QA-02 索引、路线/授权与 coverage | PARTIAL / BLOCKED | §9 映射 QA-02 #1–#10 至 PLAN 章节及实现门；§10/BOARD 都写 QA-04 PASS 前不实现。最终 PLAN 快照 §10.1 自己阻止 PASS，故路线授权一致但准入未满足。静态覆盖表广，但正文把 parser fixture 列表称“向量”而非附完整可执行 bytes；另有 idle/retry 控制语义缺口。最小修订见上两项与 no-range blocker，再以一份稳定新版本复审。 |

## 结果轴和未运行项

规格级静态通过不等于动态验收：本轮未执行 CMake/build、CTest、Python tests、validator/parser、logger 故障注入、run_alpha_demo 混合日志测试、tree upload/download、resume/hash/frame 对照、overhead profile、SSH 或云端实验，全部 `NOT_RUN`。没有可报告的运行态 `transfer_status`、`integrity_status`、`evidence_status`、`wire_accounting_status` 或产品性能资格。

## 执行回执

- 输入：固定源码 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；已逐项审查快照 SHA-256 `6699CF8C38A7B4D80B64D7C03D553908853C5921E267ECA2DAC4ADB454D3F2AB`、状态 BLOCKED；收口时当前 PLAN-04 SHA-256 为 `4FCC510DFB7EF20D4FAC86D26B2A9A5B26CAA51496FA3365DA52959148924246`、状态仍为 BLOCKED，触发输入变更停止。任务单指定的 ARCH-REVIEW-02 路径可读。
- 改动：只新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-04-result.md`。last-message 文件未创建（CLI 未调用）。
- 命令：`git rev-parse HEAD`（0）；`git status --short --branch`（0，保留共享文档改动）；`git diff --cached --name-only`（0，无输出）；固定源码 `git diff --quiet 3b0820d..HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp`（0）；输入文档定向 `Get-Content`/`rg`（0）；写后严格 UTF-8/纯 LF/尾随空白/末尾换行、`git diff --check` 与 HEAD/index 核验结果已在本次收口补录。
- 失败/阻塞：已审查快照存在 no-range 起点与 N/A 生命周期冲突、retry 要求新 control 与固定源码复用冲突、interfile idle 尾部规则不符合 append-only 时序；收口时 PLAN-04 又改以 download 首 payload stream ID 配对未冻结为阻塞，未对该新快照复审。无修复或代码执行。
- 下一步：00/01 先冻结当前快照的 download `first_payload` stream ID 配对及既有 no-range/idle 语义；03 更新稳定 PLAN-04 后，04 重新进行完整独立复审。只有新版本全项 PASS 后才可创建 implementation worktree。
