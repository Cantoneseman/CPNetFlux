# V3 TCP range 独立写入与并发伸缩 01 结果

日期：2026-11-07。状态：本轮实现、回归、固定版本双机实验、内存与清理门完成；阶段3继续，100G与生产稳定性门未通过。

## 范围与版本

输入 `071619294c56ac6322cfe5736f943ca15703e2d9`；范围/旧行为基线 `8c2ea7bf04eca704129dd028286b0b58e805285b`；实现 `acda9ad35fcf94645d3c02665132b9ca8dc20c28`；计时契约修正及正式对照源码 `60873ba0dc21ca7d79236071c7250eac45029104`。这些提交均已推送已核实的 CPNetFlux GitHub remote 并回读 SHA。最终文档提交与远端回读另存外部 acceptance/final-backup.json，避免文档自引用 SHA。

R770-1 `/home/Su/projects/CPNetFlux` 单人串行开发；R770-2 同路径只同步已提交版本、构建与测试。本轮没有子代理、worktree、RDMA调优、系统网络或CPSS改动。保留原两份 Oct14 untracked 文档。

## 基线和实现

旧行为三次轮换顺序的1/2/4 lane task中位数为21.046/14.823/8.656 Gbps；2/4 lane 的 Entry 锁等待累计中位数为0.3885/2.3939秒，锁持有包含 pwrite。线程累计区间可以大于整任务 wall，不能相加或从 wall 直接相减。自适应计划实际为2 lane/4 ranges、4 lane/8 ranges，总文件仍为1GiB。

本轮把符合条件的、不重叠 range 的 POSIX direct 写入移出共享 Entry 锁；原始 writeAtAll 绕过有状态 BufferedFileWriter。Entry 内继续验证身份/边界、认领写入、更新 checksum/进度、记录 checkpoint 和发布。shared Entry 所有权固定 transaction/fd 生命周期；在途 slot 保持 active/incomplete，不能提前发布。失败或部分写不认证未完成字节；较高 attempt 在 condition variable 上等待该 slot 写入排空，醒来后重新验证 attempt，避免旧写覆盖重试数据。额外修复较高 attempt 改变原有 range 分区的问题。

新增 `CPNETFLUX_RANGE_DIRECT_WRITE=serial|parallel` 诊断开关。仅 POSIX effective Direct（含Auto/零buffer）允许 parallel；有缓冲/coalesced/io_uring 保留串行。当前保留 eligible parallel 默认及 serial 对照开关；不自动提高 lane 数，不改 full 校验、checkpoint、commit-sync 等策略默认。单文件单 lane 路径不受此开关影响。这里direct指应用不合并写策略，不代表O_DIRECT或绕过页缓存。

观测新增 parallel_direct_write_calls（尝试数，不是已认证进度）、reclaim_wait_calls/seconds。Entry hold 明确排除锁外存储写入和 reclaim condition wait；后者单列。旧 timer 别名保留，schema2字段为追加。锁等待下降本身不是性能验收。

## 正式同提交对照

固定 `60873ba0`，TCP R770-2→R770-1，显式192.168.100.2/192.168.100.1 bind，经eno17095np0并保留route/interface证据；TLS和compression均off。源/目标tmpfs，总量1GiB，CRC32C，verified_chunks，manifest/range checkpoint every16，commit_sync=none，direct，preallocate off，depth2，AvailableCapacity唤醒，receiver batch1，profiling/phase timing on。不是持久化带宽或原生verbs实验。

2/4 lane各四对，确定性交替串行/并行和lane顺序；另有3次单lane控制，共19个正式伸缩样本。两端每次互传前核对同提交、tracked clean、相同CMake配置/版本与八个二进制SHA256。构建为RelWithDebInfo。task为客户端启动至正常退出的外部wall，包含建连、文件处理、提交/终态及summary tree hash；不含源预热、服务启动、独立SHA和清理。

| lane | 串行 task Gbps 中位数 | 并行 task Gbps 中位数 | 中位数比值提升 | 并行 CV | 串行 CV |
|---|---:|---:|---:|---:|---:|
| 2 | 12.393777 | 17.603091 | +42.03% | 4.15% | 11.45% |
| 4 | 8.640418 | 13.666202 | +58.17% | 3.43% | 1.88% |

八对样本均更快；2 lane逐对提升24.50–50.81%，4 lane52.48–69.79%。报告比值为两组中位数之比，不是逐对比值平均。串行2 lane波动较大，样本短且小，不能外推稳态/长期或任意负载收益。新3次单lane控制中位数20.022165 Gbps、CV1.28%；单lane仍最快，旧单lane基线21.046不是本轮开关A/B，不据此声称单lane优化或回退。

| 指标，中位数/1GiB | 2 lane串行→并行 | 4 lane串行→并行 |
|---|---:|---:|
| task wall秒 | 0.695181→0.487985 | 0.994182→0.628684 |
| Entry累计wait秒 | 0.546742→0.013411 | 2.433629→0.030566 |
| Entry累计hold秒 | 0.574220→0.037069 | 0.852053→0.043439 |
| raw range write累计秒 | 0.498893→0.740508 | 0.723881→2.098304 |
| 接收进程CPU秒 | 1.340→1.440 | 1.840→3.105 |
| 双端进程CPU秒 | 2.280→2.380 | 2.910→4.255 |

Entry wait减少97.55%/98.74%，同时双端CPU增加4.39%/46.22%，接收CPU增加7.46%/68.75%。因此这是多lane task时延改善，**不是CPU效率改善，也没有提高当前最快单lane吞吐**。并发写入成本仍很明显，raw write累计区间与其他线程重叠；共享inode/页分配争用只是下一步待验证假设，未有kernel stack证据，不能直接下结论。保留parallel默认的依据是两种lane下所有配对均有净task收益且正确性/内存门通过；CPU代价使其不能充当自动增加并发的依据。

另一次4 lane、full策略控制通过完整任务/终态、CRC、写后校验和独立SHA，task0.781473秒/10.991979 Gbps；仅完整性门，不构成重复full性能对照。正式9个旧基线+19个同提交伸缩+1个full共29样本均通过，源物理read_bytes全为0，驻留页/源inode不变。每GiB实际17408个DATA；parallel所有range锁外写尝试17408，serial为0，范围总长度/写入字节均为1GiB，失败写为0。

## 测试与失败记录

- 10个upload/download参数化行为测试：独立range在另一pwrite阻塞时仍可完成；无提前发布；buffered/serial仍串行；真实1字节partial后EIO与高attempt排空/重试；禁止重试改分区。Linux测试链接包装pwrite只在测试目标fd安排调度/故障，不修改生产syscall。独立进展及分区漂移均先观察到有意义RED，再修正为GREEN。
- focused 36/36；6个直接写/attempt用例重复20轮均通过。完整V3恢复/lease/断连/校验/原子发布门保留。
- 首版全量576/578，两个旧观测脚本仍要求write包含在Entry hold内；60873ba修正per-mode契约，并新增serial观测门。4/4契约用例通过。
- 60873ba第一次全量578/579；reconnect smoke在FaultProxy bind阶段报Address already in use，未进入该用例传输。保留原日志，单独复测通过；完整重跑 **579/579，261.66秒**。未以删断言/跳过用例通过。
- R770-2扩大相关门含range smokes，**241/241，70.99秒**。两端八个二进制摘要一致。
- 旧基线的脚本适配探针不进入正式基线。候选初次merged-stream对照5个通过后，第6个服务端stdout长JSON与stderr协议trace交错，无法解析，该样本未做完整SHA验收并清理，整个该组仅保留为初步记录。外部harness改为分别捕获stdout/stderr，再完整执行新标签 `serial-isolated`/`parallel-isolated`/`parallel-c1-isolated` 19样本；正式表格只用新组。没有猜测重建损坏JSON、删除失败记录或混入初步样本。分流只改外部取证工具，正式串行与并行使用同一套工具。

验收命令（实际env包含临时token，不落仓库/日志）：

```sh
cmake --build build -j2
EVIDENCE=/home/Su/experiments/CPNetFlux/2026-11-07-v3-tcp-scaling-01
# validate.py: 临时token、显式bind、batch1、parallel环境，ctest -j1 --output-on-failure
python3 "$EVIDENCE/validate.py" 192.168.100.1 R1-final-full-recheck.log .
python3 "$EVIDENCE/validate.py" 192.168.100.2 R2-final-related-ctest.log 'Persistent|PayloadPipeline|GatheredWrite|FileIo|Work|RangeCheckpoint|unified|tree_.*smoke|range_.*'
python3 "$EVIDENCE/experiment.py" direct-crossover 60873ba0dc21ca7d79236071c7250eac45029104
python3 "$EVIDENCE/analyze.py"
```

上述日志/fixture名不可原地重用；复现应先复制harness并替换证据/fixture根，核对新两端同一提交/配置/八哈希，再seed新源。现有旧源已清理，不能直接运行覆盖旧receipts。

## 内存、清理、备份与证据

只保留R2一个1GiB源和R1当前输出；每轮SHA后删除输出和sidecar，checkpoint小副本留在外部acceptance。29样本的58个双端内存回执全通过；预检MemAvailable>=12GiB，实测最低29,111,398,400字节（约27.11GiB），owned RSS峰值14,651,392字节（约13.97MiB；不含tmpfs），tmpfs峰值1,073,786,880字节（约1.00004GiB/端）。owned swap均0、host pswpout增量均0。守护工具自身默认最低6GiB，所有采样仍高于本轮12GiB门，不能混淆阈值与观测值。

源SHA256 `5d645952dfd0cecff83991563a4d8e9b1fee7e2e3e8a2a8eb9b1090d600d8f64`。最终按R2源inode49/长度1073741824/SHA核验后，仅删除本任务精确文件和空目录；两端 `/dev/shm/cpnetflux-v3-tcp-scaling-20261107` 均不存在。R1/R2原生产PID126766/164834仍在；无本轮遗留server/client/sampler/guard，两端只有原GSI QP。没有清理其他实验、历史文件、项目或CPSS。

外部证据：`/home/Su/experiments/CPNetFlux/2026-11-07-v3-tcp-scaling-01`。包括脚本、metrics/analysis.json、comparison.json、逐样本verified.json/summary/time、stdout/stderr、route/interface/内存/源驻留页、RED/GREEN、全量/相关门和失败日志、matching-builds-*.json、checkpoint/清理回执、Git备份和HANDOFF。payload、构建、凭据不进Git；正式备份只认已提交并推送回读的内容。

## 下一步交接（尚未执行）

1. 沿TCP公共路径继续阶段3。先在同一小内存负载下区分接收writer系统CPU、每lane交接/唤醒、raw pwrite竞争、Entry短临界区和源端读/CRC服务率；补充低扰动阶段观测或独立pwrite调度探针，必要时分别核对profiling on/off。当前perf权限不足，不能冒称已有kernel火焰图，也不修改系统权限。
2. 依据上述归因选择一个因素：如同inode并发写争用成立，再设计按文件固定writer/有界写任务队列，独立控制网络lane与writer并发；如交接或源端限制占主导则优先处理它。不预先扩buffer/线程，不改变磁盘/校验策略求成绩。
3. 实现前保留总字节预算、range/attempt所有权和排空，先做确定性并发/partial/EIO/断连恢复测试；提交、推送、两端同提交八哈希后再做至少三次交替对照，task/CPU/内存/完整性一起验收。

当前已完成本轮“1/2/4伸缩归因→direct范围写锁拆分→安全测试→同提交配对→处置/清理”的闭环。全局独立三阶段worker池未实现，小文件微批阶段尚未开始；单端口100G与长期稳定性仍未验收。RDMA优化暂停，历史CQ/peer-mapping继续开放。
