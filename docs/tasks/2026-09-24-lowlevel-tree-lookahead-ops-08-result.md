# LOWLEVEL-TREE-LOOKAHEAD-OPS-08 执行回执

- 状态：**BLOCKED**（隔离构建成功；定向与全量测试均有 2 项失败，io_uring 用例跳过）
- 执行时间：2026-09-24，约 02:05–02:34 UTC（深圳/上海主机本地时间 UTC+8）
- 固定源码基线：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- 输入 worktree：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`
- 本机文档仓库 HEAD：开始与结束均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`

## 结论

深圳、上海在本次采样时均满足“10 GiB 保留空间 + 本任务预算”的磁盘门槛。源码快照完整、可校验，深圳隔离构建和 CTest 已执行，构建返回 0。最终定向测试 31 项中 29 通过、2 失败；全量 CTest 224 项中 221 通过、2 失败、1 跳过。因此不能验收为测试通过，也不能据此批准性能矩阵或生产 lookahead。失败均在 `TreeLookaheadTest` 的 handoff 单次消费/元数据校验行为。io_uring 依赖文件存在，但运行时用例明确跳过，不能算通过。

## 工作树和源码工件

实现 worktree 分支为 `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`，HEAD 与固定基线一致，index 为空，`git diff --check` 返回 0。开始执行时 porcelain 共 14 条：5 个 tracked modified、9 个 non-ignored untracked；tracked 差异 984 insertions、40 deletions。完整路径状态在工件中的 `status.porcelain0`，完整二进制 diff 在 `working-tree.diff`。本任务没有修改实现 worktree、main 或共享 index，也没有提交。

本机保存的可追溯工件位于 `build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/`：

| 工件 | 大小 | SHA-256 |
|---|---:|---|
| `canonical/source-tree-git-normalized.tar.gz` | 599,987 B | `ce1bac7bffda4de3c8174dc05a6b6104c255eef2e796dddb4ce4ceb821730791` |
| `working-tree.diff` | 63,872 B | `62dbc2d5753ab41ccb0727bbd044656748cc72aa4667995d47a181491bbf9ce5` |
| `status.porcelain0` | 769 B | `8855fb15e527432a3d41ba699564a05db3d05d4e4ff76ddd5b7f3cab58ecd4a6` |
| `canonical/manifest.json` | — | `AAD07B782087248C89A24D8F3199E1061C1466394B39577928BF59C42A2C9908` |

`source-tree-git-normalized.tar.gz` 由固定 HEAD 的 `git archive`、完整 `git diff --binary HEAD` 和 9 个未跟踪文件在独立 staging 树重建，未从云端 dirty 历史目录取源码。Linux 执行前，隔离副本中的 8 个 `.sh` 文件从 CRLF 转为 LF，以避免 Windows 行尾破坏 Linux shell 测试；C/C++ 文件和 worktree 未变。实际构建输入归档 `canonical/execution-snapshot/source-linux-normalized.tar.gz` 为 605,362 B，SHA-256 `40387b2f1289b95a378c3afafad2e97f6d58dd65df11c618827d9844b53b7c99`；逐文件前后 hash 记录在 `canonical/evidence-final/execution-evidence/line-ending-normalization.json`，执行源清单在 `execution-source-manifest.json`。原始完整快照仍单独保留，二者没有混称为字节完全相同。

## SSH、主机资源与服务

Windows Git OpenSSH 客户端 `D:\Software\Git\usr\bin\ssh.exe` 启动失败（Win32 access denied，进程状态 `-1073741502`）。后续使用系统 `C:\Windows\System32\OpenSSH\ssh.exe`，版本 `OpenSSH_for_Windows_9.5p2, LibreSSL 3.8.2`，通过现有用户配置和原有 alias 访问，启用 `BatchMode=yes`、`StrictHostKeyChecking=yes` 与连接超时。未输出 SSH 配置、凭据或 token。此前续接 CLI 尝试 `codex exec resume` 退出 1，错误为状态库只读及 app-server `Access denied (os error 5)`；随后实际运维仍由当前会话完成。SCP 使用系统 OpenSSH 对应客户端及同样的 host-key/BatchMode 限制，归档部署和证据回收均退出 0。

每端均以只读 SSH 执行并退出 0 的查询组：`hostname`、UTC `date`、`df -B1 -T / /tmp`、`df -Pi / /tmp`、`nproc`、`free -b`、`uptime`、编译器/CMake/CTest/Python/pkg-config 版本、`pkg-config --modversion gtest spdlog zlib openssl liburing`、`test -r /usr/include/liburing.h`、`ss -H -ltnp`、有限 `ps -o pid,ppid,user,comm,lstart` 与指定 GridFlux-Beta HEAD/porcelain 检查。归档上传 SCP 和最终日志/hash 回收 SCP 均退出 0。没有读取 cmdline、environ 或凭据。

两台主机的 `/` 和 `/tmp` 均是同一块 `/dev/nvme0n1p3` ext4，不能将两行空间相加。单位均为字节；时间为 UTC。`/` 与 `/tmp` 的 inode 采样一致。

| 主机 | 采样 | `/`、`/tmp` 总量 / 已用 / 可用 | inode 已用 / 可用 | CPU / 内存可用 / swap |
|---|---|---|---|---|
| 深圳 `iZwz9bgztwf1tic26q48pjZ` | 02:05:52Z 前置 | 105,286,258,688 / 26,991,099,904 / 73,782,505,472 | 241,097 / 6,291,527 | 2 vCPU / 2,895,676 KiB / 无 |
| 深圳 | 02:33:00Z 后置 | 105,286,258,688 / 27,128,524,800 / 73,645,080,576 | 243,320 / 6,289,304 | 2 vCPU / 2,883,536 KiB / 无 |
| 上海 `iZuf6ja2uqvjzh635cugzmZ` | 02:05:54Z 前置 | 105,286,258,688 / 64,902,103,040 / 35,871,502,336 | 214,885 / 6,317,739 | 2 vCPU / 3,039,604 KiB / 无 |
| 上海 | 02:33:00Z 后置 | 105,286,258,688 / 64,902,152,192 / 35,871,453,184 | 214,885 / 6,317,739 | 2 vCPU / 3,030,072 KiB / 无 |

前置及后置 CPU/内存/磁盘查询均为只读。容量门槛按每个实际挂载点计算：10 GiB 保留 + 8 GiB 本任务 envelope（archive 2 GiB、build 4 GiB、logs/evidence 1 GiB、瞬时峰值余量 1 GiB），共 `19,327,352,832 B`。该 8 GiB 是保守预算上限，不是实测写入量；本次没有性能 payload。深圳后置超过门槛 `54,317,727,744 B`，上海后置超过 `16,544,100,352 B`。上海虽通过此次有界构建预算，后续任务仍须重新采样。深圳 task run root 后置实占 `132,853,205 B`；构建期间未触及 10 GiB 保留线。

两端工具/依赖只读核查结果一致：GCC/G++ 11.4.0、CMake/CTest 3.22.1、Ninja 1.10.1（未用）、Python 3.10.12、pkg-config 0.29.2、GTest 1.11.0、spdlog 1.9.2、zlib 1.2.11、OpenSSL 3.0.2、liburing 2.0 且 `/usr/include/liburing.h` 可读。没有安装软件。实际构建由 CMake 选择 `/usr/bin/c++`、Unix Makefiles；仅设置 `CPNETFLUX_BUILD_TESTS=ON`，未设置 `CMAKE_BUILD_TYPE`。

前后 `ss -H -ltnp` 检查保留了既有服务，未停止或改配：

- 深圳：sshd `22` PID 3747193、vsftpd `21` PID 794、GridFTP 控制端口 `22310` PID 2493350（root，`globus-gridftp-`，服务归属仍未完全确认）；另有本机 resolver `53` 与 IDE localhost listener。该 GridFTP PID/listener 未触碰。
- 上海：sshd `22` PID 302699、vsftpd `21` PID 788、GridFTP 控制端口 `2811` PID 339900（root，systemd cgroup `/system.slice/gridflux-gridftp-gsi.service`；服务 active、描述为使用 GSI auth 的 GridFlux experimental GridFTP server）；另有 resolver `53`。该服务和 listener 未触碰。
- `/tmp/cpnetflux-runs` 前置在深圳不存在；本任务仅在深圳建立并使用 `/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-08`，属 root、mode 0700。上海没有创建运行目录。完成后未发现本任务残留 PID；原有服务 PID 与端口仍在。
- 两端 `/root/projects/GridFlux-Beta` 始终只读：HEAD 均为 `16b377359494f19f386ba5d375d353449e45f7a0`；porcelain 条数深圳 78、上海 74（后置复核相同）。从未用其源码构建。完全跳过 `/root/projects/CPSS(DCC)`，未访问其内容。

## 构建命令和退出码

构建仅在深圳隔离 run root 中进行，源和构建分离。变量 `<SRC>`、`<BUILD>` 分别指向下列实际目录；命令、退出码和 stdout/stderr 完整日志均保存在后列工件目录。

```sh
cmake -S <SRC> -B <BUILD> -DCPNETFLUX_BUILD_TESTS=ON
cmake --build <BUILD> --parallel 1
ctest --test-dir <BUILD> --output-on-failure --parallel 1 --no-tests=error \
  -R '^(TreeLookaheadTest|TreeTransferOptionsTest|TreeManifestTest|TreeScanTest)\.'
CPNETFLUX_TEST_TOKEN=<运行时生成的临时值> ctest --test-dir <BUILD> \
  --output-on-failure --parallel 1 --no-tests=error
```

实际路径：

- 源：`/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-08/canonical/execution-source-root/source`
- build：`/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-08/canonical/execution-build`
- 完整 run root：`/tmp/cpnetflux-runs/LOWLEVEL-TREE-LOOKAHEAD-OPS-08`

| 步骤 | 退出码 | 结果 |
|---|---:|---|
| CMake configure（`CPNETFLUX_BUILD_TESTS=ON`） | 0 | PASS |
| `cmake --build ... --parallel 1` | 0 | PASS，目标完整生成 |
| 定向 31 项：TreeLookahead、TreeTransferOptions、TreeManifest、TreeScan | 8 | **BLOCKED**：29 pass、2 fail |
| 完整 CTest 224 项 | 8 | **BLOCKED**：221 pass、2 fail、1 skipped |
| 单独运行 io_uring smoke 用例的 GTest 进程 | 0 | 用例自身 SKIPPED，不是 PASS |

两项最终失败：`TreeLookaheadTest.ReservationAndHandoffAreSingleUse`（测试源第 81 行）与 `TreeLookaheadTest.MetadataOnlyHandoffRequiresOwnerGenerationAndFingerprint`（第 107 行）。日志中的返回枚举原始值为 `01`，测试期望 `AlreadyConsumed` 的 `03`；按枚举声明，实际值对应 `NotReady`。新增的 `TreeLookaheadTest.DepthGateDefaultsOffAndRejectsUnsafeBudget`、`TreeTransferOptionsTest.ParsesLookaheadDepthZeroAndOne` 通过；TreeManifest/TreeScan 受影响测试也通过。

最终定向 CTest 的逐项状态：

| CTest suite | PASS | FAIL |
|---|---|---|
| `TreeManifestTest`（5） | `BuildsManifestPaths`、`SerializesAndParsesRoundtrip`、`RejectsCorruptBodyChecksum`、`SavesAndLoadsAtomicManifest`、`DetectsCompleteManifest` | — |
| `TreeScanTest`（3） | `ScansRegularFilesInStableOrder`、`ValidatesTreeRelativePath`、`RejectsSymlinkByDefault` | — |
| `TreeTransferOptionsTest`（9） | `ParsesUploadOptions`、`DefaultsControlReuseOff`、`ParsesLookaheadDepthZeroAndOne`、`RejectsInvalidLookaheadDepth`、`ParsesTlsClientOptions`、`ParsesTokenAuthOptions`、`ParsesDownloadOptionsAndCreatesDestination`、`RejectsInvalidOptions`、`RejectsSchedulerMetricsInsideLocalRoot` | — |
| `TreeLookaheadTest`（14） | `EligibilityRequiresPendingAndDirectionFingerprint`、`CancelReturnsIndexOnceAndReleasesAfterThreadCleanup`、`DepthGateDefaultsOffAndRejectsUnsafeBudget`、`PreparationSignalPublishesFailureAndWakesWaiterOnce`、`FailedPreparationReturnsReservedIndexOnce`、`CandidateMemoryFailureLeavesNoReservationOrCounters`、`SocketFdHeadroomIncludesPendingAndLiveSocket`、`FailureCancelAndStopRaceReturnsOneIndexAndRollsBackResources`、`CancelVersusHandoffConsumesOrReturnsExactlyOnce`、`CandidateSocketAccountingIsLiveAndFailClosedAtTwo`、`ControlReleaseIsOwnershipBasedAcrossTerminalStates`、`LinuxSocketCounterTracksAnOwnedDescriptor` | `ReservationAndHandoffAreSingleUse`、`MetadataOnlyHandoffRequiresOwnerGenerationAndFingerprint` |

定向总计 31 项：29 PASS、2 FAIL、0 SKIP。完整 CTest 其余用例均逐项记录于完整日志；其中失败仍是上述 2 项，`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 为唯一 SKIP。

第一次对未作行尾规范化的执行快照测试时，full CTest 为 216 pass、7 fail、1 skipped：部分 shell smoke 被 CRLF 影响，另有 token 测试未注入运行时 token。之后在隔离执行快照仅把 8 个 `.sh` 转为 LF，并在 full CTest 进程环境中临时注入随机 token 后重跑；最终结果仍有上述 2 个真实单测失败和 io_uring 跳过。临时 token 未保存或写入日志。

默认行为核验：`TreeTransferOptions` 默认 `lookaheadDepth=0`；reservation budget 的 `reliableCandidateMemory=false`；tree transfer production 默认路径实际有效 lookahead 为 0。未启用生产 lookahead，未启动性能矩阵、跨域传输或外部服务。

## 测试日志和二进制 SHA-256

完整证据已通过 SCP 拉回，并核对本地/远端 hash 一致。主要日志路径（仓库相对）：

- 配置：`build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-logs/configure.log`，SHA-256 `10a38ba7268ca7769a210ecc5ead450a5ed380ffab5e7c26c9f03b5708d7f7e7`
- 构建：`build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-logs/build.log`，SHA-256 `e9d5a642a42dda5e85fca2773b8774fa9a75f528139b95399d9292be9dc0127d`
- 定向 CTest：`build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-logs/targeted-ctest.log`，SHA-256 `3c28b1cab4079bcde0aa3b7f6f56498fe2c6c21ce98d9bdae219d96e6ce87eee`
- 完整 CTest：`build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-logs/full-ctest.log`，SHA-256 `80d20e84ecd702b7c136054d73eaffa77fbf26751b32c6a436d49a7b98d11be6`
- io_uring skip：`build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-logs/uring-skipped-test.log`，SHA-256 `0fb6f0551703e35ebb0378bf94656d0deb93eac03142e483e291ef5912bbedc9`
- 二进制清单：`build/LOWLEVEL-TREE-LOOKAHEAD-OPS-08-artifacts/canonical/evidence-final/execution-evidence/binary-sha256.txt`，SHA-256 `bfe0b0ea853216a807e975a8881104e917f8163a4f13c783aa435b7d0bd7c895`

| Linux 二进制 | SHA-256 |
|---|---|
| `cpnetflux` | `101fa89cba6c880e70b99788dfaddad49d3ad521f53479225089c8f46090c128` |
| `cpnetflux-checksum-bench` | `4c96719af07364713aec3435962525f537549de36d9be8452b632b0722746917` |
| `cpnetflux-client` | `345be6ad895f8841b1a6a09b76b243fa11e496ceb028b31ac6beaa11e840296f` |
| `cpnetflux-file-client` | `43ffbb0e587aa5492209d3e18bc1ae4ce6bdf67e1fd553d3ac507269eb363312` |
| `cpnetflux-file-download-client` | `9a0bbbd1b2e1800e6e5320263f62905e1df0432c20ed4a5114db11cfa92b57f8` |
| `cpnetflux-file-server` | `966ecc0da6218630c94c39cb422de9899dfd887327a6b647005e773f3f7534b7` |
| `cpnetflux-gridftp-server` | `88efbfe3e8692ce638d8b16842f64d1df13716f3a0aeca5d052799fbdc38968c` |
| `cpnetflux-server` | `555f60fa67fed0306c46026989c089bcce0db696556aef3198627c80f1facec6` |
| `cpnetflux-storage-bench` | `4479356613020313bf3fa485cbd0ef5c9d1abd2f9ef8696d3a5a57ebccf2e3b1` |
| `cpnetflux-tree-download-client` | `2eecbe9153c6f17847baba06ba0177ed6be03a22830d6f1ecdceda0ae7d2a6c7` |
| `cpnetflux-tree-upload-client` | `36a2f045aec5272c5f68fcaefa53a54d320917f72fdcc0ac0ae12206e0dfd8e8` |
| `cpnetflux_unit_tests` | `f4edddcf17c2cd1c2a4b4000410b05ccef044f0da68a3725ee0be6cc97748e9f` |

完整证据目录下还包含 configure/build/test 命令与 `.exit` 文件、测试发现清单、full CTest 环境记录（不含 token 值）、执行源 manifest、行尾转换前后 hash、执行前 listener/PID 快照。liburing 开发包和头文件虽存在，`FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` 因运行时报告 `io_uring backend unavailable` 被跳过；没有安装其他依赖或尝试规避该 skip。

## 范围核对与交接

- 未修改 main、共享 index、实现源码、云端历史树或 BOARD/ROSTER；未提交。
- 未进入受保护项目；未清理、移动、停止或重配服务。
- 未运行性能矩阵、跨域传输或 lookahead 性能实验。CTest 中既有 smoke 脚本只使用各自 loopback 临时测试环境。
- 本次 Linux 构建只在深圳完成；上海仅做只读资源/工具链/服务预检。
- 空间和构建门槛满足，但测试验收不满足。需要实现负责人修复两个 handoff 单测所覆盖的结果语义，并另派任务重新运行定向和完整 CTest；io_uring skip 也须由验收方决定后续如何处理。05 未改实现代码，当前不得将该 worktree 或二进制描述为测试全绿/可发布。
