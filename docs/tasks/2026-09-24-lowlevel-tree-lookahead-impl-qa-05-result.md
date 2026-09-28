# LOWLEVEL-TREE-LOOKAHEAD-IMPL-05 独立质量验收

## 结论

**BLOCKED：不通过本轮实现验收，也不授权作为已验收结果进入 05 固定 Linux 构建任务。**

白名单和默认关闭的静态边界基本符合契约，但候选准备线程的停止/关闭、异常隔离和资源计量仍不能证明在 scheduler stop、认证/连接失败、首错或取消时回到原路径。Windows 没有可用 CMake/CTest/C++ 编译器，WSL 返回 `E_ACCESSDENIED`，所以没有动态证据可以降低这些风险。若 00 需要编译诊断，应另派修订任务；本结果不把静态检查、实现者回执或旧 loopback 结论写成通过。

## 输入与工作树门禁

- 路线/任务：`R2026-09-24.12/v1`，`LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`。
- 固定实现输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。
- 主工作树：`git rev-parse HEAD` 返回 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`（退出码 0）；已有共享文档改动保留，未切分支。
- 实现 worktree：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`，分支 `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`，`git rev-parse HEAD` 返回固定输入（退出码 0）。
- 主树与实现 worktree 的 `git diff --cached --name-only` 均为空；没有执行 `git add`、提交、切分支或修改 index。
- 主树固定三源码相对固定输入的 parity 检查退出码 0。实现 worktree 的提交仍是固定基线，工作区修改仅按下表白名单审查。
- 主树、实现 worktree 的 `git diff --check` 均退出码 0；仅有 Git 的 LF→CRLF 提示，没有尾随空白错误。

## 白名单与禁止路径

实现 worktree 的 `git status --short --branch` 显示的 tracked 修改为：

- `CMakeLists.txt`
- `include/cpnetflux/config/tree_transfer_options.h`
- `src/config/tree_transfer_options.cpp`
- `src/core/io/tree_transfer_client.cpp`
- `tests/unit/tree_transfer_options_test.cpp`

白名单新增文件为 `include/cpnetflux/core/io/tree_lookahead.h`、`src/core/io/tree_lookahead.cpp`、`tests/unit/tree_lookahead_test.cpp`；worktree 内的任务回执仅是实现者已有文档。对 file client/download、framed/control protocol、manifest/checkpoint/resume/checksum、scheduler/compression/IO backend 等禁改路径执行 `git diff --name-only`，结果为空。该项 **PASS（静态范围）**。

实现回执中列出的 `rg` 门禁也可复核：`control_id|controlId` 无匹配（退出码 1，表示无匹配），`state.nextIndex++` 裸旁路无匹配（退出码 1），lookahead 新模块未引用 manifest 写入、文件级命令或 transfer ID。它们只证明文本边界，不证明运行时路径。

## 静态行为复核

| 检查项 | 结论 | 依据与边界 |
|---|---|---|
| 选项与默认行为 | PASS（静态） | `tree_transfer_options.*` 接受 0/1，默认 0；实现回执含负数、>1、溢出和缺值检查。未编译或运行 parser。 |
| Pending/方向/指纹 eligibility | PASS（静态） | `src/core/io/tree_lookahead.cpp:22-34` 检查 `TreeFileStatus::Pending`、方向、路径、endpoint/options、size/mtime；`reserveLookaheadLocked` 在 `tree_transfer_client.cpp:1277-1310` 调用。动态状态序列未运行。 |
| owner/generation/fingerprint 与单次 handoff | PASS（静态） | `tree_lookahead.cpp:91-120` 的 `markControlReady`/`consumeCandidate` 和 `:123-166` 的 finish/cancel 做身份校验；单测文件覆盖 stale、单次消费和归还，但未运行。 |
| scheduler 锁接缝 | PARTIAL | `claimWorkLocked` 在统一 mutex 下推进 `nextIndex`、claim/reserve/returned；worker 在 `tree_transfer_client.cpp:2640-2708` 以同一锁提交 handoff/finish/cancel。由于没有动态并发、cancel-vs-handoff 和 stop 测试，不能证明所有 interleaving。 |
| 候选阶段协议边界 | PASS（静态） | 候选代码只做 upload 本地 stat/metadata 或 `ensureControlReady`；没有候选 data FD、manifest/event/summary/transfer ID。`ensureControlReady` 位于 `tree_transfer_client.cpp:484-500`，包含连接、认证、`login` 内的登录设置；尚需 Linux trace 证明没有越过文件命令边界。 |
| handoff 后原路径 | PARTIAL | `controlForFile` 接收 handoff control，随后仍调用原 `processFile`；但没有编译/真实 upload/download、resume/changed、checksum/manifest failure 或 retry 证据。 |
| worker/global/off 边界 | PARTIAL | lookahead 仅在 `runTreeScheduler` 普通 worker 路径计算 effective depth；global scheduler 没有接入。`control_reuse=worker` 设 metadata-only，`off` 才建普通 control。未运行选项组合和 global/多 worker 测试。 |
| 资源硬门 | BLOCKED | `effectiveDepthAllowed` 和 `reserveCandidate`（`tree_lookahead.cpp:13-87`）只按 Candidate 结构及三个 string capacity 计 `candidateBytes`；实际 `CandidateRuntime`、mutex/condvar、`std::thread` 栈、`ControlClient`/TLS/socket 缓冲未计入 1 MiB/2 MiB 上限。`extraFdPeak` 是逻辑 pending 计数，不是实际 socket 峰值。不能证明契约要求的硬门可靠。 |

## Critical / Important 阻塞

### Critical：候选线程无可合作取消，join 可能无限等待

`CandidatePreparation` 在 `tree_transfer_client.cpp:1055-1070` 的析构函数无条件 `thread.join()`。`startCandidatePreparation`（`:1351-1388`）启动线程后同步执行 `ensureControlReady`；该路径连接、认证和读取没有传入停止 token、超时或 socket shutdown。`waitCandidatePreparation`（`:1390-1416`）使用无超时 `condition.wait`，首错或 scheduler stop 不能唤醒网络操作。最终清理（`:2720-2735`）先取消逻辑 candidate，再清空 shared pointer，仍会触发上述 join；worker 失败路径也会释放 candidate。

这违反实现契约要求的 stop/cancel/close/wait 回退边界：认证拒绝通常可返回错误，但连接黑洞、TLS 阻塞或服务端不读时，scheduler 可能卡在 worker join，而不是回到原 index 的普通路径。必须先有停止 token、可中断连接/读、有限等待和可观测 close/wait 失败状态，才能进入固定 Linux 验收。

### Critical：候选线程异常未隔离

线程 lambda（`tree_transfer_client.cpp:1360-1387`）没有 `try/catch`。`std::make_unique<ControlClient>`、文件系统操作、状态对象分配或 `ensureControlReady` 内部任何异常若逃出线程，将调用 `std::terminate`，不会写入 `preparation->status`、不会归还 index，也不会走原始 `processFile`。必须将异常转换为候选失败并保证通知、归还和原路径回退；需有故障注入单测/集成测试。

### Important：close/wait 失败没有独立的可验收状态

当前只依赖 RAII 析构释放 `ControlClient`，没有 close/wait 的结果、超时或取消状态可供 scheduler 判断。该问题与上面的阻塞 join 叠加；不能以“析构最终会关闭”替代失败清理证据。

### Important：资源预算与实际对象不一致

`candidateFootprint`（`tree_lookahead.cpp:8-12`）只计算 `sizeof(Candidate)` 和 fingerprint 字符串容量。候选 runtime、同步原语、线程栈、控制对象、TLS/socket 缓冲和准备期间的临时对象未计入；因此即使逻辑 `candidateBytes <= 2 MiB`，仍不能声称满足 candidate memory 硬上限。应完整计量，或在无法计量时保守地将 control candidate/effective depth 设为 0，并记录可复核的实际峰值。

### Important：实际 FD 峰值未观测

`sampleActiveFdCount`（`:1441-1458`）只在 scheduler 初始化读取 `/proc/self/fd`；`extraFdPeak` 在 reservation 表中由 pending 数推导，未在 socket 建立/关闭处测量。它可作为保守逻辑门，但不是“extra FD≤2”的运行时证据，必须由 Linux 故障/并发测试或明确计量门补齐。

## 动态构建、测试与 smoke

以下项目均为 **NOT_RUN/BLOCKED**，不是通过或跳过即算通过：

| 项目 | 实际命令/结果 | 状态 |
|---|---|---|
| CMake 配置 | `cmake -S <worktree> -B <worktree>\\build-lookahead -DCPNETFLUX_BUILD_TESTS=ON`；PowerShell 报 `cmake` 未识别 | NOT_RUN/BLOCKED |
| 编译 | 未配置构建目录，且 Windows 无 C++ 编译器 | NOT_RUN/BLOCKED |
| lookahead/options 单测 | 未运行；无 CTest/编译器 | NOT_RUN/BLOCKED |
| 受影响 tree 单测 | 未运行 | NOT_RUN/BLOCKED |
| 完整 CTest | 未运行；不可把缺失环境当 skip/pass | NOT_RUN/BLOCKED |
| loopback upload/download | fresh、empty、Completed skip、resume/changed、worker/off、TLS/认证失败、并发重叠均未运行 | NOT_RUN/BLOCKED |
| hash/manifest/旧 summary/event/wire 等价 | 未生成任务专属外部证据目录 | NOT_RUN |
| WSL/Linux 入口 | `wsl --status` 与 `wsl.exe bash -lc ...` 返回 `WSL/EnumerateDistros/Service/E_ACCESSDENIED` | BLOCKED |

没有 SSH、云端构建、传输、实验、清理或性能测试；没有跨域、100 Mbps、GridFTP 或收益结论。实现者回执中的 NOT_RUN 记录与本轮环境复核一致，但不是 04 独立测试证据。

## 最小修订与下一步门

1. 在候选准备路径增加可合作取消：stop token/状态、连接与阻塞读的超时或 shutdown、取消时唤醒 condition；对 close/wait 结果建立明确失败状态，保证 scheduler stop、首错、TLS/auth reject、断连都能有限时间退出并回到同一 index 原路径。
2. 在线程入口捕获所有异常，统一写入候选失败状态并通知等待者；确保 `std::thread` 创建失败、准备失败、取消和 stale 不丢失/重复归还 index。
3. 修正 candidate memory/FD 硬门：计入准备对象和实际控制/TLS 资源，或在不能可靠量测时 depth=0；Linux `getrlimit`、socket 峰值、候选清理需要可观测断言。
4. 修订后在可用 Linux 环境重新运行：配置/编译；lookahead/options、tree 受影响单测和完整 CTest；真实 fresh/empty/Completed/no-range/resume/changed、manifest/checksum failure、TLS/auth/cap/reject、cancel/stop、并发多 worker、worker/off/global；核对源/目标 hash、manifest 最终状态、旧 summary/event/wire/frame 等价。任何 skip、失败或环境阻塞逐项记录。

在 1–3 未闭合前，不应创建“已验收后进入 05 固定 Linux 构建”的绿灯记录。修复后可另派窄实现修订/再验收任务；本轮不修改 worktree、不创建新 worktree、不授权合入。

## 执行回执

- 实际输入/输出 commit：输入 worktree HEAD `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；主资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本轮仅新增本结果与独立摘要，未提交。
- 实际改动：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-qa-05-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-qa-05-last-message.md`。
- 实际命令、退出码与证据文件：主树/worktree `rev-parse`、`status`、`diff --cached --name-only`、`diff --check`；白名单/禁改路径 diff；`rg` 静态门；实现回执 `build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true/docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-05-result.md`。动态命令及限制见上表。
- transfer/integrity/evidence/wire：未运行真实传输，不能给运行态结论；静态上 handoff 后复用原 `processFile`，但 hash/manifest/旧输出/wire 等价均 NOT_RUN。
- 失败/跳过/阻塞：Linux/CMake/CTest 不可用；候选停止/异常/资源硬门是实现级阻塞。
- 剩余风险：候选连接/认证可能阻塞 scheduler；线程异常可能终止进程；候选资源预算与 FD 峰值未闭合；未有任何动态回归证据。
- 下一角色可直接执行的下一步：03 修订候选取消/异常/资源计量后，由 00 另发 Linux 固定构建与动态回归任务；04 再独立验收。不得将本回执当产品或性能通过。

