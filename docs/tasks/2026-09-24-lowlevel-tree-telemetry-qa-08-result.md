# LOWLEVEL-TREE-TELEMETRY-QA-08 结果

日期：2026-09-24（Asia/Shanghai）
路线/任务：R2026-09-24.9 / v1
角色：04 测试与质量；验收：00 总指挥

## 独立结论

**结论：PARTIAL。**

候选实现和 Linux 证据足以允许在匹配的 Linux/POSIX 环境中继续做**受限的 fresh 目录阶段观测 profile**：只观察已实现的阶段和关联字段，不把结果当作性能收益、wire 等价、跨域结果或 100 Mbps 达标。它还不足以放行一般化目录 profile、performance eligibility、实现合入或产品验收。

以下仍是进入较宽 profile 或任何性能结论的硬门禁：

- no-range、resume_partial 的 telemetry on/off 等价尚未运行；
- 正式 fresh/resume 10-pair overhead gate 尚未运行；
- 传输失败/重试/空文件/no-range 的真实 telemetry 行序和故障隔离尚未由本证据覆盖；
- manifest canonical、DATA frame/wire accounting 和 mixed old/new consumer 没有对应的运行态证据。

本轮没有执行构建、CTest、传输、故障注入或性能实验；Linux 结果是 00 生成的固定 bundle，本轮只做哈希、日志和源码的独立复核。

## 输入、工作树和范围

主仓库：

- git rev-parse HEAD 退出码 0：a076c532640ba06de016ed7ed20f7d2a6d48a0a7。
- git status --short --branch 退出码 0；主仓库保留既有共享文档 dirty 改动。
- git diff --cached --name-only 退出码 0、无输出；未操作 index。
- git diff --check 退出码 0；输出的 LF/CRLF 提示属于既有共享跟踪文档。

候选 worktree C:\Users\12563\AppData\Local\Temp\cpnetflux-lowlevel-tree-telemetry-impl-01\worktree：

- git rev-parse HEAD 退出码 0：3b0820dab6dc149f549bd3e81ef403ea7953c4e9，分支为 codex/LOWLEVEL-TREE-TELEMETRY-IMPL-01。
- git status --short --branch 退出码 0；index 为空。跟踪改动是 CMake、两个 file client 头文件、event_log 头/源、三个 IO 源；未跟踪项是 tree_telemetry 头/源、专属单测、6 个 fixture 和实现回执，均在实现任务白名单内。
- 候选回执的 git diff --quiet 3b0820dab6dc149f549bd3e81ef403ea7953c4e9..HEAD 只证明提交 HEAD 等于基线，不能证明未提交工作树无差异；本轮以实际 status 和白名单逐项核对为准。
- 候选 worktree git diff --check 退出码 0（其临时 core.whitespace=cr-at-eol 设置只影响检查解释，不改变文件）。

固定三份源码在主仓库相对资料 HEAD 未被本轮改写；候选的改动集中在任务指定的 tree/file telemetry 插点。

## 静态实现复核

| 项目 | 结论 | 独立依据 |
|---|---|---|
| 严格 schema、键集和整数词法 | PASS（静态/测试证据） | candidate/src/core/metrics/tree_telemetry.cpp:387、508、669-697；26 键 tree_stage_v1、18 键 stream_identity；重复/未知/缺失键、浮点/指数/负数/溢出和 span_id=0 有拒绝逻辑。tests/unit/tree_telemetry_test.cpp:58-121 对应 golden、词法和 canonical key order。 |
| 生命周期、阶段和关联字段 | PASS（实现边界） | tree_telemetry.cpp:765、943、1100-1198、1253；start/terminal 使用同一 recorder 的 monotonic 纳秒，terminal 校验 end>=start 和 elapsed；worker/control/file/attempt/stream/transfer/path 由既有对象填充。 |
| download late stream identity | PASS（实现边界） | file_download_client.cpp:403-431 先以空 stream id 开始 data_connect，SessionInit 后 bind；tree_telemetry.cpp:1134、1178 对 late id 和 first_payload terminal 做配对。实际 on-download 日志有 16 条 identity 行。 |
| empty/no-range/skip/retry/idle 的编码 | PARTIAL | validator 和 fixture 对 first_payload/payload_io paired N/A、attempt-0 skip/manifest finalize、same-control retry、interfile idle 有规则（tree_telemetry.cpp:412-488、529、765；6 fixtures）。本次 fresh Linux run 没有 no-range、resume_partial、首块前失败或真实 retry 行，故只能确认静态契约和普通 fresh 观测。 |
| upload/download payload 阶段插点 | PASS（fresh 范围） | file_transfer_client.cpp:358-359、431-522；file_download_client.cpp:464-495、592-680。DATA/frame/manifest/resume 路径本身未被改写；telemetry 只包围已存在的阶段调用。 |
| 并发计时和重叠 | PARTIAL | recorder 使用 steady/monotonic 纳秒和互斥 append；各 span 独立计时，可重叠，不应相加。tree_parallel smoke 通过，但本轮没有按 worker/stream 重新验证并发阶段的因果边界或 profile 分母。 |
| logger 故障隔离 | PASS（单元层）/PARTIAL（真实传输层） | tree_telemetry.cpp:860、907、943-970 记录 evidence partial/incomplete/evidence_gap；event-log 写失败不改变 transfer 状态。Linux CTest 的 173、174 通过；但没有本轮真实传输中的注入 run，故不能把进程级隔离记为动态全覆盖。 |
| 旧 event-log writer | PASS（源兼容） | event_log.cpp:149-173 保留旧事件字段并使用 O_APPEND helper，追加写处理 partial write/EINTR；旧行不会改成 tree_stage_v1。 |
| 旧消费者和混合日志 | PARTIAL | tools/demo/run_alpha_demo.py:272-286 逐行 json.loads 并读取 error_code，源代码可消费含额外 telemetry 行的 JSONL；off/on 日志中仍分别保留 10/18 条旧行。本轮没有单独运行 demo 的 mixed old/new consumer 断言。 |
| telemetry 对 transfer/integrity/wire 的非干扰 | PARTIAL | 静态 diff 未涉及 wire/frame/handshake/manifest/resume/checksum/scheduler/compression/IO backend，logger 只更新 evidence 状态；但本轮没有 manifest projection、DATA frame 序列或 wire accounting 的 on/off 运行证据。 |

静态代码中值得保留的边界：正常 finish 会先移除 open span 再写 terminal；若 terminal sink 失败，结果是 evidence partial/缺 terminal，而不是伪造 duration 或改变 transfer 状态。该行为符合“证据轴隔离”，但要求 profile 端把缺 terminal 的 run 排除。

## Linux bundle 复核

证据根：D:\Project\CPNetFlux-evidence\LOWLEVEL-TREE-TELEMETRY-IMPL-01-local-20260924\linux-validation

### 哈希和归档边界

- bundle lowlevel-tree-telemetry-linux-evidence.tar.gz 的 Get-FileHash SHA-256 退出码 0，实际为 7757a497593ef2e5fb4dad55b1d81e89377ae73bdcaabefedb6af93968239b70，与任务单一致。
- 在内存中读取 tar 的 results/evidence-sha256.txt 并重算内部 11 项，Python 退出码 0，bad=0。清单包含 source-worktree.tar、configure/build/CTest/summary 和三个 build binary。 清单中的 summary SHA-256 为 d45e2d2ba789ea85f6e711bd980c5c7d24861de40dca149832e770ad7ea88eed；unit/upload/download 二进制分别为 ba87c778862d63c71d95ce26c55317501a75456400f10364eca72126b61a790b、07ecbd9d3af4c9ae787c4f03df7b582d5f59f8a35acd5213d35b3449117009d4、dfdcb494e40a052686c10a1244bc6b026fa7ceddee60af0630917853564bc7a4。
- source-worktree.tar 的内部 SHA-256 为 04fef0a5f848afd1d9c4148e93a9dec415339c448ebdf20724b2d2666dd31d1e，与任务单一致。该比对只证明 bundle 内归档自洽，不能独立证明它仍等于生成归档时的 WSL 原始目录；没有恢复 payload，也没有 SSH。
- 内部 summary.json 的 source tree SHA-256 为 a2c3c053052647cb09088a9ae574bd92fa4188e54915c61af780bb1b619d9c82，4 files、1,179,671 logical bytes；upload/download 的 source/destination tree hash 均相同，exit_code 均为 0。

### 构建和 CTest

- results/build.log:112：[112/112] Linking CXX executable cpnetflux_unit_tests；configure 参数为 tests=ON、TLS=ON、io_uring=OFF。
- results/ctest-targeted.log:4-15 首次定向运行的 file transfer/resume/checksum 三个脚本因 CRLF 导致 set: pipefail 解析失败；同一日志的 tree smoke 仍通过。
- results/ctest-rechecked.log:58-60：修正独立 WSL 构建副本后 27/27 通过，26.35 s；其中包含 EventLog、TreeTelemetry parser/lifecycle/logger、file smoke、tree upload/download/resume/parallel/control-reuse/changed/edge/manifest-corrupt。
- results/ctest-unit-all.log:370-372：全单元 183/183 通过；:137-139 和 :375 明确 FileIoTest.IoUringContextReadWriteSmokeWhenAvailable skipped。该 skip 不计为功能通过，也不阻塞本任务的 POSIX/io_uring=OFF 观测范围；不能外推 io_uring backend。

### fresh telemetry on/off

本轮用 Python 在内存中重新读取四个 JSONL 文件，未运行项目 parser：

- off-upload：10 条旧 event 行，0 条 tree_stage/identity；
- on-upload：100 条，其中 10 条旧 event、90 条 tree_stage；stage 为 run_preflight、control_acquire、control_prepare、data_connect、first_payload、payload_io、data_channel_finalize、transfer_complete_wait、manifest_finalize、interfile_idle；
- off-download：18 条旧 event 行，0 条 tree_stage/identity；
- on-download：224 条，其中 18 条旧 event、190 条 tree_stage、16 条 stream_identity；额外有 download_mtime_finalize 和 stream_identity。
- 本轮独立 Python JSONL 读取脚本退出码 0；结构重读未发现 malformed/duplicate 行，这不是 C++ validator 的运行态证明。
- summary.json 同时给出 telemetry_off_emitted_no_stage_rows=true、telemetry_on_emitted_stage_rows=true、equivalent_hashes=true。fresh 范围内这支持“观测开关没有改变退出码和最终 tree hash”，不支持 DATA frame/wire、manifest canonical projection、resume decision 或性能开销等价。
- on-upload 的 90 条、on-download 的 206 条 telemetry 数以 summary 的 telemetry_rows 字段为准；独立按 event 分类时 download 为 190 stage + 16 identity，和 206 相符。

### CRLF 归一化的证据边界

任务要求记录归一化脚本名和范围。现有 bundle、ctest 日志和候选实现回执都**没有保存脚本名、命令行或 commit**；因此归一化工具名称为 NOT_RECORDED，不能声称已经独立复现。

可确定的对象是：

- 首次失败恰为 tools/test/run_file_transfer_smoke.sh、tools/test/run_file_resume_smoke.sh、tools/test/run_file_checksum_smoke.sh，对应 ctest-targeted.log:4-15；
- source-worktree.tar 的只读扫描发现 8 个 shell 文件带 CRLF，其中包括上述三个和 tools/test/run_file_checksum_private_once.sh、四个 tools/perf 脚本。bundle 没有记录后者是否被处理；
- rechecked.log 只证明独立 WSL 构建副本重新执行成功，不能证明使用了哪一个 LF 工具，也不能证明仓库源文件被修改。没有发现仓库源码被该操作改写的证据。

所以这次调整只能作为“独立构建副本的测试脚本行尾修正”看待；在补存确切命令、目标文件清单和修正后快照之前，归档可复现性为 PARTIAL。

## Critical / Important / Minor

### Critical

对“受限 fresh 观测 profile”没有发现必须立即停止的代码或证据 Critical。若 profile 入口要求包含 resume/no-range、wire 等价或 overhead eligibility，则下列 Important 项会升级为该入口的硬阻塞，不能绕过。

### Important

1. **no-range/resume on/off 未运行。** 现有 tree_resume smoke 是普通功能回归，不能替代 telemetry on/off paired no-range/resume_partial。要分析 resume 或 skip 阶段，必须先完成这些 cell 的哈希、manifest、missing-range、退出码和证据完整性。
2. **正式 10-pair overhead 未运行。** 现有 bundle 没有 wall/cpu/jsonl 的 paired 数据，也没有至少 8/10 valid、median/p95 结果；不得写 telemetry overhead 通过或 performance eligible。
3. **实际失败/重试/空文件/nullability 观测不足。** fixture 和 parser/logger unit 覆盖了规格，但 Linux fresh run 没有把这些分支的运行日志交付出来；依赖这些阶段做因果 profile 前必须补对应 fixture smoke。
4. **wire/frame/manifest 证据缺失。** 相同 tree SHA 只证明最终内容，不能证明 DATA frame 序列或 control/FIN/COMPLETE/226 accounting 相同。任何传输效率、协议开销或 100 Mbps 结论都被阻塞。
5. **CRLF 修正不可追溯。** 需保存实际归一化脚本/命令和仅涉及测试脚本的文件清单；否则后续固定证据不能完全再生。
6. **mixed old/new consumer 仅静态可读。** run_alpha_demo.py 的 JSON/error_code 读取与混合日志形状相容，但没有独立运行态 consumer 检查。

### Minor

- bundle 内部清单和 source archive hash 自洽，但无法与 WSL 原始目录做第二来源比对；
- io_uring 可选测试 skipped，结论只限 POSIX、io_uring=OFF 构建；
- 本轮独立 Python JSONL 重读不是 C++ parser/validator 的替代。

## 可执行的最小下一步

1. 在固定 Linux 快照中补保存 CRLF 归一化的确切命令/脚本名、目标文件清单和修正前后哈希；保持仓库源树不变。
2. 运行 fresh、no_range、resume_partial 的 upload/download telemetry off/on paired smoke；每对保存 exit、每文件 hash、manifest projection、missing ranges、evidence status，并在可行时保存 DATA frame/wire accounting。
3. 按既定随机交错方案完成每个 cell 10 对、至少 8 对 valid 的 wall/cpu/jsonl overhead gate；fault-injection run 不得进入 overhead 样本。
4. 单独运行 mixed old/new consumer 和真实传输中的 logger failure isolation；任何 evidence partial 必须排除出 performance eligibility。
5. 只有上述门禁闭合后，才可把目录阶段观测扩大为一般 profile；仍需另行决定是否创建实现提交或性能实验任务。

## 未运行项和结论边界

本轮未运行：CMake、CTest、C++ parser、真实 logger fault injection、mixed consumer、fresh/no-range/resume 的新传输、hash/resume 等价重跑、DATA/wire 抓取、10-pair overhead、SSH、云端实验和清理。

本结果不批准代码合入、不批准性能优化、不批准跨域/100 Mbps 结论。00 若接受范围约束，可把候选带入有限 fresh POSIX 目录阶段观测；若 profile 任务包含上述 Important 门禁，则应先补门禁并重新 QA。

验收人：00 总指挥（待验收）。


