# LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-07-QA

## 独立结论

**PASS（仅允许下一步 05 固定 Linux 构建/预检任务）。**

revision-07 静态闭合了 QA-REVISION-06 指出的确定性 control/storage 泄漏：control 释放改为 owner/generation 加 ownership 位判定，允许 ControlPending、ControlReady、InUse、Cancelled、Failed 五种状态；先清 ownership 位再递减 live counter，重复回调不会二次递减。未发现新的静态生命周期、协议范围或默认行为致命问题。

本 PASS 不代表代码已编译、运行态测试、传输正确性、性能或跨域验收，也不授权合入；只允许 00 另发 05 的固定 Linux 构建/预检任务。

## 发现与静态依据

### control/socket/storage 释放：PASS（静态）

- src/core/io/tree_lookahead.cpp:258-271 的 releaseCandidateControl 不再要求 ControlPending，只检查 index、owner、generation、controlAcquired 和 live counter，然后先清 ownership 再递减 liveCandidateControls。
- src/core/io/tree_lookahead.cpp:286-294 的 releaseCandidateSocket 按 owner/generation 和 socketsAcquired 一次性扣减。
- src/core/io/tree_lookahead.cpp:80-93 的 releaseStorage 拒绝仍持有 control/socket 或仍为 ControlPending 的候选，之后才清候选并递减 liveCandidates/candidateBytes。
- src/core/io/tree_lookahead.cpp:60-77 的 terminateCandidate 在进入 Cancelled/Failed 时递减 pending，并通过 returnIndexOnce 保证索引最多归还一次。
- tests/unit/tree_lookahead_test.cpp:334-391 的 ControlReleaseIsOwnershipBasedAcrossTerminalStates 覆盖五个状态、重复 control release、socket release、finish/storage 和计数归零；测试未运行，不能写成运行态通过。

### 回调、handoff、取消、失败、stop：PASS（静态，动态待验证）

- src/core/io/tree_transfer_client.cpp:111-131 的 ControlClient 析构调用 close；close 在 socketMutex_ 内交换 candidateSocketHeld_/candidateControlHeld_，先释放 socket 回调再释放 control 回调，交换位使多次 close 幂等。
- :74-107 的 CandidateSocketLease 在失败/短连接析构 fd 后释放 socket ownership；成功交接用 transferAccounting 把 ownership 转给 ControlClient，避免双减。
- :1313-1320、:1382-1385 的 requestStop/析构路径设置 stop、唤醒并 join 准备线程；:1891-1911 在 join 后取出控制对象、reset 触发 RAII 回调，再锁 scheduler 清理 socket、control、storage。
- :3187-3210 的成功 handoff 先销毁 retired control，再释放 socket/control，最后 finishCandidate；:3193-3214 的失败/取消路径使用 cancelWorkerCandidateLocked 和 stopJoinAndReleaseCandidate。
- 候选 CandidateRuntime 由 map/local shared_ptr 持有到线程 join 和回调完成；requestStop 只在 preparation mutex 下调用 socket interrupt，scheduler mutex 在 join 后才取得。静态未发现明显 UAF 或反向锁死。
- Linux 动态跨线程 shutdown、异常、TLS/auth 拒绝、断连和 stop race 未运行。

### 资源门和生产回退：PASS（契约边界；运行态 NOT_RUN）

- src/core/io/tree_lookahead.cpp:149-191 保持 depth、pending/control/socket、FD headroom 和 candidate memory 门；revision-07 没有放宽上限。
- src/core/io/tree_transfer_client.cpp:3091-3099 仍固定 reliableCandidateMemory=false，生产 effectiveLookaheadDepth 保守为 0；当前没有 lookahead 性能激活。
- 测试中的 reliableCandidateMemory=true 只是模拟状态机，不是生产内存上限证明；Linux FD 峰值、资源计数和停止清理仍待 05 动态门禁。

### 白名单、协议和默认行为：PASS（静态）

实现 worktree 相对固定 HEAD 的修改落在既有白名单：

- CMakeLists.txt
- include/cpnetflux/config/tree_transfer_options.h
- src/config/tree_transfer_options.cpp
- src/core/io/tree_transfer_client.cpp
- tests/unit/tree_transfer_options_test.cpp
- 新增 include/cpnetflux/core/io/tree_lookahead.h、src/core/io/tree_lookahead.cpp、tests/unit/tree_lookahead_test.cpp

file transfer/download client、framed/control protocol、manifest/checkpoint/resume/checksum、scheduler/compression/IO backend 未见差异。对 lookahead 新模块扫描 control_id、controlId、SIZE、MDTM、EPSV、REST、STOR、RETR、transferId、writeEventLog、writeTreeJsonSummary、SessionInit 无匹配（rg 退出码 1，表示无匹配）。control_reuse=worker 仍只允许 metadata candidate，off 才允许普通 control candidate；默认 depth=0，global scheduler 不启用。

## 输入、工作树和索引门禁

- 主资料 HEAD：a076c532640ba06de016ed7ed20f7d2a6d48a0a7，git rev-parse 退出码 0。
- 实现 worktree：D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true，branch codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05，git rev-parse 为固定 3b0820dab6dc149f549bd3e81ef403ea7953c4e9，退出码 0。
- 主树和实现 worktree git diff --cached --name-only 均为空；本轮未操作 index、分支、提交或 worktree。
- 实现 worktree git diff --check 退出码 0；只有 Git 的 LF→CRLF 提示，没有 whitespace error。
- 实现回执 docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-07-result.md 的 SHA-256 为 C57B638CF141ABC5802586B1A4737A041BE5C5E40764A870899A8482C17C207E。

## 构建和动态测试

工具核验结果：

- Get-Command cmake,ctest,ninja,g++,clang++：仅发现 wsl.exe，退出码 1。
- cmake --version、ctest --version：命令未识别。
- wsl.exe --status：WSL/EnumerateDistros/Service/E_ACCESSDENIED，退出码 -1。
- wsl.exe bash -lc 'command -v cmake; command -v ctest; command -v ninja; command -v g++'：WSL/Service/CreateInstance/E_ACCESSDENIED，退出码 -1。

以下均为 NOT_RUN/BLOCKED，不作通过：

- cmake -S D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true -B <linux-build> -DCPNETFLUX_BUILD_TESTS=ON
- cmake --build <linux-build> --parallel <jobs>
- ctest --test-dir <linux-build> -R TreeLookaheadTest|TreeTransferOptionsTest
- 受影响 tree 单测、完整 CTest、Linux FD/resource assertion
- plain connect/read stop、TLS/auth reject、exception injection、scheduler stop、并发 cancel/handoff
- fresh/empty/resume/changed、manifest/checksum failure、hash/wire/旧 summary/event 等价 smoke

未执行 SSH、云端构建、传输、实验、清理、性能矩阵或跨域验证。

## 下一步门禁

00 可创建白名单明确的 05 固定 Linux 构建/预检任务，至少运行新增 lookahead/options 单测、受影响 tree 单测、完整 CTest，并验证候选停止/失败/析构回调、五状态资源计数和真实 hash/manifest/wire 等价。只有这些证据完成后，才可决定更大范围 smoke 或 profile；本回执不允许将静态 scope 结论写成性能或跨域收益。

## 执行回执

- 实际输入/输出 commit：主树 a076c532640ba06de016ed7ed20f7d2a6d48a0a7；实现 worktree 3b0820dab6dc149f549bd3e81ef403ea7953c4e9；输出为本文件和同名 last-message，未提交。
- 实际改动：仅 docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-07-qa-result.md、docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-07-qa-last-message.md。
- transfer/integrity/evidence/wire：未运行真实传输，均 NOT_RUN。
- 阻塞：动态 Linux/CMake/CTest 环境不可用；这是后续 05 任务的环境前置，不改变本轮静态 PASS。
- 剩余风险：跨线程停止、真实 TLS/auth/断连、FD 峰值、hash/manifest/wire 等价和性能均未验证。
- 下一角色可执行：05 在固定 Linux 环境配置、构建、测试本 worktree；04 再依据运行态结果复核。
