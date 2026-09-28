# LOWLEVEL-TREE-LOOKAHEAD-QA-REVISION-08

## 结论

**BLOCKED。**

revision-08 的 handoff 重复请求修复在当前 worktree 中静态正确，但没有可用于本 revision 的 Linux CTest 结果。深圳现有构建的源快照仍是旧实现，源 SHA 与 revision-08 不同；既有运维结果中的 #180/#181 失败正来自该旧语义。因此不能把旧二进制的测试结果写成 revision-08 通过，也不能确认同组测试在修复后无新失败。

## 静态审查

### consumeCandidate 身份和重复语义：PASS（静态）

实现 worktree 的 src/core/io/tree_lookahead.cpp:297-310 顺序为：

1. index 越界返回 Stale；
2. owner、generation 或完整 fingerprint 不匹配返回 Stale；
3. 身份匹配且 handoffConsumed 已置位返回 AlreadyConsumed；
4. 未消费时才检查 ControlReady 或 metadata-only 的 MetadataReserved；
5. 可消费时置 InUse、置 handoffConsumed，并更新 reserved/claimed。

这满足“错误身份优先 Stale、同身份重复 AlreadyConsumed”。重复调用不会重新转移 opaque 或再次改变索引归属。

单测静态向量也覆盖该顺序：

- tests/unit/tree_lookahead_test.cpp:64-91 的 full-control 测试验证首次 Consumed、同身份重复 AlreadyConsumed，以及错误 owner、generation、fingerprint 均为 Stale。
- :96-122 的 metadata-only 测试验证同身份重复 AlreadyConsumed 和三类错误身份 Stale。
- :234-317 的 cancel-vs-handoff 仍只允许消费或取消其中一个结果。

### 资源归属、取消和默认行为：PASS（静态，动态未运行）

revision-08 只改变 consumeCandidate 的结果分类，没有改动 revision-07 已修复的 control/socket/storage 释放逻辑。以下向量仍存在且未见静态回归：

- :124-143 CancelReturnsIndexOnceAndReleasesAfterThreadCleanup；
- :145-174 DepthGateDefaultsOffAndRejectsUnsafeBudget；
- :176-195 FailedPreparationReturnsReservedIndexOnce；
- :234-317 FailureCancelAndStopRaceReturnsOneIndexAndRollsBackResources；
- :319-345 CandidateSocketAccountingIsLiveAndFailClosedAtTwo；
- :347-394 ControlReleaseIsOwnershipBasedAcrossTerminalStates。

生产路径仍由 reliableCandidateMemory=false 保守关闭 lookahead；本轮未编译验证 parser 或运行态 default depth=0。

## Linux 构建与证据核对

### 本地与 worktree

- 主树 HEAD：a076c532640ba06de016ed7ed20f7d2a6d48a0a7，git rev-parse 退出码 0。
- 实现 worktree：D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true，branch codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05。
- 实现 worktree HEAD：3b0820dab6dc149f549bd3e81ef403ea7953c4e9，退出码 0；index 为空。
- revision-08 回执 SHA-256：56C763A9E059E0DF197DD9D113299023FFC692663E5FC58EAAAEEDC870811F32。
- 当前 revision-08 源文件 SHA-256：40F9994B4BF8E5BF1003A488ADAD722FE655AE806FB34F032C71EB5F8DC08A44。
- 当前 lookahead 源和单测为 UTF-8、LF、无 CR 字节和无尾随空白；worktree git diff --check 退出码 0。

### 深圳现有构建的独立核对

只读 SSH 查询使用现有 alias gridflux-beta-shenzhen，未写远端、未清理、未停止服务。

远端现有目录：

- source：/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-08/canonical/execution-source-root/source
- build：/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-08/canonical/execution-build

远端 source/src/core/io/tree_lookahead.cpp SHA-256 为 110E7784862620F9128ABeba071D2542A11F363D41254B326DFF3CE41B5BE9CA，与当前 revision-08 本地源 40F999… 不一致。远端源码片段仍为旧顺序：先计算 ready，再在 !ready 或 handoffConsumed 分支中按 ControlReady 返回 AlreadyConsumed；它不是本 revision-08 的先身份校验实现。

远端只读 ctest discovery：

    ctest --test-dir /tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-08/canonical/execution-build -N -R "^(TreeLookaheadTest|TreeTransferOptionsTest)\." --no-tests=error

退出码 0，发现 23 项（TreeTransferOptionsTest #170-178、TreeLookaheadTest #179-192）。该命令只列出测试，没有运行测试。

既有运维回执 docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-08-result.md 的结果为定向 31 项 29 pass、2 fail，完整 224 项 221 pass、2 fail、1 skipped；失败为 #180 ReservationAndHandoffAreSingleUse 和 #181 MetadataOnlyHandoffRequiresOwnerGenerationAndFingerprint。回执和日志路径为：

- build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-logs/targeted-ctest.log
- build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-logs/full-ctest.log
- build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-evidence/binary-sha256.txt

这些结果属于旧 source/binary，不能作为修复后的绿灯证据。io_uring 用例的 skip 也不计通过。

### 本轮动态状态

以下命令没有在 revision-08 代码上执行：

- CMake configure/build：无与 revision-08 源快照对应的 Linux build。
- ctest --test-dir <revision-08-build> --output-on-failure --parallel 1 --no-tests=error -R "^(TreeLookaheadTest|TreeTransferOptionsTest)\."：NOT_RUN；没有对应构建目录。
- #180/#181 及同组取消、失败、释放、默认/unsafe budget/socket 测试：NOT_RUN。

原因是现有深圳构建已被源 SHA 核验为旧 revision；本机没有可用 CMake/CTest/C++ 工具链，WSL 入口此前返回 E_ACCESSDENIED。未复制或修改远端源，也未启动新的构建任务。

## 阻塞与下一步

阻塞只针对动态验收证据，不是对 revision-08 静态 handoff 逻辑的 FAIL 判定。需由 05 在隔离 Linux 源快照中重新配置/编译 revision-08，然后运行：

1. TreeLookaheadTest 与 TreeTransferOptionsTest 定向全集；
2. 明确核对 #180/#181 返回 Consumed、AlreadyConsumed、Stale；
3. 核对 cancel/failure/release、默认 depth=0、unsafe budget/socket gate 无新失败；
4. 记录构建、CTest、二进制和源快照 hash，并把 io_uring skip 单独列出。

在上述结果产生前，不批准 lookahead 进入下一动态验收或性能结论。

## 执行回执

- 实际输入/输出 commit：主树 a076c532640ba06de016ed7ed20f7d2a6d48a0a7；实现 worktree 3b0820dab6dc149f549bd3e81ef403ea7953c4e9；本轮未提交。
- 实际改动：仅本结果文件和同名 last-message 文件。
- 实际命令、退出码和证据：主树/worktree rev-parse、status、index、diff-check；本地静态读取；深圳只读 SSH 源 SHA 和 ctest -N discovery（退出码 0）；实际 CTest 未运行。
- transfer/integrity/evidence/wire：未运行传输或性能；均 NOT_RUN。
- 失败/跳过/阻塞：现有构建源与 revision-08 不匹配；#180/#181 的旧失败不能重用；io_uring skip 不计通过。
- 下一角色可执行下一步：05 创建匹配 revision-08 的隔离 Linux 构建并运行定向 CTest，04 再独立复核。
