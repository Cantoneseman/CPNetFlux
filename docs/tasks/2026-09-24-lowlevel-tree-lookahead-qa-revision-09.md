# LOWLEVEL-TREE-LOOKAHEAD-QA-REVISION-09：固定构建证据独立复核

- 状态：in_progress
- 路线/任务版本：`R2026-09-24.9 / QA-REVISION-09 v1`
- 发起人：00 总指挥；执行角色：04 测试与质量；最终运维验收：00
- 目标：独立复核 05 的 OPS-REVISION-10 源码快照、Linux 构建和 CTest 证据，并判断 lookahead 是否在该二进制中可能实际启用，明确后续实验放行状态。
- 非目标：不修改源码、证据包、runner、BOARD/ROSTER；不重新构建、不 SSH、不启动传输/性能实验、不清理、不提交 Git。
- 固定源码基底：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。实现来自 `D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true` 的 5 个 tracked 修改和 3 个 untracked 源文件；它们是未提交工作树快照，不能称为某个完整 Git commit。
- 资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。证据包：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-artifacts\LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-evidence.tar.gz`，SHA-256 `2AF5C2DC166615FD4F0924BA335408624E8620B437B07B3528A98E08674A07C6`，1,878,966 bytes；索引 `build/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-artifacts/evidence-index.json`。
- 必读：本任务；`docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-revision-10-result.md`；`docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-05.md`；`docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`；`docs/coordination/BOARD.md`；上述本地证据包和实现 worktree。
- 允许写入：仅 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-qa-revision-09-result.md`。CLI `-o` 写入本任务 `...-last-message.md`。不得覆写旧 QA-REVISION-08 结果。
- 环境/停止条件：只读本地证据。证据包 SHA 不符、日志/manifest 相互矛盾时标 BLOCKED/FAIL，不尝试修复证据。

## 验收

1. 独立重算证据包 SHA，并从包内逐项读取 exact 与 Linux-normalized manifest/source archive、远端 CTest 原始日志、exit codes、CMakeCache、二进制 SHA 清单；记录实际读取路径和命令。确认 8 个白名单源文件逐文件哈希、CMake source/test 注册以及 archive 版本之间差异范围。不得只引用 05 结论。
2. 独立核对三套 CTest 的测试总数、PASS/FAIL/SKIP 和退出码；解释 exact archive 3 个 CRLF shell-test 失败与 normalized 版本的关系；#180/#181、#183/默认深度安全门和 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 均要在日志中定位。io_uring ON 的 224/224 不得混同默认构建。
3. 检查固定工作树源码和 CMakeCache/二进制摘要是否绑定到所声称的规范化源码。写清无 Git commit 的限制，建议后续如何形成可复现固定输入。
4. 独立静态检查 runtime enable gate：`tree_transfer_client.cpp` 设置 `reliableCandidateMemory`、传入 `effectiveDepthAllowed` 的参数，以及 `lookahead::effectiveDepthAllowed` 条件。明确显式 `--lookahead-depth 1` 是否能在当前实现路径生效；动态单元测试 PASS 不等于 lookahead 已被启动。
5. 给出分轴结论：构建/单测证据、实现正确性、优化是否 effective、深圳—上海实验是否可启动。结论用 PASS/PARTIAL/FAIL/BLOCKED，附命令、退出码/静态证据和限制。不能把本任务范围外未运行的 loopback/cross-domain case 写成通过。

## 执行交接

- 实际证据包 hash/读取路径：
- CTest counts、退出码和证据定位：
- lookahead effective gate：
- 分轴结论与跨域实验放行状态：
- 剩余问题和可直接执行的下一任务建议：
