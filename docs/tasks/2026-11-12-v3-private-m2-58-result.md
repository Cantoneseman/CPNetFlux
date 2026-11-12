# Task58：有界8GiB能力、正式矩阵资源停止与修正

日期2026-11-12。状态：ENGINEERING COMPLETE / M2 OPEN / FORMAL RESOURCE BLOCKED。
本轮实现并验收有界8GiB私有后端与正式矩阵驱动；唯一正式尝试在源准备阶段触发
host swap-out硬门，尚未发生网络传输，没有新增有效吞吐成绩；随后补齐分配前资源拒绝。
[开发与正式合同](2026-11-12-v3-private-m2-58.md)、
[版本/配置/原始证据冻结](2026-11-12-v3-private-m2-58-freeze.json)。

## 实现与两个版本

输入126a3877b5a0b0f2fc921e635e4615443871eccf。
正式尝试固定代码f2b12f02293df7766b2c3acad9f52b9814390d8a：
新增m2-control/m2-candidate固定profile，挂载容量8448MiB、负载8GiB，
只能走native监督器及V3；原admission仍1GiB/1280MiB，默认不开启。
每个任务独立新建私有挂载、全新目标与一份源，清理后再下一个任务，避免累计十份数据。
固定8MiB伪随机块重复1024次并完整物化，关闭压缩；重新生成也要求同SHA，不能换数据挑样本。

矩阵固定五对AB/BA，最多十个任务；control为M1 combined传输配置+私有huge=never+
kernel pacing off，candidate为同协议/预算+huge=within_size+每路2e9B/s内核ceiling。
两臂都用native准备且全计时。这是组合验收，不单独归因THP/pacing/native，
也不与历史共享/dev/shm成绩直接算收益。DATA仍256KiB、1MiB chunk、四路TCP、
CRC32C/verified_chunks、checkpoint16、queued sender/inline实验mmap、TLS/压缩off、
commit_sync=none，payload64MiB和总映射256MiB预算不增加。
实际成功字节/(必要namespace与挂载+目标目录+client创建至成功退出)求Gbps；
源准备、外部SHA、观测和清理单列，两臂一致。禁止提前准备目标或剔除必要开销。
候选中位≥40Gbps、两臂时间CV≤5%、至少4/5改善及所有硬门/恢复才M2通过；
同时报告速率CV。缺失门、提交/SHA变化、坏数据或失败不允许继续下一任务/补跑。

后续修正代码998cba8883dc3594d976beb0e3d97881d28d8f37：
正式8GiB实际分配前额外要求MemFree≥9.25GiB（挂载容量8.25GiB+1GiB预留），
并保留MemAvailable≥21.25GiB；负载中仍≥12GiB、自有swap0、host pswpout增量0。
启动器、native root V3入口、源seed、矩阵调度均检查；不足时在目录/挂载/源写入之前拒绝。
setup-only不物化payload，可继续验收容量/策略/卸载。该保守门减少已知回收风险，
不是host零换出的保证，更不是吞吐优化或M2成绩；不修改全局缓存、swap、sysctl/cgroup策略。

两次代码都先R770-1提交/推送回读，再同步R770-2；没有使用dirty/不同版本互传。
src/include传输核心未改，四个二进制的精确SHA和两版配置分开冻结。

## 验收覆盖与原始失败

f2b12f0两端相关CMake构建成功，CTest各262/262通过；候选ceiling显式开启的
两项中断恢复两端各2/2通过。native实际验证原8种生命周期、两种8448MiB零payload
容量/huge策略/卸载、11个危险参数拒绝与symlink/既有挂载拒绝。

998cba8修正后，两端构建成功、相关专项各8/8通过，包含大文件/私有驱动、
真实native生命周期与物理free拒绝、kernel pacing和恢复。
没有在修正提交上重跑完整262项；src/include和三个传输二进制与f2b12f0相同，
新增行为由上述专项及先RED后GREEN回归覆盖。修正后的两端native_free_refusal实际true。

合同测试先捕获缺少profile/统计/停止接口；另捕获V3入口未提供mode上下文，
已补齐；物理free回归也先失败后修复。原始RED日志、初版/修正版构建及CTest均保留。
所有这些是相关集合，不是全仓库全部测试。容量验证不等于8GiB受载或M2通过。

## 唯一正式尝试：源准备停止

数据方向计划为R770-2→R770-1，192.168.100.2/.1显式bind、eno17095np0。
矩阵只尝试第1个control，源文件write到8,002,732,032B（约7.45GiB）时，
R2 host pswpout从2,292,431增至2,292,432，4KiB页增量1，触发seed swap gate。
没有启动传输server/client；完整有效任务0，candidate0，吞吐/中位/CV/改善数均未测。
本轮已停止，没有补跑、更小负载复测、重置计数或参数扫描。

| 证据 | 结果与含义 |
|---|---|
| 源准备采样 | 954次；最低MemAvailable 22,132,252,672B，仍高于12GiB |
| 源进程 | owned_swap_peak=0 |
| 源所属叶cgroup | swap.current增量0、pgscan增量0，memory.high/max无限、OOM事件0 |
| user-1002.slice祖先 | swap.current增量4096B；pgscan增量57097，其中kswapd56958、direct139 |
| 更上层user.slice | swap.current同增4096B |
| 归因边界 | 与不同于源leaf的已有charge发生换出相容，具体对象/进程未识别；不声称精确因果 |
| 整体资源门 | FAIL；祖先/host事件不能因源进程为0而忽略 |
| 源/目标清理 | 部分源删除、R2自有根删除、R1私有挂载卸载及根删除，全通过 |
| 最终现场 | 两端/dev/shm 0B，自有cpnetflux-s0路径为空，49337无监听，原2121身份未变 |

原leaf/祖先快照由实际writer/SHA进程采集，原始seed-resource.json已从R2取回，
不是另一个SSH进程的事后计数替代。事后只读meminfo/vmstat/zoneinfo/cgroup也单独保存，
不能反向当作开始时的物理空闲证据。源leaf数据支持缩小归因范围，不代表host门通过。
整个formal-m2的scene_gate=false精确保留本次host事件；清理门单独为true。
源尚未生成完整预期8GiB，因此source-check不通过是连带结果，不是传输后数据损坏。

## 当前资源条件与后续

修正后只读采样：
R1 MemAvailable 30,575,218,688B、MemFree 3,957,997,568B；
R2 MemAvailable 30,105,047,040B、MemFree 8,292,315,136B。
两端都未达到9.25GiB物理free门，current-resource-admission.json为m2_ready=false；
这些是当时快照，后续必须重新检查。MemAvailable高并不等于本轮可以无回收物化8GiB。

下一步先解决源/目标分配需要触发宿主回收的问题，固定可复核的资源条件/内存所有权设计，
不只等待空载pswpout暂时稳定就宣告解决；不得强制清理全局缓存、关swap、删除其他项目或
以owned swap替代host门。若采用新的源后端/分配设计，先补预算、计时边界与清理测试，
固定版本后另立合同，不原样重试本次矩阵。也不把自动化反复碰运气当作开发进展。
满足条件后才考虑重启正式M2五对矩阵，40Gbps/CV/配对和所有硬门保持。
软件后续结构性方向仍为TCP数据搬运与接收页管理；不原样重跑已关闭候选。
原生RDMA、微批、统一工作单元/策略记忆继续暂停。

历史正式M1仍为506b63b五对8GiB组合中位34.159410Gbps；
Task57为118211f单次1GiB完整目标40.865345Gbps，仅可行性准入。
当前HEAD未形成新性能基线；M2、70–85/100Gbps、磁盘/TLS/长时稳定性与生产验收仍开放。

## 交付、复核与回退

精确版本/四二进制/源对象、正式失败与最终修正证据见冻结清单；
实验根/home/Su/experiments/CPNetFlux/2026-11-12-v3-private-m2-58。
离线验收只读证据，不启动payload：

    python3 /home/Su/experiments/CPNetFlux/2026-11-12-v3-private-m2-58/verify-delivery.py

最终文档提交、GitHub SHA回读和R2同步保存在delivery.json，不在冻结清单自引用最终SHA。
Windows只复制报告、计划、冻结清单和小JSON至_codex_stage58，核对SHA，不覆盖源码树。
原两份未跟踪Task01b文档保留，不提交凭据、构建产物、大payload或CPSS(DCC)。
不选择formal profile和--enable-formal-m2即可退出新矩阵；旧admission默认保持。
若恢复源码，先保留当前修改，再从明确GitHub备份恢复指定文件，保留本次失败证据。
