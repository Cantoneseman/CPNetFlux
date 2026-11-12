# 阶段70：任务级接收CPU隔离诊断

输入运行时42718ac、阶段69固定输入f4129e88及其网络失败证据；实际执行固定随后提交并已备份的文档提交，两端重新构建核对四hash。单写root、R2仅固定提交和实验对端。

范围：阶段69已经证明4路实际RX队列9/27/7/28分散，但R1仍OOB/missed各+132。因此“分散即可消除错误”的充分性假设失效。只作一次不同条件的最小隔离实验：R1接收客户端的用户线程避开这四个IRQ所在物理核的全部SMT siblings，检验用户线程与接收服务共享核是否影响错误。尚未证明CPU争用为根因；该实验不是已验证修复，不修改运行时/协议/默认值。不做全局IRQ/RSS/RPS、rate/lane/frame/THP/cache/swap调整，不重跑69或扫描CPU掩码，不启动RDMA及DEFERRED工作。

现场只读依据：IRQ309/311/329/330分别固定CPU7/9/27/28；相应RPS mask全0，NAPI证据与CPU一致。物理兄弟为7,23 /9,25 /11,27 /12,28；32逻辑CPU同socket0、NIC NUMA0。接收客户端唯一允许集合为0-6,8,10,13-22,24,26,29-31（24逻辑CPU）。taskset只作用本次新客户端及其继承线程；全局IRQ不变。R2发送及其1.5e9 B/s每lane应用节流不变，kernel pacing/exact-read仍off。必须在新进程与全部线程/proc status核对实际掩码，记录last_cpu采样；没有观察证据则不判通过。无payload多线程继承自检先通过。

其余固定阶段69配置/计划/native私有within_size/实际THP/4TCP/256KiB DATA/1MiB chunk/CRC/verified_chunks/checkpoint16/inline+mmap/queued sender/64MiB每lane/接收1MiB请求/TLS和压缩off/非持久化commit不变。R2 .2驻留tmpfs源到R1 .1新目标，server data base61200和显式bind，隔离49337，原2121不动。先生成源并hash/驻留证据，大小1GiB；本轮不启动8/64GiB，8GiB9.25GiB free门不降。

资源门：准备前双端MemAvailable>=12GiB、MemFree>=2.25GiB；运行MemAvailable>=12GiB、host pswpout增量0、owned进程及leaf cgroup swap0，保留固定应用预算与root observer覆盖；有限cgroup硬上限若实际为max须明确，不冒称强制上限。连续观测双端资源/错误，保留实际tuple、CPU/NAPI/队列、TLS/transport及清理证据。RSS/fields/plan和CPU拓扑、IRQ/RPS在使用前后核验；必要的计划实时预检、affinity验证必须实测计时并报告，不延续69未计时缺口。源准备/版本审核/外部SHA/观测/冷生成成本分别记录；客户端内plan加载以及taskset启动包含在进程口径。原目标setup+mkdir+client与增加必要预检后的口径分别列明，不将应用summary称稳态或严格用户端到端。

唯一一次1GiB，任一内容/资源/网络/执行条件失败立即停止全部剩余payload，保留刷新计数、失败和清理。成功最多证明此配置一次可行；不凭单样本认定CPU根因、稳定收益或M2晋级。失败后回到证据定位，不继续CPU掩码/速率扫描或重复挑样。

后续用户完整目标保持：如本门通过，再由已有“配置和实验”聊天在串行交接后从0完成五对同条件CP/GridFTP完整交错比较（安全/校验/prep语义差异单列），目录/反向/安全场景，随后>=1800秒且>=100任务的常驻服务稳定性与双机故障/取消/恢复/重启/坏数据/TLS，再上机报告。正式比较不足5对、CV>5%或任何硬门失败均不算合格；任何未实测项明确NOT_RUN。旧69的B/C停止记录保留，70只有自身条件通过才可新开后续任务，不追补69通过。

受影响文件和验收范围已在E70/scope.json修改前固定；实验脚本/证据仅E70，不修改历史69，不混入payload/凭据/构建产物。完成阶段明确文件提交推送并回读远端SHA；双端构建与脚本自检记录。root持有写入权时不派聊天并行写；正式交接记录真实turn/session与停止状态。
