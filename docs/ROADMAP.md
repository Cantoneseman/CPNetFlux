# CPNetFlux 性能路线图

更新日期：2026-11-09

当前进度：[Receiver Critical 13 结果](tasks/2026-11-09-v3-receiver-critical-13-result.md) 已完成默认关闭的接收 CPU 归因，并按[V3执行路径决定](DECISIONS/2026-11-09-v3-dynamic-download-local-executor.md)在 `127f4b66` 修复 dynamic 下载且 scheduler=off 时遗漏本地 WorkExecutor 的缺口。两端相关214/214测试、六次8GiB完整任务通过；CPU计时off/on中位数27.602/26.836 Gbps、CV1.141%/1.516%，观察者wall开销2.858%。旧8f42c851首对的内容/终态证据保留，cpu1合同失败不改写为通过；旧28–29Gbps不代表补齐路径后的验收。

当前实验基线为上述 `127f4b66`、R770-2 -> R770-1、四路TCP、8GiB tmpfs驻留源、queued发送/inline实验mmap接收、CRC32C/verified_chunks、TLS off、commit_sync=none，既有detail观测仍开；CPU-off的27.602 Gbps不是lean默认或生产成绩。新增计时样本中physical mapped write约占lane CPU57.6%、socket read22.6%、lease2.1%；CPU总量与并发wall不能相加。下一步区分映射切换、首次触页/页分配和memcpy；不据此删除校验/租约、预热输出或原样重试失败的population候选。只在明确机制后选一个有界候选，以五对真实文件>=10%收益/CV<=5%验收。**三阶段工程已收口，30–50 Gbps完整文件门仍未过，继续暂停线程/队列扩张与RDMA。**

上一阶段：[Sender Service 12](tasks/2026-11-08-v3-sender-service-12-result.md) 已在 `351ae7ce` 完成驻留源read/CRC/队列局部隔离和一个显式同步发送候选。五组完整8 GiB对照中，queued/inline为28.757/28.092 Gbps（-2.313%），CV1.351%/0.854%；发送端CPU下降40.663%、双端CPU/GiB下降21.852%，但吞吐门未过。其下载端仍有scope13发现的local executor缺口，保留原配置结论，不替代修复后测量。默认仍queued，inline仅保留显式诊断/CPU效率实验入口；不继续盲调发送队列。

最近通过吞吐保留门的变更仍为 [Mapped Download 08](tasks/2026-11-08-v3-mapped-download-08-result.md)：`535d33db` 完成 V3 下载 CLI 显式映射写集成、错误/恢复与 range-attempt 生命周期验证，以及五组 8 GiB 完整任务 A/B。四路 inline 的 pwrite/mmap 中位数为 24.040/28.864 Gbps，CV 2.083%/1.875%，收益20.07%，五组配对全部改善；双端CPU/GiB下降9.18%。默认仍为pwrite/queued，不自动选择。

历史 scope 08 完整文件成绩为 R770-2 -> R770-1、tmpfs驻留源、TCP/TLS off、verified_chunks、commit_sync=none；同样需结合scope13发现的local executor缺口解释，不代表修复后成绩。没有通过反向、TLS-on、持久化或长时间稳定性验收。此前 [Lane / Storage 06](tasks/2026-11-08-v3-lane-storage-06.md) 的 queued 1/2/4 lane 为22.764/18.128/15.537 Gbps，inline为15.736/23.378/24.139 Gbps，按 `76f7572d` 保留，不能替代本轮同提交单路复测。[Write Backend 07](tasks/2026-11-08-v3-write-backend-07.md) 的四线程54.896/55.348 Gbps仍只是本地可行性诊断；单线程映射写退步约49%，双线程收益不足10%，本轮不推广这些配置。

旧阶段记录：`201a43a047102dfe52b1f05ec4497181dd64f6bb` 的 [TCP lane复核与两候选结果](tasks/2026-11-07-v3-tcp-scaling-audit-01-result.md) 保留原结论。file_serial/file_dual 两候选均 no-go；其 perf 定位到同 inode tmpfs 写锁 CPU 成本。后续 raw TCP、V3 帧内存路径、完整文件交接和本轮写入服务率已分别完成测量，不能再把它们标为尚未执行，也不重开失败的串行准入候选。

最近原生文件实测源码：`791c2eee785d0c197f8ff86d902f6ce8688112c8`（清单序列化优化），同组观测基线为 `d0d98c74b15b728cf2853ec168c26a7a3cc27c70`；最新源供数工具源码为 `5b8be5af0c034451c117f372e346c049228299cc`，其六个传输相关二进制与前者相同。接收策略对照源码为 `d323f1fa442071bee64650c44c96d8a11ba42b1f`。原 48 样本和 result 生命周期复测仍按各自输入提交解释。
此前接收 span 的小规模 tmpfs 原生文件优化源码：`c4c1b4a134662a740e438da2f34b8aceea7d24fe`（接收 logical span），对照/写合并配置为 `a9ecbe9179c3d8154a91e17fe23b4c9d445f5a74`。此前接收端剖析为 `bdc82c13`、payload 复用为 `7aab4e68`、基线 `6025f54e`，CRC 拼接优化为 `46be03da`。上行所述 `791c2eee` 仍是最近 64 GiB 盘上负载源码，两类负载分别解释。
当前开发分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`
上一阶段非 RDMA 工程验收提交：`ccca9b66bab457ecb6e6c3780d7c6fe1d1e33179`，性能矩阵源码`8d44802a33cd8d3bfc17ad9e5bc5a8da8c23ee2f`（八二进制相同）；[三阶段收口结果](tasks/2026-11-07-v3-stage-decoupling-01-result.md)、[执行指南](V3_TRANSFER_STAGES.md)。独立读/网络/写并发、接收共享就绪writer池、payload预算工程门已过；R1 607/607、R2 270/270、pool故障9/9，118正式1GiB内存任务校验/清理。c4在相同read4/write1下，admission→共享就绪池的task中位数8.404→14.226Gbps（+69.29%），双端CPU2.950→2.800秒/GiB。相对每lane默认，池的task中位数变化+0.75%，双端CPU变化-32.04%。这是三次小样本中位数，需结合CV；不能宣称长期吞吐提升或所有负载收益。 默认read0/write0、batch1/depth2/AvailableCapacity/preallocate off不变，吞吐门仍开。此前[range独立写](tasks/2026-11-07-v3-tcp-scaling-01-result.md)、[gathering负结果](tasks/2026-11-07-v3-gathered-write-01-result.md)保持各自提交边界；非持久化TCP不是100G验收。

## 当前目标

在 R770-1/R770-2 单端口 100GbE/RoCE 上，建立真实文件端到端性能证据，再按剖析结果优化。单端口路线不等于 10 端口 1Tbps 聚合验收。

原基线已采集 48 个样本，42 通过、6 个 64 GiB 下载失败，详见 [基线结果](tasks/2026-11-04-r770-rdma-e2e-baseline-01-result.md)。[V3 缺失块握手修复](tasks/2026-11-04-v3-resume-mask-01-result.md) 和 `fd10e16` 的 CQ 等待修复没有自动使完整文件门通过。

11/5 的[协议尾部诊断](tasks/2026-11-05-r770-rdma-tail-trace-01-result.md)定位了确定故障路径：64 GiB 文件发布且外部 SHA-256 正确，range 8 的 FILE_RESULT 两端均成功读写，但服务端在等待 172.614 秒后做 V3 lease finalize 返回 InvalidArgument。payload I/O 会刷新默认 30 秒租约，CommitPending 没有独立预算，而整文件校验耗时约 149.912 秒；成功结果到达时租约已过期，该通道没有发 DirectoryEnd，客户端继续等待。该诊断提交未修复行为，后续修复与验收见下文。

11/5 后续的 [V3 result 生命周期修复](tasks/2026-11-05-v3-result-lifetime-01-result.md)已通过 R770-1 全量 503/503、R770-2 相关 76/76 CTest，以及 1/4 lane 各三次原生 RDMA 64 GiB 下载完整任务与独立 SHA-256。两端同提交、同配置，五个二进制 SHA-256 一致；每次任务均无恢复重试，全部数据/控制终态完成。独立 result/commit 预算为 5 分钟且受任务截止约束，payload 空闲租约仍为 30 秒，过期和 owner/epoch/attempt 隔离保留。最长实测结果等待 174.730 秒已正常结束。另有 31 秒实际 TCP 等待、原生 RDMA shutdown/control-EOF 和连续 10 次终态丢失恢复证据。

上一轮源码 `78e1e4ab` 的下载 task rate 中位数为单通道 1.831384 Gbps（CV 0.254366%）、四通道 1.536711 Gbps（CV 0.348727%），均采用客户端接收端 full 校验和 commit_sync=none；外部 SHA 在任务计时之外。四通道约慢 16.1%，这是正确性修复后的结果。全文件复读校验约 149-158 秒，四通道后半组 range 还有约 80 秒事务锁等待；这些累计线程计时不可相加。该组 RNR/out_of_buffer/transport retry/ACK timeout 增量为零，但 CQ error/flush 增量尚未归因。原 48 样本的 42 PASS / 6 FAIL 保持历史结果，不改写成 48/48。

11/5 的[接收策略实现与对照](tasks/2026-11-05-v3-receiver-policy-01-result.md)已完成：下载 CLI/JSON 显式配置和报告本地 final-verify、manifest/checkpoint、commit-sync，上传接收策略标明远端未观测，默认值保留。R770-1 全量 514/514、R770-2 相关 137/137 CTest 通过。源码 `d323f1fa` 的单通道 64 GiB full/verified_chunks 交替各三次，全部协议终态与外部 SHA 通过；相同 every_n_chunks/16、commit_sync=none 和 CRC32C 下，task rate 中位数为 1.836107 / 3.794028 Gbps，CV 为 0.592008% / 0.463039%，约 2.066 倍。最终提交阶段中位数为 155.591705 / 0.058609 秒。verified_chunks 省去提交前复读，故障覆盖不同；默认 full 未改，不能称为同完整性语义下的算法提速或默认吞吐翻倍。

本轮 full 三次接收端 out_of_buffer / 发送端 rnr_nak_retry_err 增量分别为 2685、7681、5633，分块策略三次为零；增量发生在 FILE_END 前，不能据此归因给最终校验策略。两端 CQ error/flush 仍有 65/64 或 129/128 增量，集中在成功结束附近；仅有时间相关性，尚未解释。生产稳定性门仍未通过，不能沿用上一组零 RNR 结论。

11/5 的[顺序记录查重优化与剖析](tasks/2026-11-05-v3-record-index-01-result.md)使用基线 `ed8da0df` 和候选 `92f99af7` 各 24 组元数据测量。65536 条记录、每 16 块保存一次时，下载/上传查重中位数从 1.429696/1.432952 秒降到 0.001188/0.001221 秒，顺序追加扫描数从 2147450880 降为零；元数据流程 wall 从 27.123931/27.035220 秒降为 25.879379/25.727391 秒，分别减少 4.588389%/4.837501%。实现只增加两个最大值水位，重复和乱序仍保留原扫描，恢复和损坏清理时重建；没有改校验、checkpoint 间隔、磁盘格式和默认策略。R770-1 全量 525/525、R770-2 相关 154/154 CTest 通过。该 benchmark 不写 payload、不走网络，其收益不是文件吞吐提升。

同提交、同配置及六个二进制 SHA-256 一致后，原生 RDMA 单 lane 64 GiB 下载 full/verified_chunks 各一次均通过协议终态和独立 SHA。单次 task rate 为 1.906309/3.794339 Gbps，finalize 为 145.081791/0.046618 秒；它们是完整性诊断，没有构成新的重复性能对照。两轮 RNR/out_of_buffer 增量为零，两端仍各有 CQ error/flush 65/64 增量，生产稳定性门未通过。临时输出均在 SHA 后清理，服务进程与空闲 QP 身份已核对。

11/5 的[清单序列化优化与实测](tasks/2026-11-05-v3-manifest-serialize-01-result.md)已将真实 whole-file 的查重、外层排序、保存以及序列化内的记录准备、range 校验、文本格式化和 body CRC 分项观测接通。序列化借用已有有序记录，乱序输入只私有复制/排序一次；使用预留字符串和 to_chars 格式化，保留旧字节格式、CRC、损坏拒绝、原子发布、恢复和所有策略默认值。独立旧格式 oracle 与工作量约束先红后绿，R770-1 全量 534/534、R770-2 相关 138/138 CTest 通过。每版本 24 组元数据测量中，65536 条、every16 的下载/上传 wall 从 25.396926/25.810290 秒降到 12.860539/12.991456 秒，减少 49.36%/49.67%；序列化内部复制和排序归零，但 session 外层排序仍保留。

相同接收策略下，原生 RDMA 单 lane 64 GiB verified_chunks 每版本各三次均完成任务和独立 SHA，task rate 中位数为 3.843084 / 3.856495 Gbps，CV 为 0.305251% / 0.590554%。候选约高 0.35%，小样本和先旧组后新组的顺序不足以证明明显文件吞吐提升；约 2 倍的元数据收益不能改写成文件提速。真实清单序列化中位耗时从 38.235814 降到 20.727219 秒，manifest 聚合从 52.059743 降到 36.723001 秒；它们是嵌套区间，不能从 task wall 直接相减。候选 full 策略补充门也通过任务终态和独立 SHA，用时 296.126176 秒，其中 finalize/readback 为 155.748470 秒；这一次完整性门不构成新的 full 策略性能对照。

11/6 的[源端供数对照](tasks/2026-11-06-v3-source-supply-01-result.md)已完成。修复存储 benchmark 在只读成功/失败后删除输入的问题，并增加显式 read+CRC 和可复现的源供数脚本；两端相关 51/51 CTest、构建和七个二进制摘要一致，六个原有传输相关二进制未变。R770-2 同一 64 GiB 源文件的 64 KiB/1 MiB/8 MiB POSIX 同步读取各三次，中位数为 4.04734/3.95836/3.96494 Gbps，CV 均低于 0.1%，增大读块没有有效收益。65532 字节 read+硬件 CRC32C 三次为 4.04570 Gbps，中位 checksum/combine 2.68731 秒、读取约 132.99 秒。每次物理读量约 67.6 GB，全部逻辑字节与最终独立 SHA 通过，源 inode/长度/mtime/ctime 未变。checksum 组的对齐和顺序不同，不把 wall 差异全部归给 CRC；探针也不复刻生产帧分配/队列和 1 MiB chunk 边界。

上一轮真实文件的发送端 readAtAll/消费者等待中位数为 127.136050/114.453123 秒，task rate 3.856495 Gbps，约为本轮 64 KiB 源读取服务率的 95.28%。这是不同日期与缓存历史的诊断对照，支持优先调查供数，不是原生传输 A/B，也不是硬件或软件上限证明。两端约 30 GiB 内存，源文件 64 GiB，当前 /dev/sda2 ext4 上的结果不能当作全缓存网络能力。本轮保留工具和输入保护改动，未改传输读块、pipeline、checksum、checkpoint 或默认策略，也没有新的文件提速成绩。独立缓存热 CRC 原语约 868 Gbps 只处理重复 64 KiB 缓冲的逻辑字节，不能当作文件或网络性能。

11/6 的 [Cache Path 01 结果](tasks/2026-11-06-v3-cache-path-01-result.md)已按用户要求完成小规模软件路径对照：1 GiB tmpfs 源/目标，基线与候选各三次 verified_chunks，再补一次候选 full 控制，全部正式样本 CRC、外部 SHA、协议终态与原生 QP 通过、无恢复。每次源页全驻留，发送进程物理读仅基线一次 72 KiB、其余零；该进程计数还含元数据/库文件。源和每次输出已清理，两端本轮临时根均不存在。R770-1 全量 536 项中初次 531 通过、2 项缺测试 token 失败、3 项缺显式 RDMA bind 跳过，补齐环境后五项全部通过；合并覆盖全部 536 项。R770-2 相关 137/137 通过，候选八个二进制/配置/版本一致。

实现只消除逐帧附加 CRC 时的临时 vector 和整 payload 复制，沿用头部/CRC/原数据三段接口，保留编码、校验和策略默认值；RDMA 注册暂存仍复制，不能称为零拷贝。发送 frame_prepare 中位耗时 61.855149 ms 降至 1.985696 ms，但 socket_write 从 38.260775 ms 增至 92.577312 ms，lane CPU 未降。接收文件阶段中位数 17.815516 / 17.722599 Gbps，整 CLI task 为 3.048465 / 3.103627 Gbps、CV 0.851890% / 0.864489%。冷建连约 2.10 秒，主导小文件 task；候选 full 单次 task 2.894189 Gbps。顺序小组不是随机配对 A/B，未证明有效端到端提速，不把组帧计时下降改写为吞吐收益。本组 RNR/out_of_buffer 为零，但 CQ error/flush 仍有 65/64，首次候选接收端为 193/192，稳定性门仍开。

11/6 的 [Payload Reuse 01 结果](tasks/2026-11-06-v3-payload-reuse-01-result.md)完成发送端有界缓冲复用：队列仍为深度 2 / 131064 字节，默认最多 4 个 65532-byte payload 缓冲，空闲缓存最多 3 个，取消/关闭释放缓存。独立真实传输 allocation 回归在旧实现测到 64 次分配并失败，修正后连续 20 次通过；并发缓存压力发现空队列等待时需要 depth+1 个空闲槽，已修正并连续 30 轮通过。R770-1 全量 542/542、R770-2 相关 146/146 CTest 通过，互传前八个二进制/配置/版本一致。

本轮 1 GiB tmpfs verified_chunks 基线/候选各三次及一次候选 full 控制，七个正式样本全部 CRC、外部 SHA、协议终态和原生 QP 通过，源页传输前后驻留且发送进程物理读为零。候选每次实际分配 4 次、复用 17404 次，容量合计 262128 字节；producer+lane CPU 中位数从 0.425706 降至 0.329747 秒（下降 22.54%，不是进程/双端总 CPU）。接收文件阶段中位数 17.832948 / 17.770356 Gbps，整 CLI task 3.103407 / 3.048293 Gbps，未证明吞吐收益；冷建连约 2.1 秒。一个源页部分换出、物理读约 349 MB 的候选被工作负载门排除并保留失败证据，后续计时外预热并在启动 payload 前断言驻留。正式样本 RNR/out_of_buffer 为零，CQ error/flush 仍有 1/0、65/64、129/128，稳定性门继续保留；输出与最终 1 GiB 源均已清理，生产进程未动。

11/6 的 [Receiver Profile 01 结果](tasks/2026-11-06-v3-receiver-profile-01-result.md)已完成。新增默认关闭的 CPNETFLUX_RECEIVER_PROFILE=1 细分观测和可复用内存守护；R770-1 全量 542/542、R770-2 相关 146/146 串行 CTest 及两端各三项守护测试通过。七个 1 GiB tmpfs 正式样本全部驻留、CRC/外部 SHA、终态及原生 QP 通过，发送物理读为零。开启/关闭组文件阶段中位数 17.628184/17.726173 Gbps、整 task 3.048416/3.048573 Gbps；这是诊断开销对照，未改接收算法。每 GiB 有 17408 个帧 vector 和 17408 个 logical vector，副本复制/前缀移动/帧 CRC 分别约 23.769/14.395/20.388 ms，实际 17408 次 writeAtAll 为 233.351 ms。写入路径是下一对照的优先项，不能把所有成本归为用户态复制。

每端 RSS 峰值约 2.068 GiB，另有约 1 GiB tmpfs，最低 MemAvailable 为 25.466/25.072 GiB；无守护 violation、进程 VmSwap 或主机 swap-out。两次后台全局 swap-in 为 1/12 页，未归属于 payload。独立三对 tmpfs 写探针以相同帧大小测得首次写新页/覆盖驻留页 0.225910/0.153263 秒，物理读写为零；它支持调查新页写成本，不是传输吞吐。源/输出/探针均已清理。RNR/out_of_buffer 为零，但 CQ error/flush 仍有 1/0、65/64、193/192，稳定性门不关闭。

11/6 的 [Receiver Path 01](tasks/2026-11-06-v3-receiver-path-01-result.md)已完成：补齐 V3 目录下载文件写配置入口，direct/256 KiB/1 MiB 各三次轮换对照，合并写调用降为 5120/1024，但无净收益，默认保留直接写。独立 span 改动将每 GiB 的 17408 个 logical 副本和前缀移动归零，三次候选文件阶段中位数 19.378353 Gbps，对照 17.558515，约提高 10.36%，接收 lane CPU 下降 9.35%。含冷建连 task 仍约 3.048 Gbps，双端进程 CPU 估计 3.20/3.18 秒，不能声称明显整任务或总 CPU 收益。R770-1 全量 543/543、R770-2 相关 187/187 通过；13 个正式 tmpfs 样本全部完整性/终态/内存门通过，另保留一次传输前 CLI 启动失败。双端 payload 已清理，CQ error/flush 仍未归因。

11/6 的 [Frame Reuse 01](tasks/2026-11-06-v3-frame-reuse-01-result.md)完成连接内有界 readNext 帧复用，DATA 分配从每 GiB 17408 次降为 1 次、复用17407次，保留容量64 KiB；短帧、控制帧、截断、取消和现有压缩/恢复回归通过。R770-1 全量547/547、R770-2 相关192/192，独立 allocation 回归先在旧代码失败再连续20轮通过。七个原生 tmpfs 样本完整性/协议/内存门通过并清理；三次候选/对照文件阶段中位数 19.818295/19.206028 Gbps，接收 CPU -3.08%，task 3.102815/3.103481 Gbps。小组顺序实验不代表普适加速，冷建连与 CQ 生命周期仍待独立处理。

11/7 的 [V3 三阶段流水线 01 结果](tasks/2026-11-06-v3-pipeline-01-result.md)已完成：接收端引入有界 network/checksum→write 队列和独立 writer，保留 checksum 前缀逻辑视图与断连排空语义。互传输入固定提交 `50efc0e`；后续清理/测试提交 `efbc3e5` 下 R770-1 全量 552/552、R770-2 相关 134/134 通过且八个二进制摘要一致；1 GiB tmpfs verified_chunks 深度 2/8 各三次均通过 CRC、独立 SHA、原生 QP、RSS/交换分区和清理门。深度 2/8 的完整 task rate 中位数为 5.484997/5.484676 Gbps，文件阶段为 21.777862/21.922598 Gbps；消费者等待 12/1 次，完整任务无可重复收益。写入与建连仍是主路径候选，不能把本轮结果说成 100 Gbps 或 RDMA 优化。

当前下一步：按[内存实验决策](DECISIONS/2026-11-06-v3-small-cache-experiments.md)继续总量1GiB、单源/当前单输出、内存/交换分区守护。[流水线02](tasks/2026-11-07-v3-pipeline-02-result.md)的唤醒/预分配与[gathering](tasks/2026-11-07-v3-gathered-write-01-result.md)已完成且无净收益，不再列为待实现。[TCP range锁拆分](tasks/2026-11-07-v3-tcp-scaling-01-result.md)缓解多lane退化但增加CPU，当前仍在阶段3；先归因writer系统CPU、同inode并发pwrite与lane交接，再按证据选择按文件固定writer/有界写队列或其他一个因素。独立全局三阶段worker池未实现；不先扩线程/缓冲，不自动提高lane。固定版本、八哈希、CRC/SHA、恢复、发布与清理门保持；RDMA、磁盘和100G稳定性门仍独立开放。

稳定性调查保持独立门：旧组第 1 次出现 out_of_buffer/发送端 RNR 7851 次，候选第 2 次为 12762 次，其余 verified_chunks 样本为零。CQ error/flush 的端点增量仍有 65/64 或 1/0，不能凭成功结束就认定无害。先分段记录传输、FILE_RESULT/DirectoryEnd、QP close/destroy 和在途接收所有权，再按证据推进 RQ/CQ 资源与三阶段流水线；生产稳定性门未通过。range checkpoint override 16 的 whole-file 实际值仍为 null，默认校验、checkpoint 和 commit-sync 均未改变。

接收策略实现中的截短回归发现：旧报告所说的 checksum=none 强制完整复读并非实际行为，session 会直接跳过该复读，且原发布路径缺少实际长度检查。本轮补上所有校验策略发布前的长度门，并明确摘要中的 coverage_only/no-readback 语义，见 [校验语义决策](DECISIONS/2026-11-05-v3-receiver-verification-semantics.md)。既有 CRC32C 测量不受这一描述纠正影响；无 checksum 模式不进入完整性合格的性能对照。

完整范围、计时口径、阶段验收与性能目标见 [单链路 100G 性能提升计划](tasks/2026-11-04-performance-roadmap.md)。本轮路线决策见 [2026-11-04 性能决策](DECISIONS/2026-11-04-performance-route.md) 和 [result 生命周期决策](DECISIONS/2026-11-05-v3-result-lifetime.md)。下表区分历史链路/bench 证据与最新文件测量。

## 当前证据

| 类型 | 已测证据 | 不能推出 |
|---|---|---|
| 原生链路 | `ib_send_bw` 约 92.59 Gbps | CPNetFlux 文件传输达到 92.59 或 100 Gbps |
| CPNetFlux RDMA direct bench | 单连接长传输约 30.35 Gbps；4×64 GiB 组端到端约 82.97 Gbps | 文件系统、manifest 和完整任务时间有相同速率 |
| V3 真实文件，源码 `78e1e4ab` | 64 GiB 下载 1/4 lane 各三次全部通过；task rate 中位数 1.831384 / 1.536711 Gbps | 100 Gbps 文件吞吐、持久化 fsync 验收、原 48 样本全量重跑或生产 readiness |
| V3 接收策略对照，源码 `d323f1fa` | 单 lane 64 GiB full/verified_chunks 各三次通过；task rate 中位数 1.836107 / 3.794028 Gbps；相同 checkpoint/commit-sync | 两策略写后复读故障覆盖相同、默认提速、四 lane 策略成绩、无 RNR/CQ 错误或生产 readiness |
| 顺序记录元数据，源码 `92f99af7` | 每版本 24 组测量；65536 条、every16 时查重扫描数归零；下载/上传元数据 wall 减少约 4.59%/4.84% | 文件或网络也提升同样百分比、任意乱序查找均为 O(1)、100G 或稳定性验收 |
| 清单序列化，源码 `791c2eee` | 每版本 24 组元数据测量，every16 下载/上传 wall 减少 49.36%/49.67%；真实 64 GiB 单 lane 各三次 task rate 中位数 3.843084 / 3.856495 Gbps | 文件吞吐翻倍、明显整任务提速、全缓存网络上限、RNR/CQ 修复或生产 readiness |
| 源供数工具，源码 `5b8be5af` | 同一 64 GiB 源的 12 次只读/CRC 测量及独立 SHA；纯读约 3.96-4.05 Gbps，扩大读块无收益；传输二进制未变 | RAID 硬件最大带宽、异步队列无收益、缓存原生文件成绩、文件新提速或 RNR/CQ 修复 |
| 小规模 tmpfs 文件，源码 `46be03da` | 1 GiB 单 lane，基线/候选各三次 verified_chunks 与一次 full 控制，全部校验通过；接收文件阶段约 17.8 / 17.7 Gbps，冷建连 task 约 3.05 / 3.10 Gbps；CRC 拼接复制取消但未证实净吞吐收益 | 64 GiB 盘上吞吐、持久化带宽、线速 steady rate、100G 软件能力或 CQ 稳定性通过 |
| 有界 payload 复用，源码 `7aab4e68` | 同源 1 GiB 各三次及一次 full 控制；候选分配 4 次、复用 17404 次，producer+lane CPU 中位数降 22.54%；文件阶段 17.83 / 17.77 Gbps，task 3.10 / 3.05 Gbps，未证实吞吐收益 | 全进程或双端总 CPU 降同样比例、文件提速、零拷贝、持久化带宽、100G 或 CQ 稳定性通过 |
| 接收端剖析，源码 `bdc82c13` | 同提交 on/off 交替各三次与 full 控制；每 GiB 两组各 17408 次 vector 分配，副本/移动约 38.164 ms，实际写约 233.351 ms；每端约 2.068 GiB RSS+1 GiB tmpfs，内存门通过 | 已优化接收端、覆盖驻留页探针等于传输吞吐、零测量开销、持久化/100G 或 CQ 稳定性通过 |
| 接收 span，源码 `c4c1b4a` | 1 GiB tmpfs 单 lane；三次候选/对照文件阶段中位数 19.38/17.56 Gbps，接收 CPU -9.35%；task 均约 3.05 Gbps；合并写独立对照无净收益；13 个正式样本通过并清理 | 整任务提速 10%、双端总 CPU 明显降低、零拷贝、full 策略重复收益、持久化/100G 或 CQ 稳定性通过 |
| 接收帧复用，源码 `5ef3c683` | 每 GiB DATA 分配17408→1；1 GiB tmpfs 各三次及一次 full 控制，文件阶段 19.21/19.82 Gbps，task 3.10/3.10 Gbps；七样本通过并清理 | 完整任务等比例提速、零拷贝、持久化/100G 或 CQ 稳定性通过 |
| 冷 RDMA 缓冲池，源码 `aa5efcc0` | 对齐映射＋进程内 THP 提示；1 GiB tmpfs 各三次和一次 full 控制，建连 2.102/1.058 s，task 3.104/5.315 Gbps（+71.25%）；文件阶段 19.925/19.170 Gbps；七样本通过并清理 | 文件处理阶段提速、长期稳定、持久化/100G；部分候选 CQ response/flush 增量更高且未解释 |
| 流水线交接/预分配，源码 c572b39 / 545b929 | 同源 1 GiB 四对唤醒、三对预分配共 14 正式样本通过；独立 pwrite CPU 归因；两种候选均降低吞吐；R1 557/557、R2 152/152 | 新提速、完整三阶段 worker 池、持久化/100G 或 CQ 稳定性通过 |
| 无额外复制 gathering，源码 `57502f30` | TCP 同源1GiB四对、task20.099/17.045 Gbps，候选CPU增加且拒绝默认推广；R1 568/568、R2 219/219，部分写后恢复/SHA通过 | 默认提速、持久化/100G、RDMA对照收益、完整独立三阶段worker池 |
| TCP range独立写入，源码 `60873ba0` | 同提交2/4 lane各四对task12.394/8.640→17.603/13.666 Gbps，双端CPU+4.39%/+46.22%；单lane控制20.022仍最快；29正式样本校验清理，R1 579/579、R2 241/241 | 单lane提速、CPU效率提升、持久化/100G、全局worker池完成或RDMA/CQ稳定性通过 |
| TCP lane复核与有界优化，源码590358f9/1d4b1cf8/201a43a0 | 107正式+5独立perf；c4两种写准入吞吐−28.10%/−7.10%，CPU下降；两候选no-go，R1 619/619、R2 236/236 | 已解决lane扩展、稳定单lane上限、持久化/100G、tmpfs争用是唯一瓶颈或生产readiness |
| 硬件拓扑 | 每台 R770 一张 100GbE/RoCE 端口 | 10×100Gbps 多链路或 1Tbps 扩展 |

稳态 payload rate、含建连/提交的 task rate、每 lane 吞吐分别记录，不合并计时口径。测试需固定提交、双端同配置构建，逐文件校验并保存 route、端口计数、CPU/RSS 和原始日志。

RDMA 后续任务现已暂停：[流水线优先决策](DECISIONS/2026-11-07-v3-pipeline-priority.md)覆盖此前“先排查 CQ、再扩大并发”的执行顺序。CQ/peer-mapping 仍未解释；[冷建连结果](tasks/2026-11-06-v3-rdma-startup-01-result.md)保留为历史证据，不作为本轮性能验收结论。

## 串行执行阶段

| 阶段 | 主要工作 | 决策门 |
|---|---|---|
| 0. E2E 基线 | 对照原生带宽、CPNetFlux direct bench、V3 真实文件；1/4 连接、4/64 GiB、至少 3 次重复；核实同一大文件是否真正并行 range | 完整性通过，稳定与 task rate 分开，失败可解释；完成后再定优化项 |
| 1. 瓶颈归因 | 分解建连、注册/清零、RQ/CQ、帧复制、checksum、调度、写入/writeback、manifest/final verify | 主要耗时可被重复样本定位，不凭单一计数器推因果 |
| 2. RDMA 资源与完成队列（暂停） | 测接收池预算、RQ/SQ 深度、补投递、CQ 批处理及完成等待；维持槽位所有权和有界反压 | 找到低内存且长传输稳定的窗口；若收益不成立，保留现状 |
| 3. 三阶段流水线（工程收口；文件写后端独立研究） | 显式四lane inline映射写通过错误/生命周期/恢复与完整任务A/B；下一步剖析新路径剩余CPU与等待 | 真实文件中位28.864 Gbps，30–50 Gbps门未过；默认pwrite/queued，实验入口显式，不扩线程/队列 |
| 4. 小文件自适应微批 | per-file 基线已完成；下一步先做 fixed microbundle，再在同矩阵比较 adaptive；测 4–256 KiB 与 1K–100K 文件规模 | fixed 先满足 T_ready 中位至少下降 20%、p95/恢复不恶化，再决定 adaptive |
| 5. 条件式服务率调度 | 只有观察到 worker 异质性、队列偏斜或拖尾时，比较 SR-Dispatch/BAP | 多次重复收益成立且无公平性、饥饿和恢复回归；否则不实现 |
| 6. 单链路稳定验收 | 冻结代码/配置/数据；双向 large/dense/mixed 重复测与超接收池长传输 | SHA 全过、CV ≤5%、无静默降级和未解释链路错误 |

每次仅改变一个主要因素。流水线队列有界；注册池、网络在途窗口和应用暂存分别记账。扩容缓冲本身不视作吞吐优化。微批需保留逻辑成员索引和成员级校验/恢复。RDMA WRITE、系统 MTU/NUMA 修改及 1Tbps 多端口设计均需独立决策。

## 分阶段目标

目标值用于计划与阶段门，不是当前成绩、工期承诺或保证值。真实文件需同时报告 payload rate 和从任务启动至校验提交完成的 task rate。

这些目标以源端供数与接收存储具有相应服务率为前提。当前 /dev/sda2 上的 64 GiB 单队列 POSIX 源读取约 4 Gbps，不能据此承诺该路径达到下表目标。100 Gbps 有效 payload 本身要求约 12.5 GB/s 供数；70-85 Gbps 也需要约 8.75-10.625 GB/s，尚未计入校验、复制和提交代价。全缓存/内存、当前存储路径和未来较快存储路径必须分别验收，commit_sync=none 不能当作持久化落盘带宽。

| 阶段目标 | 真实文件目标 |
|---|---:|
| 第一工程里程碑 | task rate 10–20 Gbps，且至少为同负载 post-RNR-fix 基线的 2 倍 |
| 中期大文件 | 单文件 4-lane range 并发 task rate 30–50 Gbps |
| 单端口冲刺 | 稳定 task rate 70–85 Gbps |
| 极限探索 | 85–90 Gbps，仅在测量与稳定性支持时 |
| 不承诺 | 应用有效吞吐 100 Gbps；或单端口实验证明 1 Tbps |

## 研究分支边界

开题中的三阶段流水线、SR-Dispatch/BAP、adaptive microbundle 是候选方案。批处理、文件聚合和流水线已有相关研究与工程实现；先完成 profiling 和 tuned-native/fixed 对照，再评估研究增量。小文件 T_ready 优先于只看 Gbps。CPSS(DCC) 独立管理，压缩不进入当前首轮传输热路径对照。

早期 S0-S8、Phase 和迁移路线已清理出当前工作树；相关实验结果若仍在 `docs/experiments`，按其输入提交和测量环境解释，不能自动继承为当前代码的 readiness 证据。


## 2026-11-08 V3 path isolation 收口

结果记录 tasks/2026-11-07-v3-path-isolation-01-result.md 完成了最后两轮共享 ready-writer pool 有界交叉实验。固定提交 aa4dc3e536a5cd259d423cf1e569517cb19b14bd、R770-1/R770-2 二进制和配置一致，使用 8 GiB 驻留 tmpfs 源、TCP 192.168.100.1↔.2、4 connections。候选 read=0/write=1 的中位吞吐 14.530 Gbps，相对基线 15.502 Gbps 为 -6.50%；候选 read=0/write=2 的中位吞吐 14.997 Gbps，相对基线 15.618 Gbps 为 -4.73%。两轮 CV 均低于 5%，正确性、独立 SHA、内存、swap 和清理门全部通过，但均未达到候选中位至少 +10% 的推广门。pool=1/2 虽把双端 CPU 分别降至约 23.71/26.75 CPU 秒/GiB，也缩短了累计范围写计时，端到端任务仍下降；因此两候选均 no-go，默认 read=0/write=0 保留。

**阶段 3 决策：停止继续扩大三阶段流水线、writer 并发和共享队列。** raw TCP 四流中位 89.469 Gbps 与 V3 四连接完整任务约 15 Gbps 的差距，继续支持瓶颈位于 V3 应用/接收提交路径。下一步必须新建独立 scope，选择内存 sink/稳态 payload 路径或小文件微批研究之一；不能把 CPU 降低单独当成吞吐通过，也不能据 tmpfs TCP 结果宣称 100 Gbps 或生产 readiness。RDMA verbs 路线继续暂停。

## 2026-11-08 memory sink / threaded 候选收口

[Memory Sink 01](tasks/2026-11-08-memory-sink-01.md) 已完成 21 个 8 GiB 内存 TCP sink 样本。1/4/8 connections 中位数为 27.4345/40.5639/40.2047 Gbps，CV 为 20.04%/2.95%/7.01%；只有 c4 通过稳定性门。4-connection sink 约为 raw TCP 四流 89.469 Gbps 的 45.34%，说明磁盘不是解释全部差距，增加 sink connections 也没有解决问题。

[Memory Sink Threaded 01](tasks/2026-11-08-memory-sink-threaded-01.md) 只增加显式 `--io-mode threaded`，epoll 默认和 V3 文件路径保持不变。相同 8 GiB、1 MiB、4 connections、7 对交错样本中，epoll 中位 42.1100 Gbps、CV 2.94%；threaded 中位 72.5311 Gbps、配对中位提升 72.242%，但 CV 23.23%，范围 42.98–85.10 Gbps，双端 CPU 2.78→3.94 秒/GiB。字节、返回码、内存、swap、route 和接口计数门均通过；候选因稳定性门和 CPU 代价 no-go。最高样本不能改写成稳定 100Gbps 或生产 readiness。

**阶段 3 和 memory sink 候选到达边界：停止继续扩大三阶段 writer、worker、队列、缓冲和 threaded 并发。** epoll 保持默认，threaded 仅保留为诊断代码；下一步按路线转入独立的小文件自适应微批 scope，或在有新假设和 CPU/调度证据后重新设计可控的服务率实验。RDMA、V3 默认文件路径和生产服务继续不改。


## 2026-11-08 V3 small-file per-file baseline

[Small-File Microbatch 01](tasks/2026-11-08-v3-small-file-microbatch-01.md) 的 50 个 V3 TCP 内存/受控 tmpfs 样本已完成。4/16/64/256 KiB 与 1K/10K 文件矩阵（256 KiB×10K 按门限改为 8K），并追加 4/16 KiB×100K；每格五次。全部样本成员计数、逻辑字节、树 SHA-256、V3 返回码和临时目录清理通过。密集 100K 文件的 T_ready 中位数为 33.608 秒（4 KiB）和 35.436 秒（16 KiB），对应吞吐 0.098/0.370 Gbps；tree_manifest_write_seconds 中位数为 23.431/24.907 秒，逐文件 wall 累计为 32.221/34.579 秒，说明清单快照和每文件状态/结果处理是下一候选的主要对象。最大 RSS 约 580 MiB、最低 MemAvailable 约 19.44 GiB、owned VmSwap 为零，但三次 16 KiB×100K 的 R770-1 主机 pswpout 非零，环境内存门标为部分未通过，候选 A/B 前需复核。实验大 JSON 已压缩清理为 972 KiB，未提交实验 payload。

Tree Manifest Coalescing 01 的两个候选均 no-go：32/256 事件阈值和 threshold=32 下的 max-delay 100/500 ms 都没有使 4/16 KiB×100K 的 T_ready 中位数达到 20% 降幅；快照数量约 300-350，说明全量清单序列化/原子写入是主瓶颈。两轮均保留了恢复、写失败和完整性语义，默认固定为 threshold=32/max-delay=100 ms。该 manifest/coalescing 线已关闭，后续只能转入其他有测量依据的路径或独立协议级小文件微批研究。

## 2026-11-08 V3 protocol fixed microbatch

[Protocol Microbatch 03](tasks/2026-11-08-v3-protocol-microbatch-03.md) 完成了现有
V3 per-file 协议 pending window 的受控评估。4/16 KiB × 100K 文件中，window=8
相对 window=1 的 T_ready 中位数分别下降 39.3% 和 41.1%，p95 同向下降，5/5
配对改善；成员计数、字节数、树 SHA-256、VmSwap 和清理门全部通过。新增显式
`--data-pending-window auto`：至少 10K 个且平均不超过 64 KiB 的 manifest 选择
窗口 8，其余选择 2；默认值仍为 2。该优化只针对密集小文件 V3 TCP 路径，不代表
大文件吞吐或 100 Gbps readiness。后续回到 raw TCP/V3 帧服务率归因和大文件路径，
不再扩大三阶段 writer/队列。

## 2026-11-08 V3 内存帧路径归因

[Memory Attribution 04](tasks/2026-11-08-v3-memory-attribution-04.md) 新增有界内存诊断工具，
复用现有 V3 FILE_BEGIN/DATA/FILE_END/FILE_RESULT 和会话校验，在等待接收端确认的相同
计时边界下比较 raw、V3-none、V3-CRC32C。四连接各五次、128 GiB 逻辑流的观察中位数为
87.392/88.014/88.514 Gbps，CV 为 5.844/7.348/6.281%，均未通过 5% 稳定性门。单连接
筛选分别为 38.213/38.673/35.718 Gbps，各只有一次，不是稳定性结论。18 个未插桩样本的
精确字节、接收确认、显式绑定、接口计数、内存、owned swap、pswpout 和清理门通过。

该诊断不含文件 IO、manifest、真实工作租约或 TLS，不能替代完整 V3 文件性能；它表明
现有帧/CRC 实现具有明显高于历史约 15 Gbps 文件任务的诊断吞吐，不能据此修改帧格式、
关闭 CRC 或宣称稳定 100 Gbps。本轮没有推广生产候选。下一步用相同 8 GiB 驻留 tmpfs 源
复核完整 V3 的 1/4 lane，定向分离队列交接、共享范围/checkpoint 和发布路径；继续暂停
三阶段扩张和 RDMA。perf 当前权限受限，独立 strace 样本只作调用数量证据，不计入吞吐。

## 2026-11-08 V3 完整文件路径简化

[Full File Path 05](tasks/2026-11-08-v3-full-path-05.md) 完成同版本复核和一个内联接收
候选。原路径 1 lane 五次中位数 21.154 Gbps，但 CV 11.271% 未通过稳定性门；4 lanes
为 15.059 Gbps、CV 2.042%。剖析显示细粒度队列交接伴随大量主动切换，累计写入耗时
显著高于 checkpoint；不能把全部队列背压解释成纯唤醒成本。

提交 df3ac592 在不改变 CRC、工作租约、恢复、检查点和发布语义的前提下，提供显式
`CPNETFLUX_RECEIVER_PAYLOAD_EXECUTION=inline`，让接收线程同步消费 DATA 并复用帧
缓冲。两端同提交、同构建配置和二进制哈希；4 lanes 五组交错 A/B 中，完整 8 GiB
tmpfs 任务中位数从 15.302 升至 23.801 Gbps（+55.54%），CV 1.707%/1.312%；两端
CPU sec/GiB 从 4.100 降至 2.421（-40.95%），客户端主动切换下降 74.60%，5/5 配对
改善。该受控四 lane 候选通过性能门，但仍未达到 30-50 Gbps 中期目标。

默认继续 queued：单 lane 内联筛选只有 15.537 Gbps、一次样本，提示潜在回退，不能
全局替换流水线。显式双帧写批和共享写池保留原执行路径。相关 CTest 在 R1 queued、
R1 inline、R2 inline 各 174/174 通过；23 个互传样本均通过 SHA、字节、TCP 路由/
接口计数与内存门。最低 MemAvailable 20.03 GiB，owned swap/pswpout 均为零，源和输出
payload 已清理，两端 /dev/shm 归零。实验是 commit_sync=none、TLS off 的 tmpfs 场景，
不是磁盘、持久化或安全配置下的生产吞吐。

下一步先补同提交 queued/inline 的 1/2/4 lane 五组配对，再隔离共享 inode 与独立文件
的 offset 写入服务率，确定执行策略和存储路径边界。保持已验证四 lane 优化可显式
使用；继续暂停三阶段并发扩张和 RDMA，不把单 lane 波动、局部优化或纯内存帧结果
写成 100 Gbps 已解决。

## 2026-11-08 V3 lane / storage 服务率复核

[Lane / Storage 06](tasks/2026-11-08-v3-lane-storage-06.md) 先完成同提交 queued/inline
1/2/4 lane 各五组配对，再新增 Linux-only `cpnetflux-offset-write-bench` 与有界 runner。
工具复用 PosixFile::writeAtAll，以 shared/independent 文件、1/2/4 worker 和
fresh/sparse/rewrite 三种初始状态隔离写入服务率。复核发现 V3 在写前 resize 到最终长度，
因此补测 sparse 并逐次断言初始逻辑长度 8 GiB、已分配块为零。两端构建/摘要一致、
各 7/7 相关 CTest 通过；smoke 包含 72 次成功调用、独立内容检查和短写失败/安全清理回归。

与 V3 初始状态匹配的 sparse 五次中位数如下，均通过 CV≤5%：

| 节点 | 共享文件 w1/w2/w4 Gbps | 独立文件 w1/w2/w4 Gbps |
| --- | ---: | ---: |
| R770-1 | 41.531 / 31.651 / 31.039 | 41.360 / 63.769 / 87.008 |
| R770-2 | 41.198 / 31.013 / 30.877 | 41.176 / 63.653 / 87.393 |

共享文件四线程相对单线程下降约 25%，独立文件可扩展至约 87 Gbps；这一损失无需 V3
队列、CRC、manifest 或网络即可复现。当前比较同时改变 fd 与 inode，具体内核机制
需结合独立控制或历史 perf 证据解释，不能只凭服务率表宣称已确认某一把锁。已填充页面
的 rewrite 独立写约 100 Gbps 是排除预填充成本的本地计时，不能算完整传输收益。
fresh/rewrite 边界组 23/24 格稳定，R1 fresh/shared/w4 CV 6.141% 保留失败；
sparse 匹配组 12/12 格稳定。全部 180 个写样本的内容/内存/swap/清理门通过。

最低 MemAvailable 20.026 GiB，诊断最大轮询 RSS 4.871 MiB（另计 8 GiB tmpfs），
owned swap 和主机 pswpout 均为零。源和输出全部清理，两端 /dev/shm 归零，生产服务未重启。
完整传输仍是 TLS off、verified_chunks、commit_sync=none 下约 24 Gbps；没有新增运行时
吞吐成绩，没有通过持久化、TLS-on、反向/混合负载或长时间稳定性验收。

下一独立任务先用“每 worker 独立打开同一 inode”补控制，再评估一个同文件写后端，
初始方向为有界 mapped-write 诊断。先定义映射生命周期、缺页/解除映射计时、ENOSPC/SIGBUS、
内存预算和清理，再做最多两候选；本轮尚未实现该后端。只有服务率中位至少 +10%、
CV≤5%、完整性/资源无回退后，才考虑 V3 集成及 CRC、恢复、租约、取消和原子发布回归。
最终仍须通过完整 8 GiB 任务 A/B，不能用诊断收益替代端到端门。

## 2026-11-08 有界映射写可行性

[Write Backend 07](tasks/2026-11-08-v3-write-backend-07.md) 增加 shared-open 描述符对照、
显式 mapped-write 诊断、每线程最多64 MiB窗口和系统CPU计时。映射/首次缺页/解除映射
均在写入计时内，没有预填充。8 GiB、65532字节帧、1 MiB chunk、1/2/4 workers，
每格五次；两端同提交、同配置、同二进制，串行完成120个正式样本，24/24格CV≤5%。

| 节点 | 四线程共享fd pwrite | 独立open同inode pwrite | 共享文件映射写 | 映射写CV |
| --- | ---: | ---: | ---: | ---: |
| R770-1 | 30.848 Gbps | 31.097 Gbps | 54.896 Gbps | 0.913% |
| R770-2 | 31.110 Gbps | 31.029 Gbps | 55.348 Gbps | 1.204% |

独立open的变化不足1%，不作为优化；独立文件对照仍约87 Gbps。四线程映射写提高约78%，
全部5/5配对改善，CPU/GiB从1.1108/1.1031降至0.6237/0.6177。单线程映射写约21 Gbps，
比pwrite慢约49%；双线程约33.3–33.5 Gbps、收益不足10%。仅四线程场景通过可行性门，
不全局替换pwrite，不自动选择，也不把55 Gbps写成完整传输成绩。

两端相关CTest各8/8，通过原有72和新增30次成功调用，并验证mmap失败退出2、真实SIGBUS
退出-7及父进程清理。最低MemAvailable 20.060 GiB，最大轮询RSS 248.820 MiB，另计8 GiB
tmpfs文件；映射RSS引用同一文件页，不重复计算成第二份8 GiB。owned swap/主机pswpout
均为零，全部payload清理，两端/dev/shm归零，生产服务未动。

下一独立集成先限于单任务下载CLI、POSIX direct、显式inline/四lane。窗口归属range-attempt，
沿用writing pin，回收/取消/发布前退役；不得使用全局共享窗口或thread-local fd缓存。
mmap错误返回Status，SIGBUS当前会终止子进程，必须验证对端EOF退出、无成功/发布和checkpoint
恢复；不能声称已有优雅恢复或直接放进共享服务端。完成这些行为门后，才进行五组8 GiB
完整任务A/B，以≥10%中位收益、CV≤5%及原CRC/SHA/租约/恢复/内存门决定是否保留。

## 2026-11-08 显式 V3 映射写集成验收

[Mapped Download 08](tasks/2026-11-08-v3-mapped-download-08-result.md) 完成上一节的集成与验收。
源码`535d33db5d38fe7e380d022e100195a94ec5e32e`已提交、推送并远端SHA回读，两端同配置、
同八个二进制SHA。R1最终相关105/105及认证/故障5/5通过，R2相关107/107通过。
1 GiB loopback故障验证ENOMEM退出1、真实SIGBUS退出-7、无残缺发布、对端EOF退出，随后
恢复与独立SHA通过，SIGBUS场景复用64 MiB。SIGBUS仍按隔离任务失败处理，不是优雅恢复。

五组8 GiB配对，四lane/inline、驻留tmpfs、TCP/TLS off、verified_chunks、commit_sync=none：
pwrite中位24.039570 Gbps，mmap中位28.864204 Gbps，提升20.069554%；CV 2.083093%和1.874964%。
双端CPU/GiB中位2.43750 -> 2.21375，接收端1.23375 -> 0.98375。所有10个任务完整性、路由/
接口计数、V3终态、资源和清理门通过。每个映射任务128次map/unmap，完成时活动映射为零；
每range最多64 MiB、四路活动窗口上限256 MiB，不改变帧缓冲预算。

最低MemAvailable R1/R2为20.552/20.003 GiB，最大轮询owned RSS为226.266/19.805 MiB，
另计tmpfs文件页；owned swap和主机pswpout增量均为零。源、输出及sidecar全部清理，两端
/dev/shm归零，生产服务PID及October 30启动时间保持。证据约9.5 MiB位于R1独立experiments目录。

只保留显式实验入口，不改变默认。下一独立scope先在新后端重新归因发送准备/CRC、接收帧/
CRC、Entry锁、checkpoint和等待，再选一个假设；不扩大三阶段结构，不把小文件微批替代
大文件吞吐门。单路同提交对照、反向、TLS-on、持久化、混合负载及长稳仍待验收。

## 2026-11-08 映射路径归因与缺页对照

[Mapped Profile 09](tasks/2026-11-08-v3-mapped-profile-09-result.md) 完成三组交错观测对照，
完整8 GiB任务轻量/详细观测中位28.222448/28.047271 Gbps，CV1.150489/2.088662%。
详细观测任务时间中位差约+0.625%，只代表本批三组；没有新的运行时吞吐候选通过性能门。
六次V3任务的CRC、独立SHA、终态、源驻留、TCP显式绑定/接口计数和资源门均通过。

接收端mapped writer跨range累计wall中位4.724 s，Entry等待0.965 s、checkpoint flush
0.212 s、frame CRC0.162 s；这些区间重叠，不得相加成任务wall或CPU比例。客户端系统态
CPU中位6.420 s，约211万次minor fault、零major fault。发送端producer/lane CPU仍约
4.633/5.077 s；producer等待含下游背压，不能全部归为唤醒开销。perf实际权限拒绝，
gprofng校准不可靠，因此未把其样本用作热点证据。

提交5a9f06a7扩展现有offset-write诊断，增加写入区间缺页计数及独立prefill wall/CPU，
不改变V3运行时。先验证旧二进制缺指标失败，再实现；两端相关CTest各4/4通过，八个原有
二进制及新增变动的offset工具摘要一致。R1三组四worker/shared/mmap的8 GiB局部对照中，
sparse/rewrite写入中位54.720/76.299 Gbps，CV0.346/0.262%，但计入serial prefill后的
两段合计时间是1.256/2.558 s；写入区间都仍有2097152次minor fault。已分配文件页不能
等同于已建立映射页表，局部76 Gbps不是V3吞吐，预填充暂不进入运行时。

下一独立scope只评估一个有界窗口内批量建立可写页表的候选，先核实支持、计入全部准备/
退役成本并通过本地内容/故障/资源与稳定收益门，再决定是否进入V3实验入口和五组完整任务
A/B。失败则关闭该假设，转向发送read/socket CPU剖析，不叠加worker/队列。100 Gbps、
70-85 Gbps及30 Gbps中期下限均未验收；三阶段结构扩张和RDMA优化继续暂停。

本轮12个样本最低MemAvailable约20.010 GiB，owned swap与pswpout增量均零；全部payload
已清理，两端/dev/shm归零，生产PID/启动时间保留。源码已提交推送并回读，证据在R1独立
experiments目录，未将payload或凭据提交Git。

## 2026-11-08 有界窗口批量页表建立：No-go

[Mapped Populate 10](tasks/2026-11-08-v3-mapped-populate-10-result.md) 完成上一节的单候选验证。
仅在本地offset-write诊断增加显式`--populate=write`，每个64 MiB mmap窗口立即调用
MADV_POPULATE_WRITE，全部准备/写入/退役均计时，默认lazy及V3运行时保持。错误不降级，
新增真实部分population后ENOMEM与截断后错误返回测试；两端最终相关CTest各4/4通过。

初版子区间getrusage CPU存在tick归因误差，改为CLOCK_THREAD_CPUTIME_ID后提交e61a5868，
两端重新同步构建、核对八个原二进制及offset/shim摘要，重复全部20个样本；原20个证据保留。
最终每端五组8 GiB、shared inode、四worker/sparse/mmap对照结果：

| 节点 | lazy局部写入中位 | population局部写入中位 | 中位变化 | CV lazy/populate |
| --- | ---: | ---: | ---: | ---: |
| R770-1 | 55.078 Gbps | 41.555 Gbps | -24.553% | 0.294% / 0.647% |
| R770-2 | 54.877 Gbps | 41.041 Gbps | -25.213% | 0.476% / 0.836% |

所有配对都回退，写线程总CPU约4.97-4.99 s上升到6.36-6.40 s，minor fault仍均为2097152。
只看较小的残余copy区间或系统态CPU会漏掉population本身和新增用户态成本。候选关闭为
no-go，仅保留诊断负对照，不进入V3集成和完整任务A/B。完整文件性能仍约28-29 Gbps，
本轮未新增吞吐成绩；局部55 Gbps不等同完整传输。

40个样本内容/内存/swap/清理全部通过，最低MemAvailable20.040 GiB、最大轮询RSS260.441
MiB，owned swap与pswpout增量零；全部payload已清理，两端/dev/shm归零，生产PID保留。
源码已推送并回读GitHub，独立证据位于R1 experiments的mapped-populate-10目录。

下一独立scope转向发送端read/CRC与socket CPU归因，分清condition等待、下游背压和逐帧
lease/dispatch成本，先量化观测开销再选一个假设。不继续叠加页表候选、worker或队列。
三阶段结构扩张和RDMA优化继续暂停，30 Gbps下限、70-85/100 Gbps及生产场景仍待验收。

## 2026-11-08 发送端 CPU 归因完成

[Sender CPU 11](tasks/2026-11-08-v3-sender-cpu-11-result.md) 在同提交`2bd8280e`、
同配置/八二进制摘要一致后完成三组8 GiB交错完整任务。原range/payload观测两组都开，
只切换新增发送端CPU时钟；base/CPU组task中位27.857/27.927 Gbps，CV1.169%/0.618%。
wall差异-0.249%在样本波动内，发送端进程CPU中位9.95→10.34 s(+3.920%)，不能宣称
零观测成本或新吞吐收益。默认关闭，观测关闭或接收端导出null。

发送端每8 GiB各包装区间的CPU中位：read2.621 s、checksum+combine1.475 s、push0.545 s、
pop0.392 s、prepare0.117 s、lease0.716 s、socket3.482 s。read+socket约占观测组发送端
CPU59%，但未定位到具体内核函数或锁；producer/consumer等待wall不能当CPU占比。
现有CRC矩阵已经缓存。实际库的单线程1 MiB热缓存诊断中，8 GiB逻辑帧的update/combine
分别约0.102/0.184 s，both约0.293 s，与并发任务存在未解释差距，不能直接预测提速。

6个任务的字节、V3终态、外部SHA、route/NIC、内存/swap与清理门全过；最低可用内存
19.988 GiB，最大轮询RSS258.617 MiB，owned swap/pswpout增量0。源与目标已删除，两端
/dev/shm归零、无测试传输残留，原生产PID/start保留。R1/R2相关CTest各198/198通过。
观测源码已推送/回读GitHub；原始证据、诊断代码和摘要在R1 experiments/sender-cpu-11。

本scope完成归因工具和观测开销复核，不冒称解决约28 Gbps上限。未启动五对吞吐候选门。
下一独立scope先隔离驻留源read、实际user/sys与线程调度、逐帧socket服务成本；必要时
在真实并发中继续拆分CRC update/combine。新候选仍需>=10%完整任务中位收益、CV<=5%
和完整性/资源门。禁止据局部CPU下降继续扩队列、删CRC/lease或重开页表population。

## 2026-11-08 发送端同线程候选：吞吐 No-go

[Sender Service 12](tasks/2026-11-08-v3-sender-service-12-result.md) 先完成18个本地供数
诊断：四lane同线程read+CRC服务率153.031 Gbps，经原depth2队列为128.185 Gbps；CPU
1.666→3.178 s，主动切换约13万次。该局部结果不含网络/真实文件完成流程。原guard未识别
外部诊断二进制，RSS0不作零内存结论；补time-v探针maxRSS5236 KiB、major/swaps0。

源码351ae7ce提供默认关闭的CPNETFLUX_SENDER_PAYLOAD_EXECUTION=inline，发送lane复用
单帧缓冲完成read/CRC/send，queued仍默认；两模式共用缺失区间、校验、压缩、租约、
恢复和终态路径。新增执行方式/CPU归属与零队列记账契约，未改协议或扩大预算。
R1最终inline相关CTest208/208，R2 default/inline各208/208；原旧队列正数断言的初次
201/202结果保留，已改为同时验证两种模式。含压缩和中断/服务重启/超时恢复。

两端同提交/配置/版本/八摘要后，五组8 GiB、四TCP lane、inline/mmap接收端、相同lean
观测的queued/inline任务中位28.756716/28.091585 Gbps，CV1.350980%/0.853702%，变化
-2.312962%，4/5配对回退。发送CPU9.96→5.91 s（-40.663%），双端CPU/GiB2.21375→1.73
（-21.852%），接收CPU仍约8 s。**候选未通过>=10%吞吐门，不推广默认。** CPU成本得到
改善不等同当前任务瓶颈解决，数据尚不能唯一判定下游限制或发送重叠损失。

额外两次开启观测的样本只验证实际CPU归属，不合并性能单元或声称零观测成本。共12个
完整任务SHA/字节/终态/route/NIC/资源/清理全过；最低MemAvailable20.003 GiB，最大
轮询RSS242.891 MiB，owned swap与pswpout增量0。两端payload清空、tmpfs0、生产PID/start
保留；原始证据和摘要在R1 experiments的2026-11-08-v3-sender-service-12目录。

发送端fusion/队列调优在此收口。下一独立scope保留queued发送端，针对接收socket、
映射文件处理与共享状态做匹配的关键路径对照，先区分下游上限和发送重叠损失，再选择
候选；已有不含真实文件/租约的帧内存高带宽不是这个匹配对照。30 Gbps下限、70-85/100
Gbps与生产验收仍未通过，RDMA和三阶段结构扩张继续暂停。
