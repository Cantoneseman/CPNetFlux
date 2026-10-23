# V3-TREE-MANIFEST-COALESCE-01

日期：2026-10-23
状态：实现、本机性能/正确性验收及上海同步构建核验完成
输入提交：618c5dabeff4b95bb3d311ba3cdad861545817b7
权威工作区：深圳 /root/projects/CPNetFlux；单人串行，沿用当前分支。

范围：将 V3 tree 状态事件与清单序列化/原子写分离；唯一后台写者按 32 个事件或 100 ms 合并，使用 generation 防止陈旧快照覆盖；结束/失败/取消在工作线程退出后强制写出最新状态。添加事件数、写次数、序列化字节、锁等待和版本指标。

非目标：不改变 per-file checkpoint、数据写、协议、压缩、系统网络或物理链路；不改变 tree manifest 文件格式；原子 rename 仅承诺进程退出/重启，不承诺断电安全；最终双机 GridFTP 对照留在第 8 项。

验收：
- 先用真实 V3 dense 上传观察清单 rename 次数，证明旧版逐事件写出；新实现明显合并。
- 单元测试覆盖事件/时间触发、并发更新、I/O 锁外执行、最后版本屏障、保存失败传播。
- 上传/下载、干净失败、SIGKILL 后 resume、陈旧 Completed 验证和最终 SHA-256 正确。
- 固定 dense 输入做至少 5 组旧/新交错测试，记录 wall、事件/快照/字节等；本机结果不当成 R730→R750 最终性能验收。
- Release 构建、相关/完整 CTest、git diff --check；明确文件提交并推送 GitHub，回读 SHA。

预期文件：
- include/cpnetflux/core/tree/tree_manifest_writer.h
- src/core/tree/tree_manifest_writer.cpp
- src/core/tree/tree_manifest.cpp 与对应头（写出字节计数）
- src/core/io/tree_transfer_client.cpp
- tests/unit/tree_manifest_writer_test.cpp
- tools/test/run_cpnetflux_tree_manifest_coalesce_smoke.py
- CMakeLists.txt、本记录、性能路线、docs/ROADMAP.md

构建目录：/tmp/cpnetflux-build-fd-release-01（复用前项）；旧二进制证据隔离到 /tmp，不建源码 worktree。

## 实施结果

代码提交：0b2a3a77815b4e97e15fb2970f03429e0374fbf9（已推送 origin 当前分支并回读一致）。

- TreeManifestWriter 使用一个后台线程；回调在状态锁内修改记录并递增 generation。后台在 32 个待处理事件或最早待处理事件等待 100 ms 时取快照，在锁外序列化和原子替换。只有该线程写盘，旧快照不可能晚于新快照覆盖。100 ms 是触发等待上限，不是慢盘环境的持久化完成时限。
- 所有 transfer workers 停止后，在同一状态锁下排序、整理失败记录并标记最后 generation，随后 finish/join 强制等待。保存失败为 sticky error，最终返回失败；JSON 报告 generation/persisted generation，不冒称未落盘状态已保存。
- 首次清单和 resume preflight 保持同步写盘，后台线程仅覆盖 persistent callbacks 到最终快照这一段，绝不与同步写者并存。文件格式、per-file checkpoint 和原子 rename 的保证均不变，不新增断电/fsync 承诺。
- 修复同一范围内的恢复缺口：上传重启时也向 V3 接收端重新提交 Completed 身份，核验 committed output/checkpoint。完好文件跳过 payload；同大小且保持 mtime 的损坏输出会被拒绝。global scheduler 为这些身份建立核验 work lease。
- JSON 新指标：tree_manifest_state_events、tree_manifest_generation、tree_manifest_persisted_generation、tree_manifest_snapshots、tree_manifest_serialized_bytes、tree_manifest_write_seconds、tree_manifest_state_lock_wait_seconds。跨 reconnect attempt 以各 attempt 数值求和，scope 字段明确范围；bytes 是序列化产生的字节（失败尝试也可能计入），snapshots 为保存尝试数，write seconds 含序列化/I/O。锁等待是回调获取状态锁与后台 I/O 后重新获取锁的累计时间，不含条件变量等待或其重获锁的时间，不可当作 wall。

## 回归与本机性能证据

旧实现先复现：256 文件发生 514 次 tree manifest rename；新实现的 rename 合并门禁因此能捕获旧行为。另以旧二进制复现 Completed 上传恢复会忽略已经损坏的输出。新增 7 个单元测试覆盖事件阈值、时间触发、并发更新、慢 I/O 不占状态锁、保存顺序/最新 generation、失败传播和析构兜底。

新增 smoke 覆盖：

- 256 文件上传/下载的独立 SHA-256、最终所有状态落盘和 rename 次数；
- 上传/下载分别在第 2、40 个文件已发布时 SIGKILL。实际文件数为 2、40 时，落盘 tree 清单的 Completed 只有 0、37；四例重启 resume 均最终通过 SHA-256，证明陈旧目录状态没有误导恢复。
- 普通/全局调度下全量 Completed 恢复：完好的 16 MiB payload 全部跳过、不重传；保持大小和 mtime 的损坏文件拒绝成功。
- 通过真实非空目录阻止原子 rename，验证终态 fail 且 persisted generation 小于 generation，错误没有被吞掉。

2048 × 64 KiB、4 lane、CRC32C Auto、无压缩，本机 TCP loopback、两组均 TLS off，5 组交错 old/new：

| 方向 | 旧进程 wall 中位 | 新进程 wall 中位 | 旧清单 rename | 新清单 rename |
|---|---:|---:|---:|---:|
| 上传 | 5.4561 s | 1.2447 s | 4098 | 128–129 |
| 下载 | 3.6279 s | 1.1938 s | 4098 | 129 |

全部 20 次输入/输出 SHA-256 tree 一致。上传中位约 4.38 倍、下载约 3.04 倍，这是深圳单机对照，不能替代 R730→R750、TLS 开启、GridFTP 的第 8 项最终矩阵。精简原始数据见 docs/tasks/2026-10-23-v3-tree-manifest-coalesce-01-evidence.json；大 payload 与全量日志在 /tmp/cpnetflux-coalesce-paired-01，不入 Git。

验收命令：

    cmake -S . -B /tmp/cpnetflux-build-fd-release-01 -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON
    cmake --build /tmp/cpnetflux-build-fd-release-01 -j2
    ctest --test-dir /tmp/cpnetflux-build-fd-release-01 -R 'TreeManifestWriterTest|cpnetflux_tree_scheduler_smoke|cpnetflux_tree_manifest_coalesce_smoke' --output-on-failure
    CPNETFLUX_TEST_TOKEN=cpnetflux-test-token-only ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j1
    python3 tools/test/run_cpnetflux_tree_manifest_coalesce_smoke.py --build-dir /tmp/cpnetflux-build-fd-release-01 --baseline-build-dir /tmp/cpnetflux-pre-coalesce-618c5da --files 2048 --rounds 5 --evidence-dir /tmp/cpnetflux-coalesce-paired-01
    git diff --check

首次完整 CTest 有 1 项失败：scheduler smoke 在 Completed 恢复路径出现 work lease mismatch，已定位并修复其调度输入。修复后相关 9 项通过，重新完整运行 426 项零失败（1 项 io_uring 环境条件 skip），151.43 s。完整日志 /tmp/cpnetflux-coalesce-ctest-final.log。没有把首次失败标为通过。

下一项：V3-RANGE-OBSERVABILITY-01。增加大文件 pipeline 等待时间/水位、range state lock wait/hold、数据写入与 checkpoint 分开的计数和耗时，再做默认 depth=2 与 4/8/16 的受控实验；不直接猜测参数或拆共享 writer 锁。此后还有 checkpoint 合并、range 并行写、profile 热点优化、最终双机对照，总计 5 个执行门。

## 两端同步和备份

上海对端源码目录 /root/projects/CPNetFlux 由代码提交 0b2a3a77815b4e97e15fb2970f03429e0374fbf9 的 git archive 创建；既有 CPNetFlux-a9905aa 等历史目录未修改。SOURCE_COMMIT 记录相同提交，双方构建目录均为 /tmp/cpnetflux-build-fd-release-01，Release / CPNETFLUX_BUILD_TESTS=ON，GCC 11.4.0。上海全量构建成功，相关 writer、coalescing 与 scheduler 共 9 项 CTest 全部通过。本轮没有深圳↔上海 payload 传输样本，不把对端构建核验当成跨机性能验收。

两端 SHA-256 均一致：

| 产物 | SHA-256 |
|---|---|
| 提交源码归档 | 014fe28f72983dd8e8d7cfc48cfb3e579a1744f2c78e347f3584b0d8edc97821 |
| cpnetflux-tree-upload-client | 44c2d05438ab7022380ee00fd15677cbbb87ebe499e2a8d966fb84bddfa6dffc |
| cpnetflux-tree-download-client | 84af0f2235b0a7c966f2906844817b5f9705ce13a25ae246d6ee2bf1b4f96179 |
| cpnetflux-gridftp-server | f2e4c4badc5138524d38abe3c17b31d64400a79cdc4a0f5d9388aeac060efcc9 |

运行时代码已在 GitHub 备份；后续文档提交仅更新本记录、精简 evidence、路线图和性能计划。Windows 性能优化路线.md 从深圳计划同步并核对 SHA-256。
