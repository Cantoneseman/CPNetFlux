# V3 Receiver Path 01 结果

日期：2026-11-06。输入提交 `975da5b31f36770706f5e24053a2ba3369a07655`。
写配置/基线提交 `a9ecbe9179c3d8154a91e17fe23b4c9d445f5a74`；span 候选提交
`c4c1b4a134662a740e438da2f34b8aceea7d24fe`。R770-1 单人通过免密 SSH 开发，
R770-2 仅接收固定提交构建。未使用子代理、工作树或桌面控制。

## 本轮结论

1. 有界写合并确实减少 writeAtAll/pwrite 调用，但本次 tmpfs 新目标条件下没有净收益，
   保持默认直接写。参数作为显式选择保留，不作为推荐默认配置。
2. 接收器改用当前帧的只读 span，取消 logical vector 副本和 CRC 前缀 erase。
   三次候选文件阶段中位数为 19.378 Gbps，对照为 17.559 Gbps，约提高 10.36%；
   接收 lane CPU 约下降 9.35%。保留这一改动。
3. 完整 CLI task 仍约 3.048 Gbps，未观察到净提速；冷 data connect 约 2.1 秒。
   当前结果不证明 100G、持续网络线速、磁盘持久化吞吐或生产稳定性。

## 配置入口与实现

最初按旧计划直接传入文件写参数，发现仅单文件 CLI 已支持，V3 目录 CLI 不支持。
首次启动返回 `unknown option: --file-io-buffer-size`，没有进入 payload/QP 数据传输。
原始日志与失败 metrics 保留，不计入性能统计。补齐以下 download-only 配置后重新测量：

- `--file-io-buffer-size 0..67108864`；默认 0。
- `--posix-write-strategy auto|direct|coalesced`；默认 auto，零缓冲时实际 direct。
- 复用 `validateFileIoConfig`，拒绝 coalesced + 零缓冲，拒绝上传端设置接收参数。
- `TreeTransferOptions.fileIo` 传入本地 `PersistentTransferPolicy.options.fileIo`。
  上传接收策略仍由远端服务器管理。

span 只借用当前 `readNext` 返回帧；CRC 前缀用 `subspan(4)` 跳过。同步的 whole/range
write 接口接收只读 span，调用返回前所有校验、写入或缓冲复制完成，不保存借用指针。
压缩帧解码到独立 owned vector，span 指向该 owner，直到本帧处理结束才释放。
CRC-before-write、chunk flush/checkpoint、完整回读校验、发布和恢复语义保持原样。
尚未复用 readNext 帧缓冲，也未借用 RDMA 注册槽，不能称为零拷贝。

## 测试与版本门

下载参数回归在旧代码因 unknown option 失败，修正后两端相关 55/55 CTest 通过。
真实 4 MiB+37 字节传输的独立 new 包装器在接收线程观察 65536 字节分配：
旧代码 133 次，候选 69 次。后者为 64 个满帧和 5 个 chunk 回读缓冲。
初版测试漏算每 chunk 的回读分配，候选以 69>66 失败；核对 readChecksumForRecord 后
将预算明确为满帧数+chunk 数，保留 full 校验，未删除或绕过旧实现。

CRC/none、压缩/raw、观测 on/off 八种组合仍用独立 wire 字节/CRC oracle 和真实输出验证；
logical copy/shift 计数更新为零，解压计数单列，不把解码分配说成零分配。
Framed 测试 14/14；R770-1 全量 543/543（257.70 秒）；
R770-2 相关 187/187（49.91 秒）。构建及 diff --check 通过，无跳过。
每个源码阶段先提交、推送并回读 GitHub SHA，再同步对端；每次互传前核对同提交、
无 tracked dirty、八个二进制 SHA-256、CMake 配置和版本一致。

## 实验口径

所有正式样本：R770-2 -> R770-1，单 lane，单文件 1 GiB，源和目标均 tmpfs。
使用同一源 inode，随机 8 MiB 块重复 128 次，压缩 off、CRC32C、1 MiB chunk、
payload depth=2、every_n_chunks/16、commit_sync=none。各次目标均新建，未提前写满。
范围 checkpoint override=16，但 whole-file 实际值为 null，未改变 whole-file 策略。

显式绑定 192.168.100.2/192.168.100.1，路由为 eno17095np0，实际 data_transport=rdma，
保留双端 native QP、摘要、协议终态和接口计数；控制面仍 TCP。
源预热/源 SHA 在 CLI 计时外；目标创建、写入、发布和冷 RDMA 准备仍在任务计时内。
外部输出 SHA 在任务计时外，用于验收。文件阶段包含提交/结果，不等于 steady wire rate。

先在同一 a9ecbe9 提交轮换 direct/256 KiB/1 MiB 三组，各三次，再以 c4c1b4a
测三次 span-direct 和一次 span-full 控制。两次代码组按先后执行而非随机交错；
每组仅三次，只报告本次小负载的观测，不能当作普适加速比例。

| 中位数，verified_chunks，各三次 | Direct 对照 | 256 KiB 合并 | 1 MiB 合并 | Span + Direct |
|---|---:|---:|---:|---:|
| 文件阶段 wall，秒 | 0.489218 | 0.494971 | 0.511824 | 0.443275 |
| 文件阶段 rate，Gbps | 17.558515 | 17.354431 | 16.782975 | 19.378353 |
| CLI task wall，秒 | 2.817772 | 2.867835 | 2.867904 | 2.817917 |
| CLI task rate，Gbps | 3.048485 | 2.995268 | 2.995196 | 3.048328 |
| 接收 lane CPU，秒 | 0.487589 | 0.493712 | 0.511033 | 0.442021 |
| 三个 payload 线程 CPU 合计，秒 | 0.819686 | 0.833247 | 0.853830 | 0.782664 |
| 客户端进程 user+sys，秒 | 1.70 | 1.70 | 1.71 | 1.66 |
| 双端进程 CPU 估计，秒 | 3.20 | 3.21 | 3.23 | 3.18 |
| 实际 writeAtAll 调用 | 17408 | 5120 | 1024 | 17408 |
| writeAtAll 聚合 wall，秒 | 0.234131 | 0.225755 | 0.230119 | 0.233288 |

三个 payload 线程指发送 producer、发送 lane、接收 lane，均为线程 CPU，不能当作双端进程总 CPU。
客户端进程 CPU 来自 GNU time，精度 0.01 秒；服务器 CPU 来自最后可用 /proc 采样，
两端 CLK_TCK=100，可能漏掉末尾 tick。双端相加只作估计，3.20 -> 3.18 不足以声称显著总 CPU 收益。
冷建连和注册池成本仍存在，不能把约 10.36% 文件阶段收益写成整任务收益。

256 KiB 缓冲不是严格四个写调用/chunk：65532 字节帧的拼接和 chunk 尾部 64 字节
产生五次实际写，所以 1 GiB 为 5120 次；1 MiB 为每 chunk 一次、合计 1024 次。
减少 syscall 没有消除新目标页写入成本，额外合并复制也在 write 聚合中，不能只看调用次数。

候选每 GiB 取消 17408 个 logical 分配、1073811456 字节副本及 1073741824 字节前缀移动；
对照 copy/shift 聚合约 23.606/14.429 ms，候选均为零。接收帧分配仍为 17408 次，
RDMA 槽到帧和文件写入的拷贝仍在。实际帧 CRC 和写入字节均为 1073741824，写失败为零。
writeAtAll 约 233 ms 仍占候选文件 wall 的较大部分，readNext 缓冲复用不能消除这些写入成本。

span-full 单次控制：task 2.868102 秒、2.994989 Gbps，文件阶段 0.568328 秒。
该样本验证 full 回读路径；没有重复 full 对照，不能当作 full 性能收益结论。
累计子计时存在嵌套，不能相加推算总耗时。

## 完整性、内存与清理

13 个正式样本全部 CRC、外部 SHA、源/目标驻留、FILE_RESULT、DirectoryEnd/控制终态通过。
发送进程物理读取均为零。每次输出验收后删除，最终源删除前再次核对 inode 和 SHA。
本轮唯一排除项为前述传输前 CLI 启动失败。

| 全部正式样本 | R770-1 | R770-2 |
|---|---:|---:|
| 实验进程 RSS 采样峰值，GiB | 2.06889 | 2.06794 |
| 主机 MemAvailable 采样最低，GiB | 25.35866 | 24.96295 |
| tmpfs 峰值，GiB | 1.00004 | 1.00000 |
| 实验进程 VmSwap 峰值 | 0 | 0 |
| 主机 swap-out 增量 | 0 | 0 |
| 内存守护 violation | 0 | 0 |

RSS 与 tmpfs 分开记账；每端约 2.04 GiB RDMA 接收池仍是主要常驻资源。
守护复用已有脚本，启动 MemAvailable >=12 GiB、tmpfs 空闲 >=2 GiB，采样低于 6 GiB
或本轮进程合计 RSS >4 GiB 时仅终止本轮进程，属于采样守护而非内核硬配额。
主机全局有少量 swap-in（R770-1 一次 12 页，R770-2 两次 5/1 页），
不能归属给 payload；实验进程 VmSwap/物理读取和源驻留门均通过。

全部 RNR/out_of_buffer 为零，但 CQ response error/flush 仍出现 1/0、65/64、129/128、
193/192、257/256，尚未归因，稳定性门不关闭。没有修改网络、系统权限或重启生产服务。
清理后双端 payload 根不存在、无本轮进程、仅内核 GSI QP；保留原 PID 126766/164834
以及两份历史 untracked October 任务文档。

源 SHA：`2fbf31fdde0ddb00c31d1319cc97db9c8271c1f998216ca3b999fbf15a88eed6`，inode 17。
证据根：`/home/Su/experiments/CPNetFlux/2026-11-06-v3-receiver-path-01`。
大 payload 不入 Git；保留 failed/verified metrics、双端日志/路由/计数/驻留/内存、
acceptance/comparison.json、rollup.json、cleanup-R770-*.json、matching-builds-* 和验收日志。
最终文档提交、GitHub SHA 回读、双端构建摘要和下一步记录见外部
`acceptance/github-backup.json` 与 `HANDOFF.md`。

## 下一步

独立实施每 lane 有界 readNext 帧缓冲复用，覆盖短 DATA、控制帧、压缩、错误/取消和恢复。
必须保留当前 span 所指帧的所有权，消费完成前不得覆盖。沿用单 lane/1 GiB 内存实验与守护，
继续对比真实分配、CPU、文件阶段和完整任务。RDMA 接收池准备/连接复用以及 CQ teardown
另立阶段，不通过增大并发或把预触页移到计时外制造收益。磁盘队列和大文件调查继续延后。
