# V3 Receiver Critical 13：接收路径修正与 CPU 归因结果

日期：2026-11-09。R770-1 单人串行开发，R770-2 仅同步构建与验收。
输入提交：`821afa721dc806fbb4efd92406998c48a18e093f`。
CPU 观测提交：`8f42c8513d0169bbb30a4f4036c2bb868f7f759c`。
修复及正式六样本源码：`127f4b66c9223b4366d00162c4b6115f51334a18`。
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`。

## 本轮结论

完成默认关闭的接收端线程 CPU 观测，并修复动态下载关闭全局调度时没有创建本地
WorkExecutor 的执行缺口。补齐后的六次真实文件任务全部通过。
关闭新增 CPU 计时的三次完整任务中位数为 **27.602 Gbps**，CV **1.141%**。
本轮没有通过新的吞吐提升门，不把语义修复或观测能力计作吞吐收益。
三阶段工程已收口，当前处在接收端处理路径归因阶段；30–50 Gbps、70–85 Gbps、
100 Gbps 完整任务目标均未通过。继续暂停扩大线程/队列和 RDMA 优化。

## 发现并修复的执行缺口

原 dynamic 下载分支只有在 `globalScheduler` 存在时才创建 incoming
DynamicFileQueue。因此性能配置 `--file-scheduling dynamic --scheduler off`
没有本地接收工作租约。发送端已有 V3 工作租约，V3 数据协议、文件内容和终态
校验均曾通过；不能把这一缺口描述为所有 V3 执行均不存在或已证实数据损坏。

首对 8f42c851 样本完成字节、SHA、终态和清理，但 cpu1 的新合同检查因
`local_work_executor=false` 失败。原矩阵立即停止，原两个 receipt 和日志保留；
未改写失败为通过，未把零租约计时解释成租约成本为零。

修复仅使 dynamic 下载无条件创建已有的有界 incoming 队列；全局 scheduler
仍保持 off，仅存在时才 attach。沿用 owner/epoch/attempt、deadline、已发起 IO
排空、结果预算和恢复逻辑。没有新增 worker、放宽租约或删除 CRC。
新 inline/queued 行为回归先复现缺口，再在修复后通过。正式 cpu1 三样本中，
每个 1 GiB range 均有 17,408 次 start/finish，两者平衡；整任务各 139,264 次。

## 观测边界与实验配置

`CPNETFLUX_RECEIVER_CPU_PROFILE=1` 且既有 phase/range 观测开启时，才启用
`CLOCK_THREAD_CPUTIME_ID` 计时。默认关闭；发送端及关闭时 JSON 为 null。
计数表示调用尝试，不能替代 certified progress。socket read 包含 FILE_END；
CPU 排除睡眠，包含计时本身的开销；begin/finalize/background 不包含在这些包装中。
physical write 包含于 range，range 又包含于 write，禁止三者相加。
queued 的 lane/writer/shared-pool 所有权与 inline lane 均有合同回归。

正式矩阵共三对，顺序 0/1、1/0、0/1，以 receiver13v2 新标签与旧失败路径隔离。
仅新增 CPU 计时开关不同；两组均开启既有详细 range/payload 观测，**不是 lean
默认配置或无观测吞吐**。配置为：

- R770-2 -> R770-1，TCP，显式 bind 192.168.100.2 / 192.168.100.1，
  route 和接口计数保留，接口 eno17095np0，无原生数据 QP。
- 单个 8 GiB tmpfs 驻留源，四个 TCP lane、八个 1 GiB range；压缩 off、TLS off。
- queued sender；inline receiver、实验 mmap 后端、64 MiB 窗口，约 256 MiB 活跃映射。
- read0/write0，CRC32C，verified_chunks，checkpoint16，commit_sync=none，preallocate off。
- task 计时包含客户端启动、实际文件任务和成功退出；独立 SHA 在计时之外。
- 每次全新输出，SHA 后删除；源物理读量均为零，源 inode/驻留和摘要一致。

源 SHA-256：`ab4f363f1ea39b8809a16fea140b6d843fd12f46b046864de9c5cd2463b7e563`。
源形状为同一随机 8 MiB 块重复，压缩关闭；不代表任意科学数据分布。

| 新增 CPU 计时 | 样本数 | task 中位数 Gbps | CV | task 中位数秒 | 接收进程 CPU 中位数秒 |
|---|---:|---:|---:|---:|---:|
| off | 3 | 27.602 | 1.141% | 2.489621 | 8.11 |
| on | 3 | 26.836 | 1.516% | 2.560762 | 8.44 |

新增 CPU 计时使中位 task wall 增加 2.858%，接收 CPU 增加 4.069%；配对吞吐变化
为 -4.855%、-3.171%、-0.124%。这是观察者开销的小样本结果，不是软件优化 A/B。
与旧 scope08/12 的 28–29 Gbps 不构成同语义配对比较；旧证据保持原配置边界。

## CPU 结果与下一步判断

以下为三个 cpu1 任务的线程 CPU 累计值的中位数。并发线程累计秒不能从 task
wall 直接相减；独立中位数之比只提供粗略占比，不能当成可消除的关键路径比例。

| 包装区间 | CPU 秒 | 解释 |
|---|---:|---|
| receiver lane 总量 | 8.140 | 分母为已观测 lane，不是整个接收进程 |
| socket read | 1.843 | 约 22.6%；包含实际 readNext 的内核与解析 CPU |
| frame CRC | 0.225 | 约 2.8%，不是当前首要优化对象 |
| frame state | 0.247 | 状态校验 CPU |
| local lease start/finish | 0.168 | 约 2.1%，不支持删减租约语义 |
| write 包装 | 5.457 | 约 67.0%；包含下面的 range 和 physical |
| range 包装 | 5.385 | 包含 physical；非 physical 部分约 0.702 秒 |
| physical mapped write | 4.686 | 约 57.6%；包括映射切换、缺页及 memcpy，尚未进一步分开 |

接收进程 minor faults 中位数约 2,111,482，而 8 GiB / 4 KiB 为 2,097,152 页；
major faults 为零。它支持继续调查首次触页/页分配，但不是“全部 CPU 都由缺页
造成”的证明。shmem THP 策略只读为 never；本轮未改系统策略、绑核或网络配置。
Entry lock wait 累计中位数 0.888 秒、hold 0.519 秒；它们是墙钟累计且有嵌套。
不能将之与 CPU 相加或据此重新认定同 inode 锁为唯一根因。

下一轮先在同一映射写入口增加可关闭的细分诊断，区分映射切换、首次触页、
memcpy 和内核分配成本，并与已失败的 scope10 population 候选对照，避免原样重试。
必要的局部诊断只用于解释服务成本，不能代替完整文件验收。只有发现能改变上述
主要成本的具体机制，才选择一个有界候选；任何直接接收进映射区的方案都需要先
写清缓冲所有权、CRC 失败、部分帧、range attempt 和恢复边界，再实施和测试。
不得通过预热输出、免除首次写入计时或取消校验来制造吞吐收益。
候选仍需五对完整任务 A/B、>=10% 中位收益和 CV<=5%，未过即停止推广。
本轮没有额外性能候选，也未重开更多 worker/queue、mapped population 或 sender inline。

## 验证、失败记录与资源

- 新 CPU 合同在输入二进制先红：missing receiver CPU observation contract。
- 最初观测回归曾错误假定所有 server upload receiver 都持有本地 ready；两项失败
  后明确导出 local_work_executor，并按实际上层协调所有权验证。保留原失败日志。
- 动态下载专项在修复前红：dynamic scheduler-off download lacks local V3 executor。
  修复后 inline/queued 2/2 通过。
- R1 / R2 各 **214/214 相关 CTest**，100.18 / 100.16 秒；包含队列/租约、写失败、
  映射恢复、进程中断、服务重启、超时恢复与压缩。这是相关子集，不是全量测试。
- R1 首次相关测试命令的末尾 tail 因 Windows 管道附加 CR 报错；CTest 本身已通过，
  原日志独立回读核验。R2 管道移除 CR 后正常退出。未把 shell 错误当测试失败或成功。
- 外部分析器首轮引用未导出的 record_update_seconds 报 KeyError，改为实际 schema
  并只配对已经完成的重复；最终六样本分析成功，未修改原始 timing/receipt。
- 127f4b66 双端同源码、构建选项、版本 0.1.0，八个二进制 SHA-256 一致后才互传。
- 六正式任务的字节、独立 SHA、V3 FILE_RESULT/DirectoryEnd、CPU 计数、路由/接口、
  无 QP、内存、无 swap 与输出清理全部通过。
- 观测到的最小 MemAvailable：R1 20.563 GiB、R2 19.987 GiB，均超过 12 GiB。
  owned VmSwap=0、host pswpout 增量=0；轮询 owned RSS 上限约 243 / 20 MiB，
  不把轮询值当作未采样时刻峰值，tmpfs 数据占用另计。
- 源经尺寸/SHA 复核后删除，输出逐次清理。最终两端本轮 tmpfs 根目录均删除，
  /dev/shm 使用量均为 0，owned 传输进程为空；生产 PID 126766/164834 和启动时间保持。
- 原两个 October14 未跟踪任务文件保留；未触碰 CPSS(DCC)、旧树或其他实验。

## 证据与复核命令

权威证据目录：`/home/Su/experiments/CPNetFlux/2026-11-09-v3-receiver-critical-13`。
重点文件：receiver-analysis.json、matrix.log（原失败合同）、matrix-v2.log、
metrics/*receiver13v2*.verified.json、acceptance/red-dynamic-download-executor.log、
acceptance/ctest-r1-executor-related.log、acceptance/ctest-r2-executor-related.log、
acceptance/matching-builds-receiver13-127f4b66c922.json、cleanup-R770-1/2.json、
backup-readback.json、provenance.json、evidence-sha256.json。
driver/analyzer/旧 harness 依赖摘要保留在 provenance；大 payload 不进入 Git。

```sh
cmake --build build -j4
ctest --test-dir build --output-on-failure -R 'Persistent|BoundedPayload|TransferStages|WorkExecutor|DynamicFileQueue|HotPathCompression|sender_(cpu|inline|execution)|receiver_cpu|dynamic_download_local_executor|range_.*observability|range_observation_export|mapped_download|tree_resume|v3_.*resume'
python3 /home/Su/experiments/CPNetFlux/2026-11-09-v3-receiver-critical-13/analyze_receiver_13.py
```

源码两阶段提交均已推送并回读 GitHub SHA；最终文档提交的 SHA、远端 SHA、源码祖先
关系与最终双端版本由 backup-readback.json 记录。正式任务固定使用 127f4b66。
反向、TLS-on、持久化、长时间稳定性及 RDMA/CQ 门仍未通过，本轮不作生产就绪声明。
