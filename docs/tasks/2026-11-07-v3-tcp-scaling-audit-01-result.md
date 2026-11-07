# V3 TCP lane 扩展复核与有界优化 01：结果

日期：2026-11-07。状态：测量复核和两轮候选均完成；**吞吐问题未解决，两候选 no-go，停止继续扩大本阶段架构。**

## 结论与边界

用户批准的“一轮复核、最多两轮有明确假设的候选”已经执行完毕。固定版本的 1/2/4 lane 复核仍显示增加 lane 后吞吐下降。CPU 周期采样定位到同一 tmpfs 文件的 pwrite 内核写锁争用；限制同文件物理写入并发虽然减少 CPU，却降低完整 task 吞吐。它是已测到的重要 CPU 成本，不能据此宣称是全部 wall 瓶颈，也不能把 tmpfs 结果外推到真实磁盘。

保留 eligible POSIX direct 的 parallel 默认；file_serial/file_dual 仅保留为显式诊断开关，不推荐用于性能部署。本轮不追加第三候选，不加深队列、不扩大全局 worker 架构。三阶段工程解耦/有界性已有交付，吞吐门继续未过。30–50、70–85 和最终 100Gbps 目标均未达到；本轮不是生产 readiness 或单端口线速验收。

范围、验收和候选设计见[任务记录](2026-11-07-v3-tcp-scaling-audit-01.md)，语义边界遵循 [RESEARCH_BASELINE](../RESEARCH_BASELINE.md)。

## 固定版本与实验条件

| 角色 | 提交 |
|---|---|
| 输入 | 6e1b7534e14f6d6ec11ad6d10bc84a3ee599849e |
| 测量复核 | 590358f9da439b7f9e10dd161a722a98bdcc04cd |
| 候选1：file_serial | 1d4b1cf8fbac971990dcfade4e49d3f953b0b45f |
| 候选2：file_dual，最新核心代码 | 201a43a047102dfe52b1f05ec4497181dd64f6bb |

各提交都已提交、推送到已核实 GitHub CPNetFlux remote，并回读远端 SHA。R1 origin 为 git@github.com:Cantoneseman/CPNetFlux.git；R2 为同仓库 HTTPS URL。分支沿用 codex/V3-GRIDFTP-OPTIMIZED-RERUN-01。每次互传前均核对两端同提交、tracked clean、相同配置/版本和八个二进制 SHA-256。最终文档提交和双端同步回执留在证据根 acceptance/final-backup.json、matching-builds-final-handoff.json。

- 单人串行在 R770-1 /home/Su/projects/CPNetFlux 开发；R770-2 同路径只同步构建/测试。两个原有未跟踪 worker-state 文档保留。
- RelWithDebInfo，版本0.1.0；TESTS/RDMA/IO_URING/ISAL/TLS=ON，DESKTOP/CROSS_DOMAIN_TESTS=OFF。构建启用 RDMA 不等于本轮使用 verbs。
- 本轮仅 TCP V3 下载：R2 192.168.100.2 → R1 192.168.100.1，客户端显式 local-bind，服务端显式 host；eno17095np0 的路由、socket 和接口计数在每次前后保留。管理走免密 SSH r770-1/r770-2，没有修改网络、内核、NUMA 或 MTU。
- 同一 1GiB tmpfs 源，8MiB 随机块重复128次，compression off；发送前确认驻留，发送进程 read_bytes 增量最大为0。每次创建一个新输出，完整任务后外部 SHA-256 再删除。
- 4MiB logical chunk，64KiB frame buffer，depth2、batch1、AvailableCapacity、preallocate off、POSIX direct。默认 read0/write0；仅等价负对照 c1 使用 read4/write1，实际无需额外准入或 writer 池。
- 性能矩阵为 verified_chunks、CRC32C、every_n_chunks/16、range checkpoint batch16、commit_sync=none，TLS off、anonymous。另有每候选一次 full 控制，不与性能矩阵混算。默认 full 策略没有改。
- 每格7次，配对交替或轮换顺序；task rate=逻辑字节×8/外部客户端进程 elapsed，包含建连、准备、传输和终态，外部 SHA 不在计时内。CPU 为双端进程 user+sys 秒/GiB，非线程等待时间相加。CV 按 analyze.py 的整组标准差/均值（statistics.pstdev，ddof=0）；中位数作吞吐比较。
- 性能门要求稳定性 CV≤5%，候选 task 中位数至少+10%且超过配对波动，完整性/恢复/内存无退化；CPU单独下降不算吞吐通过。

证据根（R1主归档）：/home/Su/experiments/CPNetFlux/2026-11-07-v3-tcp-scaling-audit-01。R2保留本端原始证据。共 **107个正式通过样本、5个独立perf诊断通过样本、2个排除记录**。107=49复核+36候选1（含full1次）+22候选2（含full1次）；5个perf样本不进入吞吐统计。

## 测量复核

源码590358f9，每格n=7。完整观测保留 receiver/range 详细计时；lean关闭这些详细计时，仍保留 phase timing、资源guard和进程采样，两类单独解释。

| 组别 | lanes | task中位数Gbps | CV | 双端CPU秒/GiB |
|---|---:|---:|---:|---:|
| 完整观测，默认 | 1 | 19.254 | 9.04% | 1.30 |
| 完整观测，默认 | 2 | 18.494 | 5.10% | 2.27 |
| 完整观测，默认 | 4 | 14.038 | 2.07% | 4.12 |
| 完整观测，read4/write1等价负对照 | 1 | 16.532 | 16.25% | 1.41 |
| lean，默认 | 1 | 19.626 | 17.20% | 1.24 |
| lean，默认 | 2 | 17.582 | 7.59% | 2.36 |
| lean，默认 | 4 | 14.629 | 3.73% | 4.04 |

c1默认和read4/write1走同一有效执行路径，却仍有明显差异和较大CV；不能把该差异解释为并发参数的因果收益。关闭详细计时后 c4 退化仍存在，不能仅归因于计时探针本身。c1稳定性门未过，也不能把约19Gbps称作精确软件上限。单lane whole-file 与多lane range路径不同，残余差异尚未被完全分解。

## 剖析与两轮单因素候选

普通perf被perf_event_paranoid=4拒绝；发现既有sudo -n perf可用后，仅剖析本轮新启动的客户端，没有修改权限配置。采样为199Hz cycles及DWARF调用链；报告使用self overhead。以下比例是短时CPU周期样本占比，**不是任务wall占比**。

默认c1热点为crc32cCombine 14.52%、_copy_to_iter 12.62%、copy_folio 12.06%、shmem_add_to_page_cache 11.08%。c2出现rwsem_spin_on_owner 31.29%；c4的osq_lock 36.12%与rwsem_spin_on_owner 22.86%合计58.98%。调用链为：

osq_lock → rwsem_down_write_slowpath → down_write → shmem_file_write_iter → vfs_write → pwrite64 → storage::writeAtAll → FileTransaction::writeRange → RangeFileCoordinator::write。

**候选1 file_serial**：每文件Entry独立IO mutex，仅包住实际物理writeRange。等待不持Entry，旧slot/attempt pin保留；释放IO门后才重入Entry。不同文件独立，不加payload拷贝、worker或队列。假设是消除同inode内核争抢会提高吞吐。

**候选2 file_dual**：最后一轮候选，每Entry固定2许可的counting_semaphore。保留4条网络lane、Entry外IO和独立文件；RAII保证失败/异常归还许可，等待时仍pin旧slot。假设是保留两个活跃写入可减少完全串行的交接成本，同时缓解四路争抢。

每行是同一提交、同配置的7对交替对照；不同候选的baseline分别重测，禁止跨提交挑最好成绩：

| 候选/lanes | baseline→候选Gbps | 中位数变化 | baseline→候选CV | 双端CPU秒/GiB | 处置 |
|---|---:|---:|---:|---:|---|
| file_serial / 2 | 17.223→13.813 | −19.80% | 4.17%→6.22% | 2.36→2.05 | no-go，7/7对负收益 |
| file_serial / 4 | 14.375→10.335 | −28.10% | 1.77%→7.78% | 4.11→2.68 | no-go，7/7对负收益 |
| file_dual / 4 | 14.064→13.066 | −7.10% | 2.91%→8.10% | 4.11→3.43 | no-go，6/7对负收益 |

file_serial的c4 CPU减少34.79%，file_dual减少16.55%，但吞吐下降且候选CV均未过门。file_dual第7对约+4.96%不能覆盖前六对负收益或算晋级。每轮c1控制分别17.645Gbps/CV12.57%、18.853Gbps/CV9.30%，仍显示短任务波动。

独立perf中，file_serial的主要rwsem/osq热点退出≥0.5%报告，copy_folio 16.89%、crc32cCombine 13.04%成为前列；file_dual仍有rwsem_spin_on_owner 24.02%。这支持“写争用是重要CPU成本”，也证明消除它不足以提高总服务率；线程交接、序列化和不同执行路径的成本尚需分离。两个候选的full各一次task为8.072/10.464Gbps，仅作完整性控制。

## 实现、回归与失败记录

受影响核心文件为 include/cpnetflux/core/io/persistent_tree_transfer.h、src/core/io/persistent_tree_transfer.cpp 和 tests/unit/persistent_range_write_test.cpp。物理写入门没有覆盖Entry元数据锁、网络等待或payload队列，不新增payload副本；旧Entry外slot/attempt排空、checksum认证、checkpoint、FILE_END/FILE_RESULT和原子发布语义保留。非POSIX direct后端继续原串行处理。

观测新增serialized_direct_write_calls、bounded_direct_write_calls、direct_write_concurrency和direct_write_wait_seconds。前两者属于parallel_direct_write_calls，concurrency是配置而非峰值；IO门等待与Entry/writer计时范围不同，跨线程重叠，不能相加从task wall扣除。详细计时关闭时这些字段不作为归因证据。

- 候选1先用上传/下载8个确定性回归得到RED（未支持模式），实现后GREEN；相关range20/20。测试实际阻塞pwrite，检查同文件串行、Entry元数据仍可响应、不同文件不被阻塞、部分失败后higher-attempt排空恢复。
- 候选1 R1全量615项首轮610通过、2项缺临时测试token失败、3项缺本地RDMA bind跳过；补齐测试环境后5/5通过，合并覆盖615/615。R2相关232/232。首轮失败/跳过保留，不能写成首轮全绿。
- 候选2新增4个上传/下载用例先RED后GREEN；实际同时阻塞两个pwrite并验证第三个不能进入，同时另一文件可完成，另覆盖部分写EIO后重试。最终相关range24/24。
- 最终源码201a43a0：R1全量619/619一次通过，无失败/跳过；显式file_dual故障/恢复/观测10/10；R2相关236/236。完整全量测试提供临时CPNETFLUX_TEST_TOKEN和本机CPNETFLUX_TEST_RDMA_BIND；没有把测试凭据写入仓库或日志。
- 测试先误选cpnetflux_framed_socket_tests的FileSerial过滤器得到0项，随后改为真实链接这些用例的cpnetflux_unit_tests；0项结果没有作为通过证据。

两个排除实验：首次两端各自生成随机源，导致R1预期SHA元数据与实际R2源不一致，实际输出匹配R2源；改为R2单一权威源和一致source.json后重新完整复核。首次perf诊断在读取root所有者的perf.data时被拒绝；只修正诊断报告读取/本次文件所有权，重新采样。失败记录保留，不加入吞吐表。

## 内存和空间清理

所有112个接受样本的外部SHA、输出删除和双端memory guard均通过。资源统计来自50ms guard采样，不能声称捕获了所有瞬时峰值；RSS不含tmpfs数据页，两个指标分开报告。

| 资源 | R770-1 | R770-2 |
|---|---:|---:|
| 最小MemAvailable | 29333168128 bytes | 28912508928 bytes |
| 最大owned RSS | 15114240 bytes | 13217792 bytes |
| 最大本轮tmpfs占用 | 1073786880 bytes | 1073741824 bytes |
| owned VmSwap / host pswpout增量 | 0 / 0 | 0 / 0 |

逐次输出清理回执在acceptance/*-cleanup.json，checkpoint/manifest证据归档在证据根，不丢弃。最终核对两端没有本轮实验活进程；R1/R2分别检查114/228份进程记录，PID存在时不把复用的PID当成本轮任务处理。R2源在确认精确绝对路径、非符号链接、inode106、1GiB及SHA后删除，只移除已空的本轮tmpfs目录；两端临时根均已不存在。R1早期多余源也已按独立校验回执清理。没有递归删除未知目录。

生产服务PID126766（R1，start_ticks1191040）和164834（R2，start_ticks1679976）仍保留。收尾MemAvailable分别30679166976/30137294848 bytes。历史swap存量不等于本轮发生换出；判定依据本轮owned VmSwap和pswpout增量。CPSS、共享数据、历史原件、未跟踪任务文档均未清理。

回执：acceptance/final-cleanup.json、r2-final-cleanup.json、final-process-audit.json、r2-final-process-audit.json；两轮driver、profile的exit.json均为0。

## 证据与复核入口

- metrics/all-summary.json：按commit/label/lanes/policy分组的逐样本值、中位数、CV和CPU。
- metrics/final-decision.json：三组配对差值、正式/诊断/排除数、内存和清理汇总。
- metrics/root-cause-summary.json、root-cause-c1/c2/c4-callchains.txt：默认剖析提取；diagnostic-perf-*.perf.data/.perf.report.txt为原始诊断。
- metrics/*.verified.json：完整命令、协议/配置、客户端计时、服务端CPU、两端guard和SHA；R770-1/R770-2/logs与snapshots保留双端事件、路由和计数。
- acceptance/candidate1*与candidate2*：RED/GREEN、CMake构建、CTest、推送/版本摘要。matching-builds-*.json是逐标签版本门。

已完成检查入口为cmake --build build、cpnetflux_unit_tests的PersistentRangeWriteTest过滤，以及ctest --test-dir build --output-on-failure。R2相关CTest正则与显式file_dual模式在candidate2-r2-ctest-exit.json；不能用全局环境覆盖的测试名反推每个fixture都使用该模式，fixture会按用例设置自身模式。

复算不重新互传、不重建payload：在证据根运行python3 analyze.py、python3 final_analyze.py。实验脚本SHA-256如下（脚本在证据根，未把大payload或perf.data提交Git）：

- experiment.py：d930513ce52bbcd7e17a18739b339cca844730d30dd1097e8bd80ad7b8cfbeb5
- baseline_evidence.py：5aa90070a90bca299bdcd0faf7b5bc95edbd1f988ef442c1a750a0aca9e5353e
- profile.py：9a0247393c48fb887de190dc64a11f4fb28c536606c182a2d31990f2c958f1ac
- analyze.py：fed177066b2b3040a00ab0340c927f8903c2adcb29db149ecacc81e7c9174c7d
- final_analyze.py：ae61c744810a0474be2e2b638a51d1cba7a58627cc268ea66466b49364e828a8
- final_cleanup.py：a79cd04f2ccaae398bda39aeac95e66418237eb845ce56340c121c5efc257264

如需重新实验，使用新证据目录/标签，先在R2单独seed并把source.json复制到R1；随机源已清理，不能声称重建后仍是原SHA。两端固定对应提交、构建和摘要后，audit-crossover/lean-crossover各7轮；file-serial-crossover/file-dual-crossover分别只在对应提交执行。原目录有历史完成标签，不直接覆盖重跑。保持同一源、逐次新输出、外部SHA和资源guard；perf诊断单列。最终收尾使用精确路径/身份校验，不能照搬旧inode去删除新源。

## 下一步交接：新测量阶段，尚未执行

1. **先获得稳定测量窗口。** 设计20–30秒可累计的重复传输窗口，以有界2–4GiB驻留源循环使用，最多一个当前输出；先核实实际MemAvailable再定预算。分别报告task启动/终态开销和窗口有效服务率，每格至少7次、CV≤5%，禁止把单个最高值作为100G能力。
2. **区分文件系统与软件路径。** 同版本对比原始TCP有界内存流、保留V3帧/CRC/队列的诊断内存接收端、当前真实tmpfs文件输出。内存sink必须显式标注诊断，不能伪装成已发布文件或破坏V3-only。它们分别提供传输/协议/CRC/队列和文件系统成本的证据。
3. **按新证据选择一个后续优化方向。** 若主要损失在tmpfs写入，评估实际目标存储后端或写入布局/粒度；若在内存V3路径，拆分CRC、帧处理和交接成本。只有证据支持时再改一个因素并比较端到端吞吐、CPU和恢复。本轮两种同文件物理准入候选已否决，不继续堆第三种门。
4. **保留原目标和暂停项。** RDMA资源/CQ/peer-mapping优化继续暂停；小文件微批单列研究，不能替代大文件吞吐门。默认parallel、read0/write0、depth2、batch1保持；长期稳定性、持久化和30–50/70–85/100Gbps仍未验收。

交接输入核心代码201a43a047102dfe52b1f05ec4497181dd64f6bb，当前文档版本见final-backup.json。上述新测量工具/实验没有在本轮实现或启动，不把后续计划记成成果。
