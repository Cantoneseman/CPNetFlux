# V3-RAW-PERFORMANCE-NEXT-PLAN-01

日期：2026-10-22
状态：前 4/8 项实现与本机回归验收完成；其余 4 项待串行执行；最终双机性能结论仍待第 8 项。
权威工作区：深圳 /root/projects/CPNetFlux
分支：codex/V3-DESKTOP-NETWORK-BINDING-01
输入代码：运行时基线 55f7a21bb970cc0528d21a52901627aa96660b44；初版计划 HEAD f7d2959；本轮细化基于文档提交 3a2ff9ccbfa2581a22f15356e7eaf94efb1f631a。
主要证据：2026-10-22-v3-gridftp-raw-stage-01-result.md。

## 目标和边界

用同一份无压缩实验矩阵，优先修复已证实的资源生命周期和小文件清单瓶颈，再减少摘要计算的额外成本，最后用单变量实验判断大文件数据通路的实际限制。所有优化须保留 V3 唯一运行逻辑、断点恢复和校验语义，并用真实运行数据判断是否有效。

本计划只涉及 CPNetFlux 后端资源生命周期、tree manifest/summary 与 V3 range 数据通路。桌面端 UI 可继续按 API 3 合同独立开发；本计划不改桌面界面。

非目标：启用压缩、改 GridFTP、调整系统 IP/路由/防火墙、配置或验收多物理网卡、改用 RDMA、宣称 100 Gbps readiness。报告的热缓存直连 R730→R750 数据不代表冷盘持续性能或深圳—上海链路性能。

## 报告结论

| 现象 | 证据 | 判断 |
|---|---|---|
| 2048 个 64 KiB 小文件明显慢 | CPNetFlux 进程中位 19.964 秒，GridFTP 1.958 秒，约慢 10.2 倍；发送端 manifest 阶段累计 77.530 秒、user CPU 约 20.31 秒；独立 strace 观察到 4098 次 rename | 已确认的主要热点：每次文件开始/完成都在全局锁下序列化并原子重写整棵 tree manifest。线程累计阶段值有重叠，不能相加当作 wall time。 |
| 2048 文件会碰到打开文件数上限 | 默认 RLIMIT_NOFILE=1024 时约传到 1000 个文件后失败；把上限临时提高到 8192 后完成，但接收端峰值达到 2064 个 FD | 已确认的资源生命周期缺陷，不能靠提高系统上限当成修复。已完成事务仍被批次审计对象保留，事务中的文件句柄没有及时关闭。 |
| JSON summary 令大文件进程退出明显变慢 | 4 GiB 测试的 core 结束到进程退出中位数约 11.8–12.0 秒；无 summary 的配对测试完整进程约 9.3 秒。computeTreeVerificationHash 固定使用软件 CRC32C | 已确认的进程尾部重扫成本。它不等于 V3 传输时间；优化后须分开报告传输时间和 summary/hash 时间，不能把计时边界改漂亮来制造吞吐提升。 |
| 大文件仍慢于 GridFTP | 去 summary 后，两个 4 GiB 样本慢约 1.46–1.49 倍；mixed 慢约 1.38 倍。每 lane pipeline 约 131064 bytes，观察到约 6.4 万次 producer wait；range 写的计时代码覆盖协调器锁和 checkpoint | 传输路径确有剩余差距，但锁竞争、流水线深度和校验各自造成多少影响尚未证实。先补可区分的测量，再逐项消融。 |

## 瓶颈、设计、改动位置和验收一一对应

| 瓶颈/现象 | 证据与确定程度 | 对应设计 | 主要改动位置 | 如何证明解决 |
|---|---|---|---|---|
| 2048 个小文件传输慢，客户端反复保存整棵目录清单 | 已确认：每个文件 begin/complete 都会在 manifestMutex 内更新记录并调用保存；全目录 JSON 序列化后原子替换。strace 观察到 4098 次 rename。它解释了 dense 的主要软件热点，但不能单独解释整个 19.964 秒。 | 状态更新只改内存并递增 generation；唯一后台写者合并多个 generation，序列化/rename 放在状态锁外。按“32 个状态事件或 100 ms”触发，结束、失败、取消时等待最新 generation 写出。严格单写者，避免旧快照覆盖新快照。 | src/core/io/tree_transfer_client.cpp、src/core/tree/tree_manifest.cpp；tests/unit/tree_manifest_test.cpp 及 tree transfer 集成测试。 | 记录事件数、快照数、锁等待和 wall；snapshot 数符合阈值，dense 达到目标且最终 SHA-256 相同。强制终止后重启，未落盘状态由 per-file checkpoint 和 resume 协商重建，不能信任旧的 Completed。 |
| 2048 个文件撞到默认 1024 FD 上限 | 已确认：批次 context.files 保留事务，已完成事务的 writer 和文件句柄也留到批次审计；提高 ulimit 只能绕过失败。旧实现的 1024 FD 回归测试复现了 manifest/source 无法打开。 | 已完成：成功发布和 committed checkpoint 后先销毁 writer，再关闭事务文件；审计保留轻量事务状态并按文件重新打开。失败/取消路径保持原有 lane 停止后释放生命周期，不在本项提前关闭。 | src/core/io/persistent_tree_transfer.cpp；tests/unit/persistent_data_session_test.cpp。 | 独立 CTest 进程将 soft RLIMIT_NOFILE 设为 1024，真实 socketpair 传输 2048 个 64 KiB 文件成功；context 存活时传输后目标目录 FD 为 0，审计后仍为 0。专项回归及全套 417 项 CTest 通过（1 项因 io_uring 环境条件跳过）。此项修复稳定性，不记作吞吐优化。 |
| 大文件的每个 4 MiB 已验证块都会保存 per-file checkpoint | 代码与阶段 4 实测已确认：512 MiB depth=2 样本 checkpoint 累计中位约 0.625 秒、Entry hold 0.961 秒，保存/rename 131 次；4 GiB pilot 1027 次。RangeFileCoordinator::write 到达 chunk 边界后调用 FileTransaction::recordRange；它 flush writer、更新 TransferSession，再无条件 flushManifest。因此即使实验配置 manifest-flush-policy=final_only，range 路径仍逐块原子保存 per-file manifest。4 GiB/4 MiB 约有 1024 个块/文件。 | 保留“数据先写完并 flush，之后才能把 checksum 记为已验证”的顺序；将 manifest 落盘从每块一次改成有界批次（先测每 1/4/16 块以及 100 ms 上限）。批次内只允许包含已经完成写入和 checksum 的块；文件提交前强制 flush。干净取消/失败按策略 flush pending，flush 失败不得回成功。 | src/core/io/persistent_tree_transfer.cpp、src/core/session/transfer_session.cpp、src/core/session/download_session.cpp；对应 session/checkpoint 测试。 | 增加 checkpoint 次数、序列化字节、flush 时间指标；比较每块/批次写法的吞吐和进程崩溃后的重传量。SIGKILL 后 resume、块校验、最终树 SHA-256 均通过。该原子 rename 不等于 fsync，不宣称可抵御断电。 |
| JSON summary 在传输结束后重读整棵树 | 已确认：tree hash 固定使用软件 CRC32C；4 GiB core 后到进程退出约 11.8–12 秒。 | 先让 summary 使用配置的 Auto CRC32C backend，并单独记录 summary_tree_hash_seconds 和实际 backend。再评估用已有的 per-file/chunk CRC32C 以 CRC combine 方式还原完全相同的旧 tree hash；只有覆盖、顺序、长度都能证明完整时才复用，否则明确 fallback 全量读取。路径/大小/内容的 hash 输入定义不变。 | src/core/io/tree_transfer_client.cpp、include/cpnetflux/checksum/checksum.h、CRC/tree summary 测试。 | 软件、硬件、Auto 对同一目录输出逐字节一致的 tree hash；涵盖空文件、嵌套/非 ASCII 路径、resume、已跳过文件和损坏输入。分开报告传输 core、摘要计算、完整进程 wall，不能把 summary 优化报成传输提速。 |
| 大文件吞吐落后约 1.38–1.49 倍，range/pipeline 的占比不明 | 原报告吞吐差距已确认；阶段 4 已补等待秒数与子阶段。深圳 512 MiB depth=2 样本 Entry wait 累计中位 2.455 秒、hold 0.961 秒，checkpoint 0.625 秒；pipeline 2/4/8/16 五组配对不支持加深默认。原报告 4 GiB 的各因素因果贡献仍需物理机器与后续消融确认。 | 先补每 lane 的 wait 秒数/队列水位、Entry 锁 wait/hold、数据写时长、块 checkpoint 写时长与次数；区分网络读取、CRC、socket/TLS、文件写入。基线采样后再单变量测试 pipeline depth 2/4/8/16。只有队列饥饿或抖动数据支持时才改默认深度。 | src/core/io/payload_pipeline.cpp、src/core/io/persistent_tree_transfer.cpp、src/core/io/tree_transfer_client.cpp、相应 metrics/tests。 | 观测开销先做校准；每项计数同时给累计时间和每 lane 分布。pipeline 候选至少 5 次配对样本，中位数改善 ≥10% 且内存有界才保留；仅 wait 次数下降不算成功。 |
| range 之间实际写入被共享锁和共享可变 writer 串行化 | 代码结构与阶段 4 的锁等待已确认；解除串行化的实际收益待阶段 6 消融。BufferedFileWriter 有自己的缓冲状态，TransferSession 的已验证块列表也不是并发安全的，不能只删 Entry 锁。 | 每个 range slot 拥有稳定生命周期、独立顺序校验/CRC、独立 writer buffer 和独立统计；不同 slot 对同一临时文件的非重叠 offset 使用明确偏移写入。Entry 锁只做租约/重叠/状态/attempt 校验；每 slot 保序。独立 checkpoint mutex 串行更新 session 和 manifest；提交屏障等待全部 active write/checkpoint 结束。新 attempt 必须等待旧 attempt 排空并校验 generation，禁止旧 lane 迟到写入。 | src/core/io/persistent_tree_transfer.cpp、src/storage/file_io.cpp、include/cpnetflux/storage/file_io.h；range/transfer 并发测试。 | 证明非重叠 range 的写入时间有真实重叠，锁等待下降且 4 GiB 中位数改善；重叠租约仍拒绝。测试并发 range、乱序/越界 frame、断线重试、旧 attempt、commit 屏障、SIGKILL/resume、最终 SHA-256；FileIoStats 不发生数据竞争。 |
| 锁和 pipeline 之后仍可能有 TLS、CRC、内存复制或线程/NUMA 热点 | 阶段 4 已有 producer/lane 线程 CPU 计时；深圳 perf 不存在，仍没有 CPU 栈，具体 CPU 热点属于未知项。短时 iperf 约 32.5 Gbps 与 V3 约 3.7 Gbps 表明软件路径值得分析，但不是同条件网络上限证明。 | 先用 Release 二进制采集 per-thread CPU、cycles、instructions、socket/NIC 字节和可用的 perf 栈；不可用时用轻量阶段计时替代。若 CRC 明显占 CPU，试用每 frame CRC 加 CRC32C combine 得到流 CRC，避免同一 payload 被完整扫描两遍；若复制/TLS 明显，再分别测 buffer ownership/record batching。只有 profile 显示 NUMA/调度问题时才加可回滚 affinity。TLS、校验和恢复始终开启。 | src/core/io/persistent_tree_transfer.cpp、src/core/io/payload_pipeline.cpp、checksum/TLS 热路径及对应测试；改动由 profile 决定。 | 对每个候选保留前后栈/CPU/吞吐证据和同一数据 correctness。profile 未证明的优化记为 no-go，不顺手加复杂度；不能通过关闭 TLS、CRC、resume 或最终验证取得数字。 |

## 串行开发任务

### 1. V3-FD-RELEASE-01：事务结束后释放文件资源（已完成）

FileTransaction::commit() 在输出发布、committed manifest 成功和临时路径清理后，先销毁 BufferedFileWriter，再关闭 PosixFile。批次 context 仍保留用于最终审计的身份/manifest 状态；审计按需只读重开输出，检查结束即关闭。

验收结果：新增真实 V3 persistent socketpair 测试，在独立 CTest 进程的 soft RLIMIT_NOFILE=1024 下传输 2048 个 64 KiB 文件。旧实现因 FD 累积而无法打开新 manifest/源文件；修复后传输成功，context 存活时目标目录 FD 数为 0，完整性审计通过且审计后 FD 数仍为 0。完整构建成功，全套 417 项 CTest 通过，1 项 io_uring 条件测试跳过。详情见 docs/tasks/2026-10-23-v3-fd-release-01.md。

本项只处理已成功提交事务的 FD 累积；失败/取消事务仍按既有 lane 生命周期，在对应写入停止、事务/context 释放时关闭，未新增提前关闭行为。该项是稳定性修复，不作为速度收益。

### 2. V3-SUMMARY-CRC-AUTO-01：先降低并准确报告 summary 成本

实现与回归验收完成，详情见 `docs/tasks/2026-10-23-v3-summary-crc-auto-01.md`。tree summary hash 使用配置的 CRC backend，并记录 `summary_tree_hash_backend` 与 `summary_tree_hash_seconds`；原 tree hash 字节定义、过滤规则及传输计时边界不变。深圳 smoke 对软件、硬件和 Auto 三种请求都得到相同的 `crc32c:3a314a68`，Auto 实际使用 hardware。

4 GiB 与 mixed 的收益尚未测量，不把小型 smoke 的单次时间视为性能结论；将在第 8 项配对矩阵分别报告 V3 core、summary hash 和完整进程 wall。CRC combine 复用已有完整 checksum 仍未实现：只有能证明字节覆盖完整才可做，否则保持全量读取。

### 3. V3-TREE-MANIFEST-COALESCE-01：避免每个文件都重写整棵树

实现与本机验收完成，详情见 docs/tasks/2026-10-23-v3-tree-manifest-coalesce-01.md。唯一后台写者按 32 个事件或最早事件等待 100 ms 取 generation 快照，序列化/I/O 在状态锁外；终态等待最新快照，保存失败返回错误并记录内存/已落盘 generation。100 ms 为触发等待窗口，不承诺慢盘持久化完成时限。

深圳本机 2048 × 64 KiB、5 组旧/新交错配对：tree manifest rename 从 4098 降为约 128–129；上传进程 wall 中位 5.4561→1.2447 s，下载 3.6279→1.1938 s，SHA-256 全部一致。此处两组均 TLS off，不代表 R730→R750/TLS/GridFTP 最终验收。上传/下载四个 SIGKILL 窗口与恢复、普通/全局调度的 Completed 核验、真实保存失败传播均通过；完整 426 项 CTest 无失败（1 项 io_uring 条件 skip）。文件格式和 per-file checkpoint 不变，不声称断电安全。

### 4. V3-RANGE-OBSERVABILITY-01：先把大文件阶段计时做对（已完成）

为每 lane 增加 pipeline producer/consumer wait 时长和高水位、源读取、CRC、frame/TLS/socket 发送、接收端 Entry 锁等待/持有、range offset 写、chunk flush、per-file manifest 序列化/rename 的次数和耗时。明确区分并发线程累计秒数与真实 wall；新增字段不能改变现有传输 elapsed 的起止点。

先测诊断开销，再用固定二进制/输入跑当前深度 2 与 4/8/16 单变量样本。同时取得每 lane CPU 和可用的 CPU 栈；若工具权限或符号缺失则如实记下，并使用轻量计时，不能伪造栈结论。

验收：新指标能回答“等待在哪个阶段、锁等了多久、checkpoint 花了多少时间、pipeline 是满还是空”。无明显观测开销，单变量试验至少 5 次，记录中位数和范围。未达 ≥10% 稳定改善的 pipeline 深度不升为默认。

实现与诊断验收见 docs/tasks/2026-10-23-v3-range-observability-01.md。
深圳 2 vCPU/3495 MiB RAM，保留两条 4 GiB TLS pilot；因输入超过缓存容量，
正式参数矩阵使用 512 MiB、40 次 TLS/CRC 传输和独立 SHA。每候选 5 组交错配对，
深度 4/8/16 process 中位数相对深度 2 约 +0.59%/-0.08%/-0.10%，均不达稳定 10%，默认仍 2。
已拆出 Entry wait/hold、offset write、chunk flush/CRC、checkpoint serialize/temp write/rename、
每 lane pipeline 等待和 CPU。512 MiB depth=2 样本中位 checkpoint 累计约 0.625 秒、
Entry hold 0.961 秒，支持先降低逐块保存频率的实验顺序。全套 432 项无失败、1 项环境 skip；
上海同提交构建与 18 项专项通过、二进制一致。perf 不存在，未采集 CPU 栈；
本项不替代最终物理机器对照。观测校准波动较大，未观察到持续减速，不宣称精确开销上限。

### 5. V3-RANGE-CHECKPOINT-BATCH-01：减少逐 4 MiB 保存检查点的成本

在观测基线后，对每个已完成 4 MiB 块仍先 flush 对应数据 writer，再把块 CRC 加入 session；只把 per-file manifest 原子落盘改为每 1/4/16 个块之一或 100 ms 到期时刷盘。候选 batch 大小不能越过已 flush 数据边界；最后提交、干净停止以及约定的错误路径强制 flush。一次 flush 失败不能对发送端报告成功。

验收：用 checkpoint 次数、写出的 manifest 字节、写盘耗时和吞吐对比策略；SIGKILL 后未落盘的块必须作为缺失块重传，不能误认为已验证。下载/上传、部分恢复、最终树 SHA-256 都通过。以不超过 100 ms 或候选 batch 上限控制进程崩溃后的重传窗口；不声称断电安全。只有中位数改善 ≥10%、内存/恢复成本在界内，才更改默认策略。

实现、安全边界和实验已完成，详见 docs/tasks/2026-10-23-v3-range-checkpoint-batch-01.md。
512 MiB TLS/CRC、20 次交错样本均独立 SHA 通过：batch4/16 保存中位从131降为39/17，
但 process wall 中位仅改善2.01%/1.31%，配对正负交错，未达到≥10%，**默认仍1块**。
保留1/4/16实验选项，不宣称大文件吞吐瓶颈已解决。
100 ms 空闲触发、范围/连接强制保存、保存失败粘性、上传/下载各batch4/16真实SIGKILL恢复均通过。
全套440项无失败、1项环境skip；两端同源码构建、关键二进制SHA一致；
80 MiB公网TLS/CRC上传下载均成功且三处SHA一致。该跨机样本仅验功能。
Entry hold 与累计 rename 耗时未随保存次数同比下降，下一项继续隔离共享writer/checkpoint，
仍需实际并发和配对实验，不把次数下降替代性能证据。

### 6. V3-DATA-PLANE-CLOSE-01：解除 range 数据写入的全局串行段

基于第 4 阶段证据实现：短暂 Entry state lock 检查 lease、range overlap、offset 和 attempt；随后每个 slot 在自己的 writer/checksum context 内顺序处理数据，不同 slot 的非重叠 offset 可并行写。独立 checkpoint mutex 只保护 TransferSession 记录与 manifest flush。提交阶段关闭新写入入口，等待 active write 与 checkpoint 清零，flush 完所有 writer，再做 coverage、验证、publish。任何 attempt 被接管前先确认旧 attempt 停止且代号匹配。

不要把 entry->mutex 简单拆成几个锁就认为完成：当前一个 BufferedFileWriter 与 session manifest 都有共享可变状态。先确认所选 FileIo backend 在并发 offset 写时的契约；每个 slot 的统计独立记录、完成时归并，不能并发写同一组普通计数器。

验收：并发测试证明多个不重叠 slot 确实同时进入数据写区；冲突 range 仍拒绝。覆盖旧 lease、写错误、取消、重复 FILE_END、连接恢复、checkpoint、最终输出校验。按固定 4 GiB 数据至少 5 次交错测试；中位数改善 ≥10% 且其他集合退化不超过 5% 才采用，否则以锁/CPU/checkpoint 新指标解释并停在更简单的实现。

### 7. V3-CPU-HOTPATH-01：仅按 profile 优化仍占主导的成本

按第 4 阶段栈样本选择一个因素：重复 CRC 遍历、TLS record/系统调用、payload 复制，或线程/NUMA 调度。每次只改一个因素。CRC combine 需要与软件参考实现对全部边界长度、切块顺序、短尾块、range resume 和损坏 frame 对照；buffer 改造要证明 checksum 输入未变化；调度亲和性必须能关闭并在双机上分别验证。

验收：每个改动至少 5 组相同条件配对结果、前后 CPU 栈和完整校验；没有重复改善就记 no-go。网络链路若显示为限制，另立链路任务，不在此阶段改系统网络配置或网卡绑定。

### 8. V3-RAW-STAGE-02：最终性能与正确性闭环

使用同一 R730→R750 热缓存数据、4 路配置、无压缩、TLS/CRC/恢复开启，对 2048 小文件、两个 4 GiB 文件和 mixed 数据做至少 5 次交错配对测试；重要最终门建议 10 次。报告 median、范围；少于 20 个样本时不把 p95 当作稳定尾延迟结论。每次外部 SHA-256 校验单列在传输 wall 之外，同时报告 V3 core、summary hash 和进程完整 wall。GridFTP 是参照而非功能等价物。

最终阶段目标：

- dense 128 MiB 的 V3 完整进程 wall 中位数 ≤4.9 秒（约不慢于本轮 GridFTP 的 2.5 倍），且 nofile=1024 下 2048 文件稳定完成。
- 两种 4 GiB 文件的 V3/GridFTP wall 中位比值 ≤1.25；mixed 比值 ≤1.25。该目标高于“比原版快一点”，但不宣称所有机器或冷盘都能达到。
- 任何正确性/恢复失败都是 no-go；性能目标没达到时，本阶段不能标成收尾，需按新增分解指标另制定下一项瓶颈任务。
- 正式样本开始前确认两端运行同一源码提交、分别构建，并核对构建产物 SHA；性能结果同时保留输入目录 hash 和运行配置。

截至2026-10-22还剩3个执行门；该状态由2026-10-23路线更新替代。第5项合并机制已实现并验证安全性，性能候选默认no-go，保留逐块默认。第 1–4 项 FD 生命周期、summary CRC、tree manifest 合并、range 观测均已实现并验收；第 4 项已以观测验收完成和 pipeline 深度候选 no-go 收口；第 6 项必须基于第 4 项证据实施，不强行加复杂度。

## 执行和交付约束

- 按以上任务串行执行，一次只改一个性能因素。每个实现/实验任务单独记录输入提交、明确文件范围、相关 CMake/CTest 命令或实验命令、输出证据和下一步。
- 深圳 /root/projects/CPNetFlux 是唯一 Git/构建工作区，沿用当前分支并保留已有未跟踪 worker-state 文档。每阶段只提交本阶段明确文件，推送到已核实的 origin 并回读远端 SHA。
- 任何跨服务器传输测试之前，先把同一提交同步到深圳和上海、两端构建，再比对源码归档和关键二进制 SHA-256；构建未完成或 SHA 不同不开始传输。
- GridFTP/CPNetFlux 对照复用相同输入、接收策略和阶段定义；不把 iperf 短探测、热缓存结果或线程累计秒数解释成持续物理链路上限。
- 截至2026-10-22还剩3个执行门；阶段6 no-go及后续路线见文末2026-10-23更新。per-file checkpoint 合并已实现、恢复正确性验收通过，但本机候选不达10%收益，默认不启用。FD 释放、summary CRC、tree manifest 合并、range 观测均已实现。第 4 项已以观测验收完成和 pipeline 深度候选 no-go 收口；第 6 项必须基于第 4 项证据实施，不强行加复杂度。


## 2026-10-23 路线更新

第6项 V3-DATA-PLANE-CLOSE-01 按原10%门槛判定no-go，候选已撤下，详见 docs/tasks/2026-10-23-v3-data-plane-close-01.md。虽然Entry锁等待大幅下降，4 GiB传输核心仅快0.52%、完整wall没有改善，所以先不做原定的CPU hotpath猜测性优化。

下一项改为 V3-SUMMARY-CRC-REUSE-01：4 GiB的完整客户端进程约72.34秒，末尾重新读取文件生成摘要占约25.39秒。传输期间收集已经读取/验证的CRC32C块，只有块offset/长度证明文件完整覆盖且文件身份未变化时，按顺序combine生成与原tree_hash字节定义完全一致的摘要；任何洞、跳过/恢复状态缺失、失败、文件变化或不完整覆盖均回退原来的全文件读取算法。协议、TLS、CRC校验、恢复语义和计时边界不变。范围及验收写于 docs/tasks/2026-10-23-v3-summary-crc-reuse-01.md。

验收需覆盖CRC combine参考对照、空/短/边界/多块及尾块、上传和下载fresh/resume/skip/文件变化与回退，输出tree_hash逐字节相同、最终SHA正确。4 GiB至少5组交错，完整进程中位改善≥10%，dense/mixed退化≤5%，再采纳。阶段8仍是R730→R750/GridFTP最终对照。当前还剩2个开发门：摘要CRC复用和最终双机验收；物理多网卡验证继续暂缓。CPU profiler缺失时不声称完成调用栈分析，也不以猜测替代已实测摘要成本。
