# LOWLEVEL-TREE-TELEMETRY-QA-06 最终独立质量复审

路线/任务：`R2026-09-24.9 / v1`
固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`
复审日期：2026-09-24

## 结论

**BLOCKED；不允许创建 telemetry implementation worktree。** PLAN-06 已补齐大部分 QA-05 要求：封闭 schema/null/reason 矩阵、六组 raw JSONL fixture、四类 logger 注入、upload/download 三 case 各 10 对 on/off 及明确 overhead 门。结构性 fixture lint 也确认 6 个 block、44 行、26/18 键集和 JSON 语法成立。

但仍有两个可复现的规格阻塞：

1. PLAN-06 §1.1 接受所有 uint 字段 `0..UINT64_MAX`，其中包含 `span_id=0`；ARCH-REVIEW-01 §严格 JSON 字段契约规定 `span_id` 范围为 `1..UINT64_MAX`。这是 schema/ID 范围冲突，不能靠文档继承消解。
2. PLAN-06 的 `R-SAME-CONTROL` fixture（§2，行 173/177）把 upload `data_connect` 的 `stream_id` 写成 `null`，但同一 PLAN-06 §1.3 要求 upload `data_connect` 使用真实 stream ID，固定 `file_transfer_client.cpp:390-413` 的 `sendStream` 也已有真实 `streamId` 参数。语义 fixture lint 独立复现该矛盾。

因此不能把其余规格 PASS 误写为实现准入 PASS。所有 parser、logger、真实 on/off/hash/resume、wire、overhead、构建和传输仍为 `NOT_RUN`。

## 输入稳定性与版本

审查开始与收口的 SHA-256、修改时间和大小一致，未触发输入变化停止条件。

| 输入 | 修改时间（+08:00） | 字节数 | SHA-256 |
| --- | --- | ---: | --- |
| `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-06-result.md` | `2026-09-24T01:56:02.9084320+08:00` | 49991 | `8FE5440B33B777FB395C99101C9BD804FC036AF6B80435F0EC12D4DE65083BC2` |
| `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-05-result.md` | `2026-09-24T01:27:05.3818370+08:00` | 11408 | `08B4EBFE0EBE8B87BB5D43A66EE0FB51B0F5FF8DCE4DE97CF483FABB6E429F81` |
| `docs/tasks/2026-09-24-lowlevel-tree-telemetry-plan-05-result.md` | `2026-09-24T01:18:22.7217464+08:00` | 24029 | `09356E014CAB4CB9DF097C3728AE055FAA929EEBADF08F104B09CF4D7AAFED0A` |
| `docs/tasks/2026-09-24-lowlevel-tree-telemetry-arbitration-03-result.md` | `2026-09-24T01:09:31.4472575+08:00` | 22836 | `75A3FE5DFBF82D2D3237FA0205F23B99470235D361A82FC84CCD4C2313BCC9FC` |
| `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md` | `2026-09-24T01:11:09.2656750+08:00` | 9764 | `C55991CD0781B24B24E6B05C295329B41B5DFF23F6149868366FF8A8596F928C` |
| `src/core/io/tree_transfer_client.cpp` | `2026-09-17T00:51:37.5710925+08:00` | 113300 | `E46447F442F237B0346A1E1955FA1F3C9DBA0EB94FF1DCFE501ABF08F197BFAA` |
| `src/core/io/file_transfer_client.cpp` | `2026-09-16T13:26:04.4242920+08:00` | 28612 | `AD9E03B51368BD66284059AF7291962E8314590EA6B690042044A96AE1982939` |
| `src/core/io/file_download_client.cpp` | `2026-09-16T13:26:04.4232992+08:00` | 40042 | `A5ECDE2D4429288DB305C205CDBCE5EAD53D630FBE6FD0CD63B08601D4E6C643` |

PLAN-06、PLAN-05、ARBITRATION-03 和 decision 均为 `R2026-09-24.9`；QA-05 是同路线的上一轮质量输入。固定三源码相对 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 无路径差异。

## 分项判定

| 审查项 | 结论 | 依据、缺口与动态状态 |
| --- | --- | --- |
| `tree_stage_v1` 26 键与 `stream_identity` 18 键 | **PARTIAL / BLOCKED** | 两个列表逐项计数分别为 26 和 18，与 ARCH-REVIEW-01 的字段集合一致；不能为凑 19 给 identity 增加 span/time 字段。可是 `span_id` 的 `0..UINT64_MAX` 文字与架构的 `1..UINT64_MAX` 冲突。最小修订：仅将 `span_id` 下界改为 1，并加入 `span_id=0` 的拒绝向量；其余允许为 0 的 uint 字段按 scope 矩阵保留。 |
| scope/null/reason/状态矩阵 | **PASS（规格，受 ID blocker 牵连）** | PLAN-06 §1.2–§1.4 已封闭 scope/stage/state，列出 required/null 字段、reason、attempt-0 两种形状和 identity 绑定规则；未知组合拒绝。修正 span 下界后才可作为稳定 parser 输入；当前没有运行 parser。 |
| 两类 paired N/A | **PASS（规格）/ NOT_RUN（实现）** | 只允许 `first_payload` 的 `no_missing_range|empty_file` 与 `interfile_idle` 的 `no_next_file|cancelled`；start/terminal、复制 start_ns、null end/elapsed 和普通 N/A 单行规则闭合。D-LATE、EMPTY、IDLE fixture 覆盖正例；重复/无 start/越界 reason 仅列反例，未运行 validator。 |
| download late-ID 序列 | **PASS（规格）/ NOT_RUN（实现）** | D-LATE-NORANGE 六行依次给出 data_connect、null-ID first_payload start、identity bound、一次性 null→7 paired terminal 和 payload_io N/A；与 `file_download_client.cpp:378-425` 在 SessionInit 后得到 stream ID 的顺序相符。没有运行真实 download 或 identity failure 注入。 |
| retry same control / 不同 attempt | **PASS（规则）/ BLOCKED（fixture）** | 文本和 R-SAME 将 attempt 1/2、span/slot 分离并保持 control_id=5，固定 `tree_transfer_client.cpp:1414-1447` 与 `1151-1179` 支持 same-control。可是 R-SAME 行 173/177 的 upload `data_connect.stream_id=null` 违反 §1.3 的 upload 真实 ID要求；应改为源码实际 stream ID并重新 lint，不能把该 fixture 记为有效。 |
| interfile idle 四类 fixture | **PASS（规格）/ NOT_RUN（实现）** | IDLE 覆盖 continuous completed、tail paired N/A、cancel paired N/A、handoff failed；前一文件失败不启动 idle。global/普通 worker 插点与固定源码循环相容；尚未动态验证取消、capacity wait 或并发重叠。 |
| 六组 golden 的结构与键集 | **PASS（结构 lint）/ BLOCKED（语义 lint）** | 只读脚本解析 6 个 `jsonl` block、44 行，JSON 语法和每行 26/18 键通过（退出码 0）。加入 upload data_connect 非空 stream ID 规则后，语义 lint 退出码 1，唯一违规为 block 5 span 121 的 `stream_id=null`。修正该行并重跑完整 lint 后才能闭合。 |
| logger 四类故障注入 | **PASS（规格）/ NOT_RUN（实现）** | `start_write`、`terminal_write`、`append_poison`、`summary_write`（含 stderr 子变体）均给出注入点、errno、prefix bytes、退出码和五轴不变断言；注入不进入 overhead 样本。尚未创建 seam、运行故障注入或回读证据。 |
| on/off、hash/resume、wire 等价性 | **PASS（计划门）/ NOT_RUN（实验）** | fresh/no_range/resume_partial × upload/download，每 cell 10 对、共 60 paired blocks/120 runs；固定 seed、输入、manifest、退出码、双端 hash、frame、resume 和 wire 等价排除规则已写明。loopback 结果只能证明 telemetry overhead，不能外推跨域吞吐。 |
| overhead 门 | **PASS（门限已写）/ NOT_RUN（实验）** | wall median≤5%、p95≤10%；fresh/resume 的 CPU/GiB median≤5%、p95≤10%，JSONL bytes/GiB median≤4 MiB/GiB、p95≤8 MiB/GiB；每 cell 至少 8/10 valid pairs，缺失或 mismatch 为 BLOCKED/FAIL。尚未运行任何 pair，不能宣称门通过。 |
| 实现白名单与授权边界 | **PASS（文档）** | 仅允许三份 tree/file C++、telemetry API、专属 parser/fixture/logger 测试和明确 CMake 注册；禁止改协议、manifest/resume/checksum/default/runner。由于上述 schema/fixture blocker，当前不授权创建 worktree。 |

## 最小修订与后续门禁

1. 修正 PLAN-06 的 `span_id` 下界为 `1..UINT64_MAX`，增加 `span_id=0` 的 raw-token 拒绝向量，并确认所有引用一致。
2. 修正 R-SAME-CONTROL upload `data_connect` 行的 `stream_id` 为固定源码实际值，或给出新的架构裁定；重新执行六 block 结构与语义 lint。
3. 04 对稳定的新哈希重新复审后，才能派发 implementation task。实现阶段仍必须依次运行 parser/golden、paired lifecycle、四类 logger 注入、mixed old/new consumer、fresh/no-range/resume tree smoke，以及 60 paired on/off blocks 和 overhead gate。

## 执行回执

- 实际输入/输出 commit：HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；固定源码输入 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；只新增本结果文件，未提交。
- 实际改动：仅新增 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-06-result.md`；未修改 PLAN-06、PLAN-05、QA-05、ARBITRATION-03、decision、源码、测试、runner、BOARD/ROSTER 或 Git index。
- 实际命令与退出码：`git rev-parse HEAD`（0）；`git status --short --branch`（0）；`git diff --cached --name-only`（0，无输出）；固定三源码 `git diff --quiet 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp`（0）；输入前后 SHA-256/修改时间核对（0，未变化）；fixture 结构 lint（0，6 blocks/44 lines/26+18 keys）；fixture 语义 lint（1，R-SAME upload `data_connect` null stream ID）。
- transfer / integrity / evidence / wire accounting：只审查 PLAN-06 的分轴、等价性和 eligibility 规则；没有运行态结果或 wire 证据。
- 失败/跳过/阻塞：span_id 下界冲突和 R-SAME fixture 语义冲突；parser、logger、CMake/CTest、真实传输、hash/resume、on/off、overhead、SSH、云端和清理全部 `NOT_RUN`。
- 下一步：00/01/03 修正上述两个最小问题并交付稳定 PLAN-06；04 再复审。QA-06 PASS 前不得创建 telemetry implementation worktree。

## 写后门禁

- 结果文件必须严格 UTF-8、无 BOM、纯 LF、无尾随空白并以 LF 结束。
- `git diff --check` 必须退出码 0；共享工作树既有 LF→CRLF 提示不作为本文件错误。
- HEAD、暂存区和固定三源码路径差异保持不变。
- 本轮未运行实现、构建、CTest、parser/validator、logger 注入、真实 on/off/hash/resume/frame、overhead、SSH 或云端实验。
