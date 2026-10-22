# V3-RAW-PERFORMANCE-NEXT-PLAN-01

日期：2026-10-22
状态：开发计划已制定；本文件不包含运行时代码修改。
权威工作区：深圳 /root/projects/CPNetFlux
分支：codex/V3-DESKTOP-NETWORK-BINDING-01
输入代码：运行时基线 55f7a21bb970cc0528d21a52901627aa96660b44；制定计划时 HEAD f7d2959。
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

## 串行开发任务

### 1. V3-FD-RELEASE-01：事务完成后释放文件资源

在文件发布及 committed checkpoint 成功后，关闭事务持有的写入器和文件描述符。批次最终审计保留路径、身份和轻量 checkpoint 信息，需要校验已发布文件时按需重新打开；不要为了延后审计而长期持有每个已完成文件的 FD。失败、取消和 range 重试路径要按“仍有无活动 writer”安全收尾，不能提前关闭其他 lane 正在使用的文件。

验收：

- 在隔离测试进程中将 CPNetFlux 接收端 RLIMIT_NOFILE 设为 1024，成功传完并校验 2048 个 64 KiB 文件；不改服务器全局限制。
- FD 数随已完成文件数不再线性增长，峰值保持在由活动 lane/服务句柄决定的有界水平。
- 覆盖 commit 后审计、range 并发、取消/失败、resume 和输出校验；完整文件树 SHA-256 与源一致。
- 先通过相关 CMake/CTest，再记录提交和证据。

### 2. V3-SUMMARY-CRC-AUTO-01：summary 使用可用的 CRC32C 后端并把时间讲清楚

让 tree hash 接受已配置的 checksum backend，优先使用 auto 的硬件运行时选择并在 JSON 中记录实际 backend 与 summary_tree_hash_seconds。保留 transfer_elapsed_seconds 的原定义，并由外部 harness 另外记录进程总 wall；不把传输计时和传输后扫描合并或互相冒充。保留软件 fallback。

验收：

- 同一输入在 software、hardware（可用时）和 auto 后端生成完全相同的 tree hash；硬件不可用时 auto 正确退回。
- summary 时间和进程 wall 可独立核对；性能报告仍单独呈现传输 core、summary hash 和完整进程耗时。
- 在 4 GiB 与 mixed 数据上量出变化。如果只缩短传输后的摘要时间，只报告 CLI 尾部改善，不宣称 V3 传输速度变快。

### 3. V3-TREE-MANIFEST-COALESCE-01：合并重复的整树 manifest 落盘

把“收到每个文件状态事件”与“重写整份持久 manifest”分开。采用单写者、可合并的 generation snapshot：状态更新锁只保护内存态；序列化和原子替换不在 callback 的全局状态锁内进行。先验证当前恢复流程的权威事实源，再实现有界刷盘（计划初值：最多每 32 个状态事件或 100 ms 刷一次；成功、失败、取消和显式结束时强制 flush）。落盘过程必须按 generation 排序，旧快照不能覆盖新快照。

不把批量刷盘误写成“每个状态事件都已持久”。如果进程崩溃，tree manifest 的未落盘状态必须由现有 per-file committed checkpoint / resume 协商重新核对；不得仅凭陈旧的 Completed 状态跳过文件。

验收：

- dense 测试不再为每个文件 begin/complete 都完整序列化并 rename 整棵 manifest；记录事件数、snapshot 数和锁等待，验证次数符合设定阈值。
- 在阈值前后注入强制退出，再 resume；所有文件最终正确，已提交文件可核对，未完成文件不被误判为完成，不损坏或丢失 checkpoint。
- 比较 2048-file 的 CPU、进程 wall、manifest snapshot 次数和最终 SHA-256；full CTest/恢复测试通过。

### 4. V3-RANGE-DATAPATH-ABLATION-01：分开测量 range 写锁与 payload pipeline

先补观测：range 协调器锁等待/持有时间、实际 checkpoint 耗时、每 lane pipeline 深度/峰值字节数、producer 等待时间，以及读/校验/发送/接收写入的可解释边界。现有 write_range 包围锁等待和 checkpoint，不能直接当作磁盘服务时间。

随后用固定代码和数据一次只改一个变量：

1. pipeline depth 依次试 2/4/8/16 个 frame，报告吞吐、CPU、等待和最大缓冲内存；
2. 独立测试 range 锁优化：不同 range 的互斥写入允许并行，checkpoint 更新仍保持正确串行化；对重叠 claim 继续拒绝。

每次先做一个改动，再运行同一组正确性和性能样本。只有重复 5 次的中位数至少改善 10%、p95 不恶化超过 5%、内存有界且恢复/校验全通过，才把参数或实现设为默认；否则保留原默认并记录结论。不能把“producer wait 次数多”单独当成 pipeline 是瓶颈的证明。

### 5. V3-RAW-STAGE-02：重跑无压缩对照并完成阶段验收

使用报告中的同一 R730→R750 热缓存数据集、4 路配置与无压缩条件，先核对源/接收目录 SHA-256。对 CPNetFlux 和 GridFTP 各数据组做 5 次交错样本，报告完整进程 wall、V3 core elapsed、summary hash、CPU/FD/锁/pipeline 指标。GridFTP 与 CPNetFlux 校验/恢复功能不完全相同，报告中保留这一限制。

阶段目标（作为验收目标，不当作已实现事实）：

- dense 128 MiB 的 CPNetFlux 中位进程 wall ≤ 4.9 秒（约不慢于 GridFTP 2.5 倍），且 2048 文件在 nofile=1024 下稳定通过。
- 两个 4 GiB 无 summary 样本相对 55f7a21 各至少快 15%，不再由 summary 重扫决定进程 wall。
- mixed 样本无正确性回归，性能不退化超过 5%。
- 若数据路径目标未达成，保留失败结果并据新增分解指标决定下一项具体工作；不以“代码已合并”替代性能验收。

## 执行和交付约束

- 按以上任务串行执行，一次只改一个性能因素。每个实现任务单独记录输入提交、明确文件范围、相关 CMake/CTest 命令、输出证据和下一步。
- 深圳 /root/projects/CPNetFlux 是唯一 Git/构建工作区，沿用当前分支并保留已有未跟踪 worker-state 文档。每阶段只提交本阶段明确文件，推送到已核实的 origin 并回读远端 SHA。
- 任何跨服务器传输测试之前，先把同一提交同步到深圳和上海、两端构建，再比对源码归档和关键二进制 SHA-256；构建未完成或 SHA 不同不开始传输。
- GridFTP/CPNetFlux 对照复用相同输入、接收策略和阶段定义；不把 iperf 短探测、热缓存结果或线程累计秒数解释成持续物理链路上限。
- 当前开发阶段还剩 5 个执行门：FD 释放、summary CRC、manifest 合并、range/pipeline 单变量消融、最终重跑验收。第 4 项可在测试证明无收益时以有证据的 no-go 结论完成，不强行加复杂度。