# LOWLEVEL-TREE-TELEMETRY-QA-05 独立质量复审

路线/任务：`R2026-09-24.9 / v1`
固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`
复审日期：2026-09-24

## 结论

**BLOCKED；不允许创建 telemetry implementation worktree。** PLAN-05 已正确吸收 ARBITRATION-03 的两个 paired N/A、download late `stream_id`、same-control retry 和 interfile idle 终态方向，固定源码的普通调用顺序也与这些方向相容。但严格 schema 仍缺可机械执行的完整 scope/null 与枚举约束；golden bytes 没有覆盖全部允许正例及 paired 序列；logger 故障注入没有具体故障点/期望断言；on/off、hash/resume 和 overhead 没有固定样本、配对、计时分母和 pass/fail 门限。按任务停止条件，不能把静态规格写成 PASS，也不能放行实现。

本报告只审查文档和固定源码映射。parser、logger、真实 tree upload/download、resume/hash/frame、wire、性能和 overhead 均为 `NOT_RUN`；下表的 PASS 只表示规格或静态调用顺序通过，不表示实现或产品验收通过。

## 输入冻结核验

审查开始和收口各记录一次输入文件元数据；两次相同，期间未发现输入变化。

| 输入 | 修改时间（+08:00） | 字节数 | SHA-256 |
| --- | --- | ---: | --- |
| `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-05-result.md` | `2026-09-24T01:18:22.7217464+08:00` | 24029 | `09356E014CAB4CB9DF097C3728AE055FAA929EEBADF08F104B09CF4D7AAFED0A` |
| `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md` | `2026-09-24T01:09:31.4472575+08:00` | 22836 | `75A3FE5DFBF82D2D3237FA0205F23B99470235D361A82FC84CCD4C2313BCC9FC` |
| `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md` | `2026-09-24T01:11:09.2656750+08:00` | 9764 | `C55991CD0781B24B24E6B05C295329B41B5DFF23F6149868366FF8A8596F928C` |
| `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-04-result.md` | `2026-09-24T00:54:37.5233668+08:00` | 12174 | `E54629F0932FFF98A2D1B47614BA958AA4BC6AE6460DEB03E15D486B11FC10E6` |
| `src/core/io/tree_transfer_client.cpp` | `2026-09-17T00:51:37.5710925+08:00` | 113300 | `E46447F442F237B0346A1E1955FA1F3C9DBA0EB94FF1DCFE501ABF08F197BFAA` |
| `src/core/io/file_transfer_client.cpp` | `2026-09-16T13:26:04.4242920+08:00` | 28612 | `AD9E03B51368BD66284059AF7291962E8314590EA6B690042044A96AE1982939` |
| `src/core/io/file_download_client.cpp` | `2026-09-16T13:26:04.4232992+08:00` | 40042 | `A5ECDE2D4429288DB305C205CDBCE5EAD53D630FBE6FD0CD63B08601D4E6C643` |

PLAN-05、ARBITRATION-03、decision 均为 `R2026-09-24.9`；QA-04 是旧 `.8` 复审记录，PLAN-05 正确把它当历史阻塞证据而不是本版本 PASS。三份源码相对固定输入未见路径差异。

## 分项审查

| 审查项 | 结论 | 依据与最小闭合条件 |
| --- | --- | --- |
| 严格 `tree_stage_v1`/`stream_identity` 键集与原始数字 token | **PARTIAL** | PLAN-05 §“严格事件形状”给出 26/19 个键、uint32/uint64 原始 token、拒绝负数/浮点/指数/字符串/越界和 UUID/方向约束；这是可实现方向。可是 `scope` 对每个字段的必填/null 矩阵没有完整表，`stage` 写成“至少包括”而非 schema v1 的封闭枚举，`attempt_kind`、`state`、`reason` 的全量允许值和各事件组合也未冻结。应补逐 scope/stage 的 required/null/enum 矩阵及未知值处理，然后才可机械验收。 |
| 两个 paired N/A 例外 | **PASS（规格）/ NOT_RUN（实现）** | §“通用状态和配对”正确限制为 `first_payload` 的 `no_missing_range|empty_file` 与 `interfile_idle` 的 `no_next_file|cancelled`；start 先写，terminal 复制 `start_ns`、end/elapsed 为 null，其他 N/A 仍为单行全 null。ARBITRATION-03 §3.1/§3.3 与此一致。尚无 parser/故障向量验证唯一 start、重复 terminal、无 start、reason 越界和 null→0 拒绝。 |
| download late `stream_id` | **PARTIAL** | PLAN-05 只允许 download `first_payload` paired terminal 从 null 一次性绑定真实 ID，另有 SessionInit 后的 `stream_identity`，且其他 stage 不回填；固定源码 `file_download_client.cpp:378-425` 在连接后才于 `readSessionInit` 得到 stream ID，映射方向正确。完整 golden 只有 late terminal，没有包含 null-ID start→identity→terminal 的完整逐字节序列，也未给 identity 失败/重复 slot 的可执行正例。应补完整序列和两次改 ID、无 SessionInit bound、重复 stream/slot 的拒绝断言。 |
| same-control retry | **PASS（静态映射）/ NOT_RUN（行为）** | PLAN-05 §retry 与 ARBITRATION-03 §二/§3.2 明确新 attempt/span/slot 沿用同一 `control_id`，只有真实 teardown/reconnect 才新建 control；固定源码 `tree_transfer_client.cpp:1414-1447` 在原 `control.value()` 上执行 wait/EPSV/REST/STOR，`1151-1179` 支持 worker reuse。仍需实现测试确认 attempt 1/2 的 span、slot、wire、process 结果隔离，禁止把 retry control_prepare 写成 control_acquire。 |
| interfile idle 四类 fixture | **PASS（规格）/ NOT_RUN（行为）** | PLAN-05 §“interfile idle 三类 fixture”实际覆盖 continuous completed、worker tail paired N/A、cancel paired N/A、handoff failed，另列前一文件失败不启动 idle；`tree_transfer_client.cpp:2231-2279` 的 global dispatch/capacity 与 `2341-2359` 的普通 worker 循环支持该边界。实现仍需确认 start 放在前一文件真实 terminal 后、tail/stop/error 都有 append-only terminal，且不把 idle sum 当 run wall。 |
| 四/五状态轴、wire 与 `performance_eligible` | **PASS（规格）/ NOT_RUN（证据）** | PLAN-05 §“四/五状态轴”独立列出 transfer/process/integrity/evidence/wire 和派生 eligibility；retry、no-range、empty、首块前失败和 idle 不进 payload 资格，wire 不补零。没有运行态结果，不能报告任何轴已通过。 |
| finalize/226/mtime/manifest 静态顺序 | **PASS（静态映射）/ NOT_RUN（行为）** | PLAN-05 §固定阶段边界与源码 `tree_transfer_client.cpp:1592-1624`、`file_download_client.cpp:665-758` 保持 file client return→226→mtime→`updateRecord`；`updateRecord` 与 `updateRecordForTransfer` 在 `1068-1092` 分开。失败、重试和 logger 失败路径尚未动态验证。 |
| golden JSONL bytes | **BLOCKED** | PLAN-05 §“Parser golden bytes”提供 upload start、download late terminal、stream_identity 三条完整行和若干拒绝变体；没有完整 download null-ID start→identity→paired terminal、payload_io N/A、idle tail/cancel/handoff failed、same-control retry、attempt-0 两例外的有效 raw bytes。ARBITRATION-03 的示例还明确省略其余键，不能直接作为逐字节 fixture。最小修订是冻结每一允许正例的完整 UTF-8 JSONL（含行尾、键顺序、配对序列）和对应 expected validator outcome。 |
| logger failure isolation / 故障注入 | **PARTIAL / NOT_RUN** | PLAN-05 §四/五状态轴规定 logger poison、start/terminal 写失败只改 evidence，summary/stderr 独立暴露 gap；方向足够。但没有规定可重复的注入点（start、terminal、summary、O_APPEND/poison）、注入方式、退出码/文件保留和每一状态轴的 expected assertion。应先冻结故障向量，再运行注入测试；在此之前不得把隔离写成已验证。 |
| 真实 on/off、hash/resume/frame 与 overhead 门 | **BLOCKED / NOT_RUN** | PLAN-05 只要求后续比较退出码、双端 file/tree hash、manifest/resume、计数、DATA logical content 和 wire；没有固定 case、配对顺序、重复数、timer/wall 分母、异常/不匹配排除规则或 overhead median/p95 门限。应由新实现/测试任务给出可执行白名单、paired block、hash/manifest/resume 必须相等、失败分类和明确 overhead 门限；本轮不运行。 |
| 版本、源码边界与实现白名单 | **PASS（文档）** | PLAN-05 路线与输入 commit 一致，白名单收窄到三份 tree/file 源码、telemetry API、专属 parser 单测和明确 CMake 注册，并禁止协议/manifest/resume/checksum/default/runner 扩围。该 PASS 只证明边界文档存在，不授权 worktree。 |

## 实现前必须保留的门禁

只有新任务补齐并执行下列门禁后，才能重新申请 implementation worktree：

1. parser：完整 26/19 键集、封闭 enum/null 矩阵、uint64 原始 token 和上述每一正反 golden bytes；未知键、重复/孤立配对、late-ID 二次改写、attempt-0 越界必须硬拒绝并只影响 evidence。
2. logger：对 start append、terminal append、poison/O_APPEND 失败、summary/process 独立输出分别注入；验证 transfer/process/integrity/wire/frame/hash 不改变，evidence 变为 partial/gap，且保留可读的退出和错误证据。
3. 真实链路：telemetry on/off 交错 paired block，upload/download、空文件、全 no-range、retry、resume/checksum/TLS smoke；比较退出码、双端 file/tree hash、manifest、resume 状态、文件/字节计数和 DATA logical frame，失败、evidence partial、wire unknown 不纳入性能资格。
4. overhead：任务单冻结重复数、同源输入、计时分母、阶段重叠处理和 median/p95 门限；不得把阶段和或 telemetry event 数当 wall time。没有这些数字，overhead gate 不可执行。

## 执行回执

- 实际输入/输出 commit：HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；固定源码输入为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；只新增本结果文件，未提交。
- 实际改动：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-05-result.md`；未修改 PLAN-05、ARBITRATION-03、decision、QA-04、源码、测试、BOARD/ROSTER、worktree、云端或 Git index。
- 实际命令：`git rev-parse HEAD`（0）；`git status --short --branch`（0，保留共享文档改动）；`git diff --cached --name-only`（0，无输出）；固定三源码 `git diff --quiet 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp`（0）；输入定向 `Get-Content`/`rg` 与前后 SHA-256/时间核对（0，未变化）。写后 UTF-8/LF/尾随空白和 `git diff --check` 在收口时记录。
- transfer / integrity / evidence / wire accounting：只复审 PLAN-05 的独立状态轴和 eligibility 规则；没有运行态状态或 wire 证据。
- 失败/跳过/阻塞：严格 schema/null 矩阵、完整 golden bytes、logger 故障注入契约和 overhead 门限不足；所有 parser、logger、构建、CTest、真实传输、hash/resume、性能、SSH、云端和清理均 `NOT_RUN`。
- 下一步：00/01 冻结上述最小修订，03 提交稳定版本和专属测试向量；04 对不变输入重新独立复审。QA-05 未 PASS 前不得创建 telemetry implementation worktree。

## 写后门禁

- 结果文件须严格 UTF-8、无 BOM、纯 LF、无尾随空白并以 LF 结束。
- `git diff --check` 必须退出码 0；共享工作树既有 LF→CRLF 提示不作为本文件空白错误。
- HEAD、暂存区和三份固定源码路径差异必须保持不变。
- 本轮未运行 CMake/CTest、parser/validator、logger 故障注入、真实 on/off/hash/resume/frame、overhead、SSH 或云端实验。
