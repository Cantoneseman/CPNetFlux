# LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10 执行结果

- 任务/路线：`LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10 v1` / `R2026-09-24.9`
- 结果：构建与定向测试通过；规范化源码 + io_uring enabled 的完整 CTest 224/224 通过。保留原始归档与默认配置的失败/skip 记录，交 04 独立复核；本回执不构成 QA 放行。
- 本地仓库 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`（`main`）；执行前后未切分支，主仓库暂存区为空。
- 输入 worktree：`build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`，branch `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`，HEAD `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；index 为空，未提交。
- 执行时间：2026-09-24 UTC；远端快照范围 `07:38:21Z` 至 `08:14:53Z`。SSH 客户端为 `C:\Windows\System32\OpenSSH\ssh.exe`，`OpenSSH_for_Windows_9.5p2, LibreSSL 3.8.2`；BatchMode、StrictHostKeyChecking=yes、ConnectTimeout=12，现有 alias。Git for Windows OpenSSH 在本机启动返回 Win32 access denied（exit `-1073741502`），所以本任务使用系统 OpenSSH；两台 SSH 均成功，命令退出码 `0`。

## 源码快照与完整性

从固定基底执行 `git archive`，随后把源 worktree 的 5 个 tracked 修改逐字节替换并显式复制 3 个 untracked 源文件；不靠 `git diff` 推断或应用 untracked 内容。归档完整源码树共 328 个文件。逐文件清单分别在本机和深圳展开目录校验通过；远端 Python 校验额外忽略其自身运行产生的 11 个 `__pycache__/*.pyc`，除此以外路径、字节数与 SHA-256 完全匹配。`CMakeLists.txt` 中 source 注册位于第 110 行、test 注册位于第 321 行，均已在展开目录核实。

本机证据目录：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-artifacts`。原始可追溯源码归档 SHA-256 为 `28C072007375D113CAC24522B09D9D592277CC4AD08BCF150D6D70FAF02FDD9A`；Linux 执行快照 SHA-256 为 `8F367127D1AC17E41EA5A1B03D1F82FCC1E725F900418526069D3A7E05B5A7B2`。执行快照只把以下 8 个 shell 脚本的 CRLF 转成 LF；源码清单中的 8 项和其字节 hash 未改变：`tools/perf/collect_env.sh`、`tools/perf/run_file_private_once.sh`、`tools/perf/run_private_once.sh`、`tools/perf/sync_remote.sh`、`tools/test/run_file_checksum_private_once.sh`、`tools/test/run_file_checksum_smoke.sh`、`tools/test/run_file_resume_smoke.sh`、`tools/test/run_file_transfer_smoke.sh`。完整文件清单覆盖 328 项。

允许的 8 项源文件与原始 worktree SHA-256：

| 文件 | SHA-256 |
|---|---|
| `CMakeLists.txt` | `FB5637C4AA1BBF6F934C98559B0ACB5D178854FEAE9A1A029BC324F70B96DC5D` |
| `include/cpnetflux/config/tree_transfer_options.h` | `D69065EF65C4445578FA5F238A1497BE5AE34DE82083EF8D6DE4743F39C68C32` |
| `src/config/tree_transfer_options.cpp` | `D0E2B10C03D1E65C56629FFE8160E36C614D092E5694CC00AD87D01D32D1A445` |
| `src/core/io/tree_transfer_client.cpp` | `050A94DE0806CD5B307AF745C19681F26AF81D63E31DD0386A1E8DC2ACEFC1F1` |
| `tests/unit/tree_transfer_options_test.cpp` | `BE6AF91E792398BA985B9523751E4FF3A70C1A7FDAAC8C37FB2291A073691B6A` |
| `include/cpnetflux/core/io/tree_lookahead.h` | `86D9D860F56F5CF73574DB3B4DA22F2505DC18D3E73ABBA5100F3CE8F5A99DB7` |
| `src/core/io/tree_lookahead.cpp` | `40F9994B4BF8E5BF1003A488ADAD722FE655AE806FB34F032C71EB5F8DC08A44` |
| `tests/unit/tree_lookahead_test.cpp` | `EAB18AE7EC9CFBD49211CB0C6929A7D5EE0D89183518DD1A06397E11A4723E50` |

Git status 与任务单列出的 5 个 tracked 源修改和 3 个 untracked 源文件相符；另有 7 个既存 untracked `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl*` 文档，未纳入快照、未修改。源码 worktree HEAD、index 和状态在结束复核时不变。tracked working-tree diff SHA-256：`62DBC2D5753AB41CCB0727BBD044656748CC72AA4667995D47A181491BBF9CE5`。

## 环境、空间与服务

| 主机 / UTC | `/` 与 `/tmp`（同一 ext4 挂载） | inode 可用 | CPU / 内存 / 负载 | 历史树 |
|---|---|---:|---|---|
| 深圳 `iZwz9bgztwf1tic26q48pjZ`，07:38:21 | 总 105,286,258,688；可用 73,581,277,184 bytes | 6,288,387 | 2 CPU；MemAvailable 2,587,824 kB；load 0.00 | HEAD `16b377359494f19f386ba5d375d353449e45f7a0`，porcelain 78 |
| 上海 `iZuf6ja2uqvjzh635cugzmZ`，07:38:25 | 总 105,286,258,688；可用 35,036,577,792 bytes | 6,317,724 | 2 CPU；MemAvailable 3,019,368 kB；load 0.00 | HEAD 同上，porcelain 74 |
| 深圳结束快照，08:21:26 | 可用 72,512,983,040 bytes；inode 可用 6,286,082 | — | MemAvailable 2,949,947,392 bytes；构建/CTest 后无本任务 cmake、ctest、cpnetflux、cc1plus PID | HEAD/count 未变 |
| 上海只读复核，08:12:32 | 可用 35,870,904,320 bytes；inode 可用 6,317,738 | — | MemAvailable 3,025,288 kB；load 0.00 | HEAD/count 未变 |

深圳新运行根 `/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10` 创建前不存在，创建前额外设置 20 GiB 最低可用空间门槛（当时约 68.5 GiB 可用），根及子目录均为 `root:root`、mode `0700`。任务结束后可用约 67.55 GiB，扣除至少 10 GiB 保留仍约 57.55 GiB；本任务没有 payload。最终 run-root `du -sb` 为 1,063,158,846 bytes（约 1.06 GB），最大资源占用远低于门槛。上海只读；没有创建目录或运行 workload。上海快照间可用空间增加 834,326,528 bytes（约 795 MiB），属于本任务之外的盘上变化；未归因或触碰相关文件。

工具链两端均有：GCC/G++ 11.4.0、CMake/CTest 3.22.1、Python 3.10.12、pkg-config 0.29.2、GTest 1.11.0、spdlog 1.9.2、zlib 1.2.11、OpenSSL 3.0.2、liburing 2.0，`/usr/include/liburing.h` 可读。GridFlux 服务监听在深圳 `22310`（PID 2493350）和上海 `2811`（PID 339900）；另有 SSH `22`、vsftpd `21`、resolver `53`。开始与结束监听快照中的既有服务/端口一致；本任务没有占用其端口、停止或改配服务。两端历史 GridFlux-Beta 树均只读且 dirty，未作为源码输入。

revision-09 旧 run-root 仅查属主/mode 与 `/proc` 的 cwd/fd 链接、进程 comm：`root:root 0700`；08:14:53 检查没有进程 cwd/fd 指向该树，也没有匹配的构建/测试进程。未递归、未删除、未修改 revision-09 内容。未进入或触碰 `/root/projects/CPSS(DCC)`、science-compressor 或历史源码树。

## 构建与 CTest

三套构建均使用新 build 目录，无旧 cache 复用。基础参数：`cmake -S <source> -B <new-build> -DCPNETFLUX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo`；工具为 `/usr/bin/c++`、Unix Makefiles、`cmake --build <new-build> --parallel 1`。configure/build 日志和各自 exit 文件都在远端 run-root `logs/`，完整 CMakeCache 和 hash 在 `evidence/`。

| 快照 / 配置 | configure / build | 定向 23 项 | 完整 CTest |
|---|---|---|---|
| 原始精确源码，`CPNETFLUX_ENABLE_IO_URING=OFF` | 0 / 0 | 23 pass、0 fail、exit 0 | 220 pass、3 fail、1 skip，CTest exit 8。失败 #195–197 是归档脚本 CRLF 导致 `set: pipefail\r: invalid option name`；#68 因该 build 选择 io_uring stub 而 skip。原始日志保留。 |
| Linux 行尾规范化源码，io_uring 默认 OFF | 0 / 0 | 23 pass、0 fail、exit 0 | 223 pass、0 fail、1 skip，exit 0；#68 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` skip。skip 未计 pass。 |
| Linux 行尾规范化源码，`CPNETFLUX_ENABLE_IO_URING=ON` | 0 / 0 | 本配置完整 CTest 包含 lookahead 全组并全部通过；专门 #68 smoke 也单独通过（exit 0） | 224 pass、0 fail、0 skip，exit 0。CMake 输出 `CPNetFlux io_uring backend enabled`。 |

默认安全回退由 `TreeLookaheadTest.DepthGateDefaultsOffAndRejectsUnsafeBudget`（#183）动态验证：请求 depth 0 被拒绝启用，`reliableCandidateMemory=false` 时 depth 1 也被拒绝。新增 lookahead 与 options 定向组 23/23 通过；特别是 #180 `ReservationAndHandoffAreSingleUse`、#181 `MetadataOnlyHandoffRequiresOwnerGenerationAndFingerprint` 均通过。

所有 CTest 使用 `--parallel 1`。需要 token 的完整 CTest 通过 `env CPNETFLUX_TEST_TOKEN="$(openssl rand -hex 32)"` 在运行时注入；token 值未写入命令记录、日志或回执。全量原始输出和 exit 文件均保留。

实际执行命令模板（`ROOT=/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10`；各命令的独立 exit code 见远端 `evidence/command-exit-codes.txt`）：

```sh
cmake -S "$ROOT/source/source" -B "$ROOT/build" -DCPNETFLUX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo                 # 0
cmake --build "$ROOT/build" --parallel 1                                                                                  # 0
ctest --test-dir "$ROOT/build" --output-on-failure --parallel 1 --no-tests=error -R '^(TreeLookaheadTest|TreeTransferOptionsTest)\.' # 0
env CPNETFLUX_TEST_TOKEN="$(openssl rand -hex 32)" ctest --test-dir "$ROOT/build" --output-on-failure --parallel 1            # 8: 原始归档行尾脚本失败

cmake -S "$ROOT/source/linux-normalized/source" -B "$ROOT/build-linux-normalized" -DCPNETFLUX_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo # 0
cmake --build "$ROOT/build-linux-normalized" --parallel 1                                                                  # 0
ctest --test-dir "$ROOT/build-linux-normalized" --output-on-failure --parallel 1 --no-tests=error -R '^(TreeLookaheadTest|TreeTransferOptionsTest)\.' # 0
env CPNETFLUX_TEST_TOKEN="$(openssl rand -hex 32)" ctest --test-dir "$ROOT/build-linux-normalized" --output-on-failure --parallel 1 # 0

cmake -S "$ROOT/source/linux-normalized/source" -B "$ROOT/build-io-uring" -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_IO_URING=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo # 0
cmake --build "$ROOT/build-io-uring" --parallel 1                                                                        # 0
ctest --test-dir "$ROOT/build-io-uring" --output-on-failure --parallel 1 --no-tests=error -R '^FileIoTest\.IoUringContextReadWriteSmokeWhenAvailable$' # 0
env CPNETFLUX_TEST_TOKEN="$(openssl rand -hex 32)" ctest --test-dir "$ROOT/build-io-uring" --output-on-failure --parallel 1   # 0
```

关键二进制 SHA-256：默认 OFF build 的 `cpnetflux_unit_tests` 为 `1C1281EC90145A85C43828A180B365565E5A1342813B7D2FB86C1C96E1F1B58B7`；io_uring ON build 的 `cpnetflux_unit_tests` 为 `8D80CD7B8F7F89881DFEBE986CEB375436C4A809B1C757BA0D986642B53C2A43`。两套所有 executable 的 SHA-256 清单、cache/hash 与 CTest 日志都在本地证据包及远端 run-root。

## 证据与交接

- 远端完整回执根：`/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10/`，包含 `source/`、`build/`、`build-linux-normalized/`、`build-io-uring/`、`logs/`、`evidence/`；未清理。
- 本地证据索引：`build/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-artifacts/evidence-index.json`。
- 本地证据包：`build/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-artifacts/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-evidence.tar.gz`；SHA-256 见 `evidence-index.json`。其中含精确/规范化源码归档、328 文件 manifest、工作树 diff、远端日志/exit、cache、依赖/空间/服务、二进制 SHA 和上海只读快照。
- 远端证据 tar SHA-256：`8EB838E3D81F88D16280510399AF1EAC07CDF8ADC4E9EC1D25497CC6AF20972B`；回收后本地 hash 相同。
- 未运行跨域性能矩阵、GridFTP 对照或跨服务器 payload 实验；完整 CTest 中本机 loopback smoke 按测试夹具运行。没有清理、安装软件、停止/修改服务、修改实现或提交 Git。
- 交 04：独立读取回收的源码清单、CTest 原始日志与 CMakeCache，重点复核 #180/#181、#183 默认回退、归档行尾处理和 io_uring ON 全绿结果。04 通过前保持后续性能实验不可运行。

## 2026-09-24 本轮续验（只读）

本轮于 10:30–10:37 UTC 接手时，任务回执、证据包和深圳 revision-10 run-root 已有此前执行结果。revision-10 根已含 `source/`、三套 `build/`、日志、证据和归档；属主 `root:root`、mode `0700`，本轮观测占用约 1,067,974,656 bytes。revision-09 根也仍在，未改动。revision-09 与 revision-10 根的 `fuser -v` 均无使用者（退出码 1）；按 `/proc` cwd/fd 链接复核，未发现进程引用两个任务根。为避免覆盖已有产物，本轮没有再次配置、构建或运行 CTest；以下为本轮新鲜只读核验，不冒充新一轮 build。

SSH 客户端记录：`D:\Software\Git\usr\bin\ssh.exe -V` 和两 alias 连接都在客户端启动阶段失败，Win32 error 5，退出 `-1073741502`。随后使用 `C:\Windows\System32\OpenSSH\ssh.exe` 9.5p2 / LibreSSL 3.8.2、用户既有 config、`BatchMode=yes`、`StrictHostKeyChecking=yes`、`ConnectTimeout=12`；两端远程快照命令退出均为 0。没有放宽 host-key 校验或打印 SSH 配置/凭据。

- 深圳 `iZwz9bgztwf1tic26q48pjZ`，2026-09-24 10:30:54 UTC：`/` 与 `/tmp` 同为 ext4 `/dev/nvme0n1p3`，总量 105,286,258,688、可用 72,508,588,032 bytes；inode 可用 6,285,872；2 CPU、MemAvailable 2,916,868 kB、load 0.00。保留 10 GiB 并另留 6 GiB 构建/证据峰值预算后，余量约 51.5 GiB。监听仍有 sshd `22`、vsftpd `21`、GridFTP `22310`/PID 2493350；另见 loopback code 服务端口 `39395`/PID 5893、`33399`/PID 3447817。任务未占用端口，也未接触这些进程。
- 上海 `iZuf6ja2uqvjzh635cugzmZ`，2026-09-24 10:30:55 UTC：`/` 与 `/tmp` 同为 ext4 `/dev/nvme0n1p3`，总量 105,286,258,688、可用 35,874,189,312 bytes；inode 可用 6,318,141；2 CPU、MemAvailable 3,037,272 kB、load 0.00。监听仍有 sshd `22`、vsftpd `21`、GridFTP `2811`/PID 339900。上海仅为只读快照。
- 本轮实际读到的深圳/上海依赖版本仍为 GCC/G++ 11.4.0、CMake 3.22.1、Python 3.10.12、GTest 1.11.0、spdlog 1.9.2、zlib 1.2.11、OpenSSL 3.0.2、liburing 2.0。

本地 `evidence-index.json` SHA-256 为 `5DE76BB1FD49D09E2C8C74DEC69B6A2D8A6C7128A25A55AE741125047CAEF392`；完整证据包 `LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-evidence.tar.gz` SHA-256 为 `2AF5C2DC166615FD4F0924BA335408624E8620B437B07B3528A98E08674A07C6`，本轮 `Get-FileHash` 与索引一致。回收的远端证据 tar 为 `8EB838E3D81F88D16280510399AF1EAC07CDF8ADC4E9EC1D25497CC6AF20972B`，与深圳端实读 hash 一致。精确源码归档仍为 `28C072007375D113CAC24522B09D9D592277CC4AD08BCF150D6D70FAF02FDD9A`，Linux 行尾规范化归档为 `8F367127D1AC17E41EA5A1B03D1F82FCC1E725F900418526069D3A7E05B5A7B2`。当前 worktree 的 8 个指定文件 SHA 与 manifest 一致；深圳展开树 8 项逐文件 hash 一致，328 文件全树验证通过，`CMakeLists.txt` source/test 注册分别在第 110/321 行。

证据包内原始日志复核：定向 23/23 通过，#180、#181 均通过；精确源码完整 CTest 为 220 pass、3 fail、1 skip（3 项失败是 CRLF shell wrapper 解析失败，io_uring smoke 因默认关闭而 skip）；行尾规范化默认构建为 223 pass、0 fail、1 skip；单独 `CPNETFLUX_ENABLE_IO_URING=ON` 构建为 224 pass、0 fail、0 skip，io_uring smoke 单项通过。skip 未计为 pass。此结论沿用已存日志与独立复核，不表示本轮重跑，也不构成 04 QA 放行。

本轮没有写入云端、没有启动/停止服务或进程、没有清理、安装、性能矩阵、GridFTP 对照或传输；本地只追加本结果和 last-message，未改代码、切分支或操作暂存区。