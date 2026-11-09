# V3 Byte Work18 结果与交接

日期：2026-11-09。范围见[任务](2026-11-09-v3-byte-work-18.md)。
状态：路线合并、诊断开发及本机筛选完成；大文件完整任务性能门仍未过。
路线提交`abf592347a2cc5478b46a945d1170fbfbc5af07a`；首批诊断`6693327793c07253b1127b5f952755f18b082068`；
修正CRC输入分布后的诊断`fb96888fdcf6f00fa459f8ef7df649516a978169`。均已推送并回读。
证据：`/home/Su/experiments/CPNetFlux/2026-11-09-v3-byte-work-18`。

## 完成的开发

- checksum-bench新增combine模式，真实CRC32C/ChecksumComputer，1MiB chunk、合法尾帧，
  明确虚拟字节/预计算叶CRC/计时排除项；旧扫描模式保持。支持fixed及最多64块varied叶序列，
  只保留叶值，逐块组装对比整块扫描。没有修改runtime crc32cCombine算法。
- mapped-cost-bench复用实际MappedFileWindow，新增65532/256KiB/1MiB处理粒度和profile on/off。
  用每64MiB窗口外层RUSAGE_THREAD观测CPU、fault及真实write调用数，不因减少逐调用计时造成假收益。
  冷写、同一live mapping热覆盖和独立pread校验保留；这些不是V3大帧支持。
- 更新计划/决策/ROADMAP：独立大帧和CRC→条件组合→页面管理→数据搬运→小文件。
  小文件已有pending-window优化仍作为tuned native，不重新包装为成员微批成果。

## 局部测量：调用减少不等于首次触页减少

同提交6693327，本机1GiB全新tmpfs目标、4 workers、每粒度3次交错样本；profile off。
源是诊断用1MiB缓冲，非真实网络/完整V3文件；每阶段字节、两次写入和独立内容检查通过。

| 逻辑处理粒度 | 每GiB写入调用 | 冷写线程CPU之和，秒 | 冷写minor faults | 冷写CPU CV |
|---|---:|---:|---:|---:|
| 65532 B | 17408 | 0.623452 | 262144 | 1.08% |
| 256 KiB | 4096 | 0.617279 | 262144 | 0.18% |
| 1 MiB | 1024 | 0.626040 | 262144 | 1.24% |

相对legacy，256KiB冷写CPU约-0.99%，1MiB约+0.42%；均没有足够局部收益。
这些是重叠worker累计CPU/墙钟，不能除以固定线程数或加到流水线墙钟来预测吞吐。
两次额外profile-on仅作观测控制，不作为三样本稳定性结果。

CRC首批fixed序列只约4.58ms/GiB，被判不足以代表不同块输入；原始证据完整保留。
fb96888同提交fixed/varied核对及varied三粒度各3次，100次虚拟GiB迭代（不分配/存储100GiB）：

| CRC叶分布/粒度 | 每GiB单个chunk累计器组装耗时 | 组内CV |
|---|---:|---:|
| fixed / 65532 B | 4.596 ms | 0.12% |
| varied / 65532 B | 21.092 ms | 0.04% |
| varied / 256 KiB | 0.1084 ms | 0.80% |
| varied / 1 MiB | 0.01586 ms | 1.59% |

fixed低估约4.59倍；改变了输入重复性和叶工作集，未单独用硬件计数归因为分支预测。
这是CRC原语诊断，leaf扫描/生成在计时外、计入setup字段，不是文件或网络服务率。
当前65532长度有14个置位；单chunk的16个长帧加64B尾帧需要225次矩阵应用，
256KiB/1MiB分别为4/1次，所以大帧本身会消除很多CRC拼接工作，B的收益不能与A相加。
chunk累计器、范围累计器和两端CPU也不能直接相加作为关键路径。

## LF0决定

- A（大帧）：逐帧工作量证据成立，但新文件冷写未因粒度变化改善，仍未证明完整任务>=10%的
  保守收益预算。暂缓协议/运行时原型，不称为已淘汰的大帧候选；有新文件特有成本证据才重新晋级。
- B（CRC预合成）：原语成本确实存在，但scope17 Self比例和本轮局部计时不足以证明整任务预算。
  暂不集成runtime。若A后续通过，常用长度已是2的幂，现算法本就一次矩阵应用，B需重新估价。
- 没有运行新的跨机完整任务。27.291Gbps仍只是scope17条件下的旧基线，不计算本轮吞吐收益。
  scope17接口+94未解释；本轮没有自有TCP payload，不能把未重现当作网络门清洁。

## 页面管理可行性：出现了可实施的新机制

R1 kernel 7.0.0-38-generic，R2只读检查为7.0.0-34-generic；shmem策略均never，/dev/shm未改挂载。
[Linux文档](https://docs.kernel.org/admin-guide/mm/transhuge.html)明确MADV_COLLAPSE可独立于never策略；
never本身不能用来认定不可行，仍须实际验证。

R1单次16MiB自有文件/对齐映射，ordinary对照与MADV_HUGEPAGE+每2MiB一次seed+MADV_COLLAPSE：

- collapse返回0，smaps `ShmemPmdMapped=16384 kB`；普通映射为0。
  KernelPageSize/MMUPageSize仍显示4kB，不能仅据这两个字段否认实际PMD映射。
- seed/collapse/copy同一外层区间，minor faults为4096→8；其中collapse路径copy阶段为0。
- 两个输出独立SHA相同，自有文件和地址空间均清理，sysfs/mount/服务未改。
- 单样本Python可行性探针，不是稳定性或吞吐验收；open/ftruncate、映射和关闭等生命周期成本
  不在该局部时间分母，排除项见page-feasibility.json，不能宣称完整任务收益。

这与旧population不同：实际得到更大页面并减少fault工作，而非只把相同工作提前。
尚未证明实际MappedFileWindow、1GiB或完整8GiB任务受益，也未解决恢复/窗口所有权设计。

## 验证、资源与交接

新接口先在旧程序失败，原因明确是unknown选项；实现后2/2 smoke通过，随后varied CRC新增行为
再次红绿通过。最终CRC smoke覆盖4个成功组合、13个拒绝场景及旧扫描；独立Python CRC32C及
xorshift字节oracle通过。mapped smoke覆盖8个成功及10个拒绝场景。43项相关既有单测通过。
CMake工具目标构建成功；列表、红绿、构建和stderr原始日志保留。
一次性varied驱动首次因__file__未传入在启动前失败，修正后执行；失败日志保留，未产生性能样本。

主诊断观察到最低MemAvailable约27.32GiB，owned swap=0、host pswpout增量0。
主mmap输出至多1GiB，页面探针16MiB；payload和tmp目录已清理。
生产PID/start_ticks R1=126766/1191040、R2=164834/1679976保持。
R2未同步新诊断代码，本轮没有跨机payload；后续互传必须重新固定并同步两端提交、配置和二进制SHA。

下一项是LF4真实有界窗口诊断：保留MappedFileWindow默认，准备/seed/collapse/copy/unmap全部计时，
证明实际页型、总CPU和fault变化。不得在已有verified字节上盲写seed零值；必须保护租约/attempt、
范围边界和并发所有权，测试已有内容、恢复、失败、取消和清理。映射仍<=64MiB/lane、总<=256MiB。
先<=1GiB有限局部筛选，有收益再做完整V3文件集成与五对8GiB、>=10%/CV<=5%正式验收。
若不支持或资源/语义门未过则关闭该页策略，不重开旧population，不更改全局THP或共享mount。
TCP大帧和CRC暂缓但保留研究记录；搬运路径、小文件与原生RDMA后续范围继续各自管理。
