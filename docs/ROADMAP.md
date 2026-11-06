# CPNetFlux 性能路线图

更新日期：2026-11-07
最近原生文件实测源码：`791c2eee785d0c197f8ff86d902f6ce8688112c8`（清单序列化优化），同组观测基线为 `d0d98c74b15b728cf2853ec168c26a7a3cc27c70`；最新源供数工具源码为 `5b8be5af0c034451c117f372e346c049228299cc`，其六个传输相关二进制与前者相同。接收策略对照源码为 `d323f1fa442071bee64650c44c96d8a11ba42b1f`。原 48 样本和 result 生命周期复测仍按各自输入提交解释。
此前接收 span 的小规模 tmpfs 原生文件优化源码：`c4c1b4a134662a740e438da2f34b8aceea7d24fe`（接收 logical span），对照/写合并配置为 `a9ecbe9179c3d8154a91e17fe23b4c9d445f5a74`。此前接收端剖析为 `bdc82c13`、payload 复用为 `7aab4e68`、基线 `6025f54e`，CRC 拼接优化为 `46be03da`。上行所述 `791c2eee` 仍是最近 64 GiB 盘上负载源码，两类负载分别解释。
当前开发分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`
最新非 RDMA 软件路径实测源码：`57502f309cbcde56e2e25f1cd49bf2b0da046b8d`；[有界无额外复制聚合写入](tasks/2026-11-07-v3-gathered-write-01-result.md)在 TCP 同提交四对对照中无净收益，默认 batch1。单 lane 1GiB tmpfs verified_chunks/commit-none task 中位数20.099 Gbps，候选17.045 Gbps；不与旧 RDMA 冷池数据做提速比。交接/预分配负结果保留，默认容量可用唤醒、depth2、preallocate off。

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

当前下一步：按[内存实验决策](DECISIONS/2026-11-06-v3-small-cache-experiments.md)继续采用内存源/目标、单 lane、1 GiB、一个输出和内存/交换分区守护。[流水线 02](tasks/2026-11-07-v3-pipeline-02-result.md)已完成：writer 主要 CPU 位于实际 pwrite kernel 路径；排空唤醒减少等待却降低吞吐，整文件预分配把 write wall 约 0.359→0.194 秒，但额外分配约 0.255 秒，完整 task 5.587→5.248 Gbps。两种默认均未改变。下一步评估现有 ownership 预算内连续 offset 的无额外复制 gathering write，先保证部分写/错误/断连排空/verified chunk checkpoint，再同提交与 direct pwrite 对照；不能重做带复制 coalescing 后只看调用数。固定版本、恢复、原子发布和清理门保持。RDMA CQ/RQ/SQ/QP、peer-mapping、磁盘继续独立，整体性能门和 100G 门未通过。

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
| 硬件拓扑 | 每台 R770 一张 100GbE/RoCE 端口 | 10×100Gbps 多链路或 1Tbps 扩展 |

稳态 payload rate、含建连/提交的 task rate、每 lane 吞吐分别记录，不合并计时口径。测试需固定提交、双端同配置构建，逐文件校验并保存 route、端口计数、CPU/RSS 和原始日志。

RDMA 后续任务现已暂停：[流水线优先决策](DECISIONS/2026-11-07-v3-pipeline-priority.md)覆盖此前“先排查 CQ、再扩大并发”的执行顺序。CQ/peer-mapping 仍未解释；[冷建连结果](tasks/2026-11-06-v3-rdma-startup-01-result.md)保留为历史证据，不作为本轮性能验收结论。

## 串行执行阶段

| 阶段 | 主要工作 | 决策门 |
|---|---|---|
| 0. E2E 基线 | 对照原生带宽、CPNetFlux direct bench、V3 真实文件；1/4 连接、4/64 GiB、至少 3 次重复；核实同一大文件是否真正并行 range | 完整性通过，稳定与 task rate 分开，失败可解释；完成后再定优化项 |
| 1. 瓶颈归因 | 分解建连、注册/清零、RQ/CQ、帧复制、checksum、调度、写入/writeback、manifest/final verify | 主要耗时可被重复样本定位，不凭单一计数器推因果 |
| 2. RDMA 资源与完成队列（暂停） | 测接收池预算、RQ/SQ 深度、补投递、CQ 批处理及完成等待；维持槽位所有权和有界反压 | 找到低内存且长传输稳定的窗口；若收益不成立，保留现状 |
| 3. 三阶段流水线（当前） | 按 profile 解耦读/checksum、网络收发、接收校验/写入/提交；比较有界暂存和现有 bounded pipeline | 端到端收益、RSS 预算、恢复与尾包语义均通过 |
| 4. 小文件自适应微批 | 对比优化后的逐文件基线、固定微批、自适应微批；测 4–256 KiB 与 1K–100K 文件规模 | 改善完整 T_ready 和 p95，成员 checksum、原子提交、独立恢复通过 |
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
