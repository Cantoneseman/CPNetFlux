# CPNetFlux 性能路线图

更新日期：2026-11-06
最近原生文件实测源码：`791c2eee785d0c197f8ff86d902f6ce8688112c8`（清单序列化优化），同组观测基线为 `d0d98c74b15b728cf2853ec168c26a7a3cc27c70`；最新源供数工具源码为 `5b8be5af0c034451c117f372e346c049228299cc`，其六个传输相关二进制与前者相同。接收策略对照源码为 `d323f1fa442071bee64650c44c96d8a11ba42b1f`。原 48 样本和 result 生命周期复测仍按各自输入提交解释。
当前开发分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`

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

当前下一步（11/6 用户调整）：按[小规模缓存实验决策](DECISIONS/2026-11-06-v3-small-cache-experiments.md)，优先用 1 GiB tmpfs 源/目标建立原生 V3 软件路径对照，记录页驻留、源物理读取、任务/文件阶段、CRC/SHA 与终态；按测量选择单项软件优化，实验后清理本轮数据。原先“先做 io_uring 磁盘队列、再做 64 GiB 原生重复”的执行顺序失效，磁盘队列调查延后，历史 64 GiB 结果保留为独立负载。本轮 tmpfs 和 commit_sync=none 不代表持久化存储吞吐。现有 io_uring 每次 readAtAll 创建/退出 ring，不等于持久预读；现有发送 pipeline 深度 2、预算 131064 字节，不据此直接扩容。checkpoint 频率和共享锁仍是候选，CQ/RNR 稳定性按独立门继续调查。任务见 [Cache Path 01](tasks/2026-11-06-v3-cache-path-01.md)。

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
| 硬件拓扑 | 每台 R770 一张 100GbE/RoCE 端口 | 10×100Gbps 多链路或 1Tbps 扩展 |

稳态 payload rate、含建连/提交的 task rate、每 lane 吞吐分别记录，不合并计时口径。测试需固定提交、双端同配置构建，逐文件校验并保存 route、端口计数、CPU/RSS 和原始日志。

## 串行执行阶段

| 阶段 | 主要工作 | 决策门 |
|---|---|---|
| 0. E2E 基线 | 对照原生带宽、CPNetFlux direct bench、V3 真实文件；1/4 连接、4/64 GiB、至少 3 次重复；核实同一大文件是否真正并行 range | 完整性通过，稳定与 task rate 分开，失败可解释；完成后再定优化项 |
| 1. 瓶颈归因 | 分解建连、注册/清零、RQ/CQ、帧复制、checksum、调度、写入/writeback、manifest/final verify | 主要耗时可被重复样本定位，不凭单一计数器推因果 |
| 2. RDMA 资源与完成队列 | 测接收池预算、RQ/SQ 深度、补投递、CQ 批处理及完成等待；维持槽位所有权和有界反压 | 找到低内存且长传输稳定的窗口；若收益不成立，保留现状 |
| 3. 三阶段流水线 | 按 profile 解耦读/checksum、网络收发、接收校验/写入/提交；比较有界暂存和现有 bounded pipeline | 端到端收益、RSS 预算、恢复与尾包语义均通过 |
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
