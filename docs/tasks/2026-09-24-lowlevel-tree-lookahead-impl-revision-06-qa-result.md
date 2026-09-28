# LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-06 独立质量验收

## 结论：BLOCKED

revision-06 补上了停止信号、非阻塞候选连接、线程异常发布和 production fail-closed memory gate，但没有闭合资源释放不变量。`releaseCandidateControl` 只接受 `CandidateState::ControlPending`；控制候选在 handoff 后是 `InUse`，在取消/失败后是 `Cancelled/Failed`。因此控制资源计数不会释放，storage 也不会释放。这是源码可直接推出、与新增单测断言冲突的确定性 blocker。

本轮不授权进入 05 固定 Linux 构建验收或云端验证。修复该状态/资源清理缺陷后，仍须在 Linux 上编译并运行聚焦测试和真实候选停止/失败门；本结果不作性能结论。

## Critical findings

### C1. 控制资源在 handoff、取消和失败路径均无法释放

`src/core/io/tree_lookahead.cpp:259-268` 的 `releaseCandidateControl` 要求 `candidate.state == CandidateState::ControlPending`。但：

1. `consumeCandidate` 在 `:296-315` 将成功 handoff 的候选置为 `InUse`。worker 完成后，`src/core/io/tree_transfer_client.cpp:3198-3210` 先销毁 `retiredControl`，再调用 `releaseCandidateControl` 和 `finishCandidate`。前者因 `InUse` 返回 false；`releaseStorage`（`tree_lookahead.cpp:80-93`）看到 `controlAcquired` 仍为 true，也返回 false。`liveCandidateControls`、`liveCandidates`、`candidateBytes` 会泄漏。
2. `terminateCandidate`（`tree_lookahead.cpp:60-77`）先将候选改为 `Cancelled`/`Failed`，然后 `stopJoinAndReleaseCandidate`（`tree_transfer_client.cpp:1891-1911`）才释放控制资源。此时同一个状态门仍使 `releaseCandidateControl` 返回 false，storage 同样无法释放。
3. 控制对象自身析构通过 `releaseCandidateControlResource` 回调释放，但该回调也调用相同的状态受限函数；它不能修复上述顺序问题。

新增单测已经写出相反的契约：`tests/unit/tree_lookahead_test.cpp:64-85` 在 handoff 后断言 control release 和 finish 成功，`:221-260` 在 failure/cancel race 后断言 control release、storage release 和计数归零。按当前实现，这些断言在有工具链时应失败；本轮虽未编译，但 blocker 不依赖动态证据。

最小修订是让控制资源释放按 `controlAcquired` 的所有权做一次性、幂等清理，允许 `ControlPending/ControlReady/InUse/Cancelled/Failed` 的合法释放，或在状态转换前完成释放；必须保证 handoff、失败、取消、线程异常和析构回调不重复减计数，并用上述测试覆盖每个状态。

## 其他静态复核

| 项目 | 结论 | 证据与边界 |
|---|---|---|
| 候选 stop/cancel | PARTIAL | `CandidatePreparation::requestStop`（`tree_transfer_client.cpp:1313-1320`）设置 atomic stop、在 preparation mutex 下对活动控制 socket 调 `interrupt`，不持有 scheduler mutex；候选 connect 使用 numeric host、nonblocking connect、100 ms poll、5 s deadline，候选读有 socket timeout/15 s deadline。TLS/token candidate 在 `:1777-1783` fail closed 到普通路径。静态锁序未发现明显 UAF/deadlock；实际 socket/TLS/停止竞争未运行。 |
| 异常发布 | PASS（静态） | `startCandidatePreparation`（`:1737-1825`）线程入口为 `noexcept` 并捕获异常，`PreparationSignal` 终态发布并唤醒等待者；线程构造异常也发布失败。动态故障注入未运行。 |
| exactly-once index return | PARTIAL/BLOCKED | `failCandidate/cancelCandidate` 调 `returnIndexOnce`，owner/generation 受检；但随后 control/storage 清理失败，索引虽入 returned 队列，资源状态不闭合。 |
| 资源计量 | PARTIAL/BLOCKED | reservation、pending、live candidate/control/socket 与 FD headroom 计数在 `tree_lookahead.cpp:146-292` 增加。`runTreeScheduler` 在 `tree_transfer_client.cpp:3091-3099` 固定 `reliableCandidateMemory=false`，因此 production effective depth 恒为 0，属于保守正确性回退，但没有性能激活；上述 control release bug 仍使启用状态机不可靠。Linux `/proc/self/fd` 和资源回滚未动态验证。 |
| worker reuse / 协议边界 | PASS（静态范围） | worker reuse 候选不建普通 control，只走 metadata；`control_reuse=off` 才准备普通 control。对 `tree_lookahead.cpp/.h` 的 `SIZE/MDTM/EPSV/REST/STOR/RETR/writeEventLog/writeTreeJsonSummary/emitTreeEvent/transferId/dataFd/SessionInit` 扫描无匹配（退出码 1）。候选模块不写 manifest/event/summary，不建 data FD；login 中的 USER/PASS/TYPE/OPTS 属登录设置。 |
| 默认行为 / global 边界 | PASS（静态） | parser 默认 depth=0；`effectiveDepthAllowed` 同时拒绝 global、非 1、非可靠 memory/FD。由于 production memory gate 固定 false，depth=1 当前不会实际启用。 |
| scope / identity | PASS（静态） | source/test/CMake 改动在既有白名单；禁改 client/protocol/manifest/checksum/scheduler backend 路径 diff 为空；`control_id|controlId` 和裸 `state.nextIndex++` 扫描无匹配。worktree 中两个旧 `impl-05` 文档 artifact 是复制 worktree 时已有，implementation receipt 说明未由 revision-06 修改。 |

## 输入、状态与命令证据

- 主工作树 `git rev-parse HEAD`：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，退出码 0；保持既有共享文档改动。
- 实现 worktree：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`，分支 `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`，`git rev-parse HEAD` 返回固定 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`，退出码 0。
- 主树和实现 worktree `git diff --cached --name-only` 均为空；没有 `git add`、提交、切分支、SSH 或云端动作。
- 实现 worktree tracked diff：`CMakeLists.txt`、两个 options 文件、`src/core/io/tree_transfer_client.cpp`、两个测试路径；新增 lookahead header/source/test 在白名单。禁改路径 diff 为空。
- 主树和实现 worktree `git diff --check` 退出码 0；仅有既有 LF→CRLF 提示，无尾随空白错误。
- 实现回执：`build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true/docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-result.md`。该回执明确 04 尚未复核，且先前 QA-05 文档不在复制 worktree 中；本轮以活动代码和本任务输入为准。

## 构建、测试和实验状态

尝试的配置命令：

```text
cmake -S build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true -B build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true/build-revision-06-qa -DCPNETFLUX_BUILD_TESTS=ON
```

Windows 报 `cmake` 未识别，未生成构建目录；没有可用 CMake、CTest 或 C++ 编译器。`wsl.exe --status` 与 `wsl.exe bash -lc 'command -v cmake; command -v g++; command -v ctest'` 均返回 `E_ACCESSDENIED`（退出码 `-1`）。因此：

- lookahead/options 单测：`NOT_RUN/BLOCKED`；
- 受影响 tree 单测、完整 CTest：`NOT_RUN/BLOCKED`；
- Linux 可观测 FD/连接中断、TLS/auth reject、scheduler stop、异常注入：`NOT_RUN/BLOCKED`；
- fresh/empty/resume/changed、hash/manifest/旧 summary/event/wire 等价和 loopback smoke：`NOT_RUN`；
- SSH、云端构建、性能实验和清理：未执行。

静态回退 depth=0 不等于 lookahead 运行态通过，也不产生任何性能收益证据。

## 最小修订与后续门

1. 修正 `releaseCandidateControl` 与 `releaseStorage` 的状态/所有权协议，覆盖 `InUse`、`Cancelled`、`Failed`；保证 `ControlClient::close` 回调、stopJoin、handoff finish 三者只能释放一次，并使计数、storage、returned index 一致归零。
2. 在 Linux 工具链可用后先运行新增 lookahead/options 单测；当前单测中的 handoff 和 failure/cancel 断言是修订后的最低门禁。
3. 再运行真实候选 plain connect/read stop、认证拒绝、TLS/token fail-closed、worker/off/global、并发重叠、resume/changed/manifest failure 和源/目标 hash/manifest/wire 等价；生产 depth=0 期间只记录 fail-closed，无性能 profile 授权。

## 执行回执

- 实际输入/输出 commit：输入 worktree HEAD `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；主资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本轮输出未提交。
- 实际改动：仅新增本结果与独立摘要文件。
- transfer/integrity/evidence/wire：未运行真实传输，均 `NOT_RUN`；不作正确性或性能通过。
- 结论：**BLOCKED**，原因是确定性的 control/storage 释放状态 bug，叠加动态工具链不可用。
- 下一步：03 修正资源释放状态协议，00 重新派发独立 QA；修复前不进入 05 固定 Linux 构建验收或云端验证。

