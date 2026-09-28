# LOWLEVEL-TREE-LOOKAHEAD-QA-REVISION-09 独立复核

- 路线/任务：`R2026-09-24.9 / QA-REVISION-09 v1`
- 审查结论：源码内容与构建证据可核对；规范化默认构建的完整 CTest 通过（另有一个 skip），但 lookahead 在产物里被生产预算门永久关闭。因此**lookahead 性能实验 BLOCKED**。
- 主资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；实现 worktree HEAD：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。实现修改是未提交工作树快照，不属于该 HEAD。
- 本任务只读取证据；没有构建、运行 CTest、SSH、传输、性能实验、清理、提交或改动实现 worktree/index。

## 结果摘要

| 审查轴 | 结论 | 依据 |
|---|---|---|
| 证据包和源文件内容 | PASS（内容级） | 外层 tar SHA-256 与任务一致；两个 328 文件 archive 均逐项匹配各自 manifest；工作树 diff hash 和 8 个实现源文件均与包中快照匹配。远端原始 tar 容器 SHA 与本地 exact tar 不同，详见限制。 |
| Linux build / CTest | PARTIAL（按配置分别判定） | exact 快照 configure/build、定向 23/23 成功，但全套 220 pass、3 fail、1 skip，exit 8；LF-normalized 默认配置全套 223 pass、1 skip，exit 0；LF-normalized io_uring ON 全套 224/224，exit 0。 |
| 单测证据 | PASS（仅测试所覆盖的状态逻辑） | #180、#181、#183 在三份 full CTest 日志均通过；默认 LF-normalized #68 被 skip，io_uring ON 的 #68 通过。 |
| 实际启用与实现集成 | FAIL（lookahead 当前不可启用） | 生产代码无条件令 `reliableCandidateMemory=false`，随后将其传入 `effectiveDepthAllowed`；该函数对 false 直接拒绝。显式 `--lookahead-depth 1` 最终仍得到 effective depth 0。 |
| 深圳—上海 lookahead 性能验证 | BLOCKED | 此二进制不会启动候选 lookahead；跑跨域矩阵不能测出该功能。且本任务没有核查实时资源或授权实验。 |

这不是代码或产品正确性总验收：构建/测试证据由 05 生成，本轮只是独立读取、重算和交叉核对。loopback smoke 仅作为证据包全套 CTest 的日志内容；本轮没有启动任何 smoke 或实验。

## 证据包与源码快照

外层包 `build/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-artifacts/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-evidence.tar.gz` 实测为 1,878,966 bytes，SHA-256 `2AF5C2DC166615FD4F0924BA335408624E8620B437B07B3528A98E08674A07C6`，与任务和 `evidence-index.json` 一致。只读 `tar -tzf` 列出 91 个成员；使用 Python `tarfile` 在内存读取，没有解包或写文件。索引内的远端 evidence tar 为 34,642 bytes、SHA-256 `8EB838E3D81F88D16280510399AF1EAC07CDF8ADC4E9EC1D25497CC6AF20972B`。

独立重算两个内嵌源码归档以及完整 manifest：

| 快照 | 归档 SHA-256 | 文件数 | archive 与对应 manifest |
|---|---|---:|---|
| exact | `28C072007375D113CAC24522B09D9D592277CC4AD08BCF150D6D70FAF02FDD9A` | 328 | 328/328 路径、字节数和 SHA 一致 |
| Linux-normalized | `8F367127D1AC17E41EA5A1B03D1F82FCC1E725F900418526069D3A7E05B5A7B2` | 328 | 328/328 路径、字节数和 SHA 一致 |

exact 与 normalized archive 展开后只有任务所列 8 个 shell 脚本存在字节差异。逐个确认 normalized 文件等于 exact 内容将 CRLF 换成 LF；减少的字节数正好等于 CRLF 行数。其余 320 个文件完全相同，尤其 8 个 lookahead/CMake/options C/C++ 源与测试内容不变。内嵌远端清单 `exact-tree-verify.txt` 记录 `verified_files=328 all_paths_bytes_sha256_match=true`；`linux-normalized-tree-verify.txt` 也记录 328 项匹配。远端 `source-remote-sha256.txt` 的 8 项与本地 modified-source manifest 一致。

我再次直接对实现 worktree 的 8 个白名单路径计算字节长度和 SHA-256；均与 exact archive、normalized archive 及 manifest 相符：

| 白名单文件 | 字节数 | SHA-256 |
|---|---:|---|
| `CMakeLists.txt` | 14,971 | `FB5637C4AA1BBF6F934C98559B0ACB5D178854FEAE9A1A029BC324F70B96DC5D` |
| `include/cpnetflux/config/tree_transfer_options.h` | 2,792 | `D69065EF65C4445578FA5F238A1497BE5AE34DE82083EF8D6DE4743F39C68C32` |
| `src/config/tree_transfer_options.cpp` | 21,013 | `D0E2B10C03D1E65C56629FFE8160E36C614D092E5694CC00AD87D01D32D1A445` |
| `src/core/io/tree_transfer_client.cpp` | 155,521 | `050A94DE0806CD5B307AF745C19681F26AF81D63E31DD0386A1E8DC2ACEFC1F1` |
| `tests/unit/tree_transfer_options_test.cpp` | 20,531 | `BE6AF91E792398BA985B9523751E4FF3A70C1A7FDAAC8C37FB2291A073691B6A` |
| `include/cpnetflux/core/io/tree_lookahead.h` | 5,785 | `86D9D860F56F5CF73574DB3B4DA22F2505DC18D3E73ABBA5100F3CE8F5A99DB7` |
| `src/core/io/tree_lookahead.cpp` | 16,399 | `40F9994B4BF8E5BF1003A488ADAD722FE655AE806FB34F032C71EB5F8DC08A44` |
| `tests/unit/tree_lookahead_test.cpp` | 19,673 | `EAB18AE7EC9CFBD49211CB0C6929A7D5EE0D89183518DD1A06397E11A4723E50` |

快照 manifest 声明基底/HEAD 都是 `3b0820d...`，并明确包含 5 个 tracked 修改及 3 个 untracked 源文件；其余 7 个 untracked `docs/tasks/` 文件未进源码快照。当前 worktree 的 tracked diff 为 63,872 bytes，SHA-256 `62DBC2D5753AB41CCB0727BBD044656748CC72AA4667995D47A181491BBF9CE5`；本轮从 worktree 重新取 `git diff --binary` 得到相同长度和 hash。HEAD/index/status 与派单时的源码范围一致，index 为空。

复核发现一个**归档容器字节指纹差异**：包内 `evidence/archive-remote-sha256.txt` 对远端 `source/source-exact-worktree.tar.gz` 记为 `9cba0fc78d9824c5eec8efaeb7c7d7673d5681930f6dce1127784fa6dd4c103f`；本地 canonical exact tar 是 `28c07200...`。远端归档原始 tar 本体不在证据包，故不能把两者说成字节级相同，也不能独立确定差异来自何种打包元数据。远端完整展开目录的 328 项验证和 8 个源文件哈希均与 canonical 内容相符，因此我判定**展开源码内容得到强交叉证实，但远端 tar 容器字节同一性未证**，不将其扩大为源代码不匹配。

## 构建与 CTest 复核

证据均在外层包 `remote/revision-10-remote-evidence.tar.gz` 的所列路径中；表内 configure/build/CTest 退出码由 `evidence/command-exit-codes.txt` 读取，并与日志末尾汇总交叉核对。本轮没有重放这些命令。

| 构建源码 / 配置 | configure / build | 定向 CTest（TreeLookahead + TreeTransferOptions） | 全套 CTest |
|---|---|---|---|
| exact / `CPNETFLUX_ENABLE_IO_URING=OFF` | 0 / 0 | 23/23 pass，exit 0，`logs/targeted-ctest.log` | 220 pass、3 fail、1 skip，exit 8，`logs/full-ctest.log` |
| LF-normalized / io_uring OFF | 0 / 0 | 23/23 pass，exit 0，`logs/normalized-targeted-ctest.log` | 223 pass、0 fail、1 skip，exit 0，`logs/normalized-full-ctest.log` |
| LF-normalized / io_uring ON | 0 / 0 | full suite 中 lookahead/options 组通过 | 224 pass、0 fail、0 skip，exit 0，`logs/io-uring-full-ctest.log`；单独 #68 smoke 1/1，exit 0，`logs/io-uring-smoke-ctest.log` |

三套均为 CTest 总计 224 项。exact 日志第 392–404 行显示仅 #195 `cpnetflux_file_transfer_smoke`、#196 `cpnetflux_file_resume_smoke`、#197 `cpnetflux_file_checksum_smoke` 失败，均在脚本第二行报 `set: pipefail\r: invalid option name`；这与 CRLF shell 脚本被 `/bin/sh` 读取相符。只规范化 8 个 shell 文件行尾后，这三项分别在 normalized 日志第 392–396 行通过；这不是对 C/C++ 源码的改写。exact 和 normalized 默认 OFF 日志的 #68 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 均为 `Skipped`（日志第 138 行）；不能把 skip 计作通过。io_uring ON 的 #68 在其完整日志第 138 行及专门 1 项日志中通过，**不能把 224/224 套用到默认 OFF 配置**。

三份 full 日志里 #180 `ReservationAndHandoffAreSingleUse`、#181 `MetadataOnlyHandoffRequiresOwnerGenerationAndFingerprint`、#183 `DepthGateDefaultsOffAndRejectsUnsafeBudget` 均显示 Passed（日志第 361–368 行）。CTest 验证这些测试所表达的逻辑，但它们并不证明 production path 能启用 lookahead。

Cache 与构建产物：三个 `CMakeCache.txt` 均为 `RelWithDebInfo`、`/usr/bin/c++`、Unix Makefiles、`CPNETFLUX_BUILD_TESTS=ON`。exact OFF cache SHA `c752faf754878ff268dcea72df14484e49f8e45a8165107a767b7eee497ba0a5`；normalized OFF cache SHA `4a39118773959e8530fbc699163b4ccd600fdef46b135d6259a11b1b5c4fc6df`；io_uring ON cache SHA `603dcf21144417418ed65acbcffb6280581bafb08b901b568ac01b4da9714aeb`。`evidence/cmake-source-registration.txt` 确认注册 `src/core/io/tree_lookahead.cpp`，`cmake-test-registration.txt` 确认注册 `tests/unit/tree_lookahead_test.cpp`；实现 worktree `CMakeLists.txt` 行 110、321 对应相同注册。

二进制清单 `evidence/binary-sha256.txt` / `key-binaries-sha256.txt` 给出 normalized OFF 的 unit test 二进制 SHA `1c1281ec90145a85c43828a180b365565e5a1342813b7d2fb86c1c96e1f1b58b`、`cpnetflux` SHA `bda6b6a5164d2b0d3b1e6d5e9514bd9414679b8fb45df364cd86dfae99884bae`；`all-binaries-sha256.txt` 给出 io_uring ON 的 unit test 二进制 SHA `8d80cd7b8f7f89881dfebe986ceb375436c4a809b1c757ba0d986642b53c2a43`。清单没有单独列 exact OFF build 的二进制 hash，故该 variant 的 build/CTest 日志可读，但不能从现存二进制 SHA 清单独立绑定其测试可执行文件。

## 运行时启用门与实验结论

固定实现 worktree 的 `src/core/io/tree_transfer_client.cpp:3091-3099` 明确先将 `state.reservations.budget.reliableCandidateMemory = false`，再以 `options.lookaheadDepth`、`globalScheduler=false`、`workerCount` 和该 budget 调用 `lookahead::effectiveDepthAllowed`，不允许时将 `effectiveLookaheadDepth` 设为 0。`src/core/io/tree_lookahead.cpp:146-155` 要求请求 depth 恰为 1 且 `reliableCandidateMemory`、FD limit、worker 等条件全为安全值；其中 memory flag 为 false 时直接返回 false。候选 reserve 还会检查 effective depth，因此后续显式 `--lookahead-depth 1` 不能越过该门。默认 depth=0 也保持关闭。

因此本构建里新增代码和单测可被编译、helper/state machine 可由测试调用，但**目录调度中的 lookahead candidate/preparation/handoff 不可能因 CLI opt-in 而运行**。#183 的 PASS 正在验证这条 fail-closed 行为；它不是 lookahead 已生效的证据。没有 `effectiveLookaheadDepth==1` 的 production 观测或 fresh transfer smoke。

结论分开记录：

- **源码快照/构建证据：PASS（内容级）**；归档容器指纹、exact variant binary hash 有上述追溯限制。
- **CTest 证据：PARTIAL**；LF-normalized OFF 完整测试 223 pass + 1 skip，io_uring ON 为独立 224/224；exact OFF 的 3 个脚本失败由 CRLF 解释但仍是失败记录。
- **lookahead 生产集成：PARTIAL**；helper 单测已由 Linux 跑过，集成执行路径被安全预算门关闭，尚无活动候选传输证据。
- **lookahead 有效性/性能：BLOCKED**；当前二进制不可能启用该优化。不得将构建通过、#180/#181/#183 通过、loopback smoke 或历史 dense 结果外推成 lookahead 性能结论。
- **深圳—上海实验：BLOCKED**；在生产启用门得到有证据的安全实现并形成固定输入前，不应安排 lookahead A/B。此轮未 SSH，因此也未复查此刻资源余量；OPS-REVISION-10 的历史资源快照不代表当前准入。

下一步应先由实现角色处理并验证 `reliableCandidateMemory` 的安全门：给出可审计的受限内存预算或保持 depth=0 并取消性能验证目标；随后将已审查的代码固定为 Git commit（或发布内容寻址源码 archive + 完整 manifest），生成可绑定 exact source、CMakeCache 和每个二进制 SHA 的 Linux 证据。仅当一个真实 transfer 能证明请求 depth=1 后 effective depth 为 1、候选实际进入并且 on/off 的 hash、wire/frame、resume/changed 等价门通过，才另行讨论跨域 A/B。此建议不授权代码修改或实验。

## 实际命令与门禁

本轮本地检查命令及终态：

- `git rev-parse HEAD`：exit 0，`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`git diff --cached --name-only`：exit 0，空。
- `git -C <impl-worktree> rev-parse HEAD`、`status --short --branch`、`diff --cached --name-only`：exit 0；HEAD 为 `3b0820d...`，index 空，源码范围为 5 个 tracked 修改和 3 个 untracked 源文件。
- `Get-FileHash -Algorithm SHA256 <evidence.tar.gz>`、`Get-Item <evidence.tar.gz>`：exit 0，hash/长度如上。
- `tar -tzf <evidence.tar.gz>`：exit 0；Python `tarfile` 在内存对 exact/normalized 328 项 manifest、8 个本机源文件、shell 行尾差异和嵌入日志/cache 清单进行复算：exit 0，核对结果如上。
- `git -C <impl-worktree> diff --binary` 的只读哈希复算：exit 0，63,872 bytes，SHA 与 snapshot manifest 相同。
- `rg` 定向复核 CMake 注册及 `reliableCandidateMemory/effectiveDepthAllowed`：exit 0；定位见上文。
- 一次只读 Python 查询误用了内层 tar 路径前缀，返回 `KeyError`/exit 1；按包内实际 `build/CMakeCache.txt` 等路径更正后读取 exit 0，已在上文记录实际 cache hash。这不是构建/证据日志失败。
- 写后执行：`git diff --check`、UTF-8/LF/尾随空白检查、HEAD/index/source hash/status 复核；结果填入 CLI 摘要文件。

输入与输出：证据包 `2AF5C2DC...` 在开始和写后校验保持一致；允许输出仅为本结果及 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-qa-revision-09-last-message.md`。动态构建/CTest/传输/性能实验在本 QA 中均 NOT_RUN；仅复核 05 留存的 Linux 记录。
