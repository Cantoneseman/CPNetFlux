# LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10：完整导出并验收深圳固定构建

- 状态：ready
- 路线版本、任务版本：`R2026-09-24.9`，OPS-REVISION-10 v1
- 发起人：00 总指挥；执行角色：05 云端运维与发布；验收角色：04 测试与质量（独立动态复核），00 验收运维证据
- 目标及理由：修复 revision-09 源码归档漏掉 `CMakeLists.txt` 注册差异的问题；为 revision-08 lookahead 实现产生可逐文件追溯的完整源码快照、深圳 Linux 构建、定向及完整 CTest 证据，使 04 可进行动态验收。
- 非目标：不运行跨域性能矩阵、GridFTP 对照或 payload 传输；不修改实现、不改远端历史源码、不清理 revision-09 遗留目录、不删除/停止任何既有进程或服务，不碰 `/root/projects/CPSS(DCC)`，不安装软件，不提交 Git。
- 输入 commit（完整 SHA）、工作树状态：基底 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；源 worktree `D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`，分支 `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`，HEAD 为基底提交、index 空。允许快照包括以下 5 个 tracked 修改：`CMakeLists.txt`、`include/cpnetflux/config/tree_transfer_options.h`、`src/config/tree_transfer_options.cpp`、`src/core/io/tree_transfer_client.cpp`、`tests/unit/tree_transfer_options_test.cpp`；以及 3 个 untracked 源文件：`include/cpnetflux/core/io/tree_lookahead.h`、`src/core/io/tree_lookahead.cpp`、`tests/unit/tree_lookahead_test.cpp`。必须记录并核对全部文件，不得只核对 tree_lookahead.cpp。目标源 SHA-256：`src/core/io/tree_lookahead.cpp=40F9994B4BF8E5BF1003A488ADAD722FE655AE806FB34F032C71EB5F8DC08A44`。
- 必读资料和证据路径：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-revision-09-last-message.md`、`docs/tasks/2026-09-24-lowlevel-tree-lookahead-qa-revision-08-result.md`、`docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-08.md`、`docs/coordination/ENVIRONMENT.md`。
- 允许修改的文件/目录；禁止修改的资源：只允许本地写本任务 result/last-message 与本任务新证据索引；云端只允许在新隔离根 `/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10/` 创建源码/build/log/evidence。禁止修改 revision-09 及 revision-08 旧 run roots，禁止覆盖 `/root/projects/GridFlux-Beta` 或进入 `/root/projects/CPSS(DCC)`。
- 工作分支/worktree：不改 Git 分支。源快照必须来自上列 worktree 的 HEAD + 全部 tracked diff + 三个 untracked 源文件。建议使用 `git archive` 基底、`git diff --binary HEAD` 补丁和显式复制 untracked 文件，随后生成逐文件 SHA-256 manifest 与最终 archive SHA-256；应用补丁后必须检查 `CMakeLists.txt` 的 source/test 注册确实存在。禁止复用 revision-09 不完整 archive/build。
- 前置条件、环境占用和停止条件：先只读复核两端资源、监听服务、revision-09 runroot 相关进程/占用及 SSH alias；若发现仍活动的本任务进程，不并发启动新 build、不杀进程，报告并停止；不因上次失效 archive 操作去清理其文件。新根创建前重验空间预算（至少 10 GiB 保留加 source/build/log 预算）。只在深圳构建；上海只读资源复核。出现运行中未知 PID、空间不足、hash 不符、patch 漏项、源码注册不全立即停止。
- 验收标准及实际可执行命令：
  1. 本地记录 worktree HEAD/status/index、tracked diff path、3 个 untracked 文件及完整逐文件 SHA-256；归档树复核 8 项修改全部存在，`CMakeLists.txt` 注册 lookahead source 与 test；记录 archive SHA-256。
  2. SSH 后核对新隔离根归属/空间；上传并展开后逐文件 SHA-256 与本地 manifest 完全一致，目标 `tree_lookahead.cpp` SHA 必须匹配上述值。
  3. 使用明确 CMake configure 参数生成 Linux build；记录 CMake cache、compiler、依赖、binary SHA-256。不能复用旧 build cache。
  4. 运行 `ctest --test-dir <new-build> --output-on-failure --parallel 1 --no-tests=error -R "^(TreeLookaheadTest|TreeTransferOptionsTest)\\."`，23 项应全部通过，特别核对旧 #180/#181；然后运行完整 `ctest --test-dir <new-build> --output-on-failure --parallel 1`，PASS/FAIL/SKIP 分列，skip 不计 pass。token 只通过受控进程环境注入，不写命令日志或回执。
  5. 保存 configure/build/CTest 原始日志、源/二进制 hash、环境、磁盘前后、PID/service 前后状态；不得启动性能实验。
- 云端运行目录、端口、资源上限、清理与归档方案：新隔离根 `/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10/`；本任务不分配服务端口、不生成 payload、不执行传输、不清理该任务证据。完成后 04 从本地回收证据独立复核。
- 预期产物路径：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-revision-10-result.md`、`...-last-message.md`；本地完整证据归档路径写入回执。

## 派给角色的消息

先读本任务、revision-09 last-message 和 revision-08 QA result。先只读检查旧 revision-09 runroot 是否还有活跃本任务进程，再决定是否启动新根；不停止任何 PID。请可靠导出基底 + 全部 5 个 tracked 修改 + 3 个 untracked 源文件，逐文件校验，重点确认 CMakeLists source/test 注册。revision-09 已证明漏用 git diff 时会构建错误旧源码；不能复用旧 archive/build。只有 hash、空间、进程门禁通过才构建。深圳运行定向 23 项和完整 CTest，上海只读快照。不得跑性能实验、不得清理旧产物。所有真实结果写回执，并明确 PASS/FAIL/SKIP 和阻塞；测试未全部通过时不要宣称 QA 放行。

## 执行回执

- 实际输入/输出 commit 与 worktree 状态：
- 实际源码快照路径、8 项差异清单、逐文件/archive SHA-256：
- 实际环境、挂载点预算、进程/端口和新隔离根：
- 实际命令、退出码和证据路径：
- 定向/完整 CTest PASS、FAIL、SKIP：
- 失败/跳过/阻塞及原因：
- 是否启动性能实验：必须为 NO。
- 下一角色可直接执行的下一步：交 04 独立核验动态测试证据；04 通过前保持深圳—上海性能实验不可运行。

## 验收与路线变化

验收人、结论和依据：

若路线改变：记录替代任务、停止点和保留证据；不覆盖之前的构建/实验结果。
