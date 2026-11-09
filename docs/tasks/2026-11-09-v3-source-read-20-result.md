# V3 Source Read20 结果与交接

日期：2026-11-09。**LF5 只读源窗口诊断已完成；当前方案局部收益不足，不晋级 V3。**
完整任务基线仍是 scope17 约27.3Gbps。本轮无网络传输、运行时提速或100Gbps新成绩。

## 输入与实现

- 输入：`980f30de02cf72ca5a3b107f2dad7eeea47e9b8a`。
- 实现及测量：`dfb582ed9088b04a1562028cadfeebcbd661acf0`，已push及回读同SHA。
- remote：`git@github.com:Cantoneseman/CPNetFlux.git`，沿用 `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`。
- R770-1根目录单人串行，无子代理、worktree或桌面；两份October14无关文档保留。
- R2只读检查，仍为 `7dbaa06fcc8d70dd5e1f9cdfc9df9e5dca727ea1`，没有用异版互传。

新增 Linux `cpnetflux-source-read-bench --source PATH --mode pread|mmap --workers 1..4`。
POSIX臂使用真实 `storage::readAtAll`、PosixFile 和 FileIoStats；mmap臂只读映射并同步消费。
两臂均保留每帧65532B上限、1MiB chunk切分、真实帧CRC32C扫描及按range累积combine，
不是重复热缓冲、不省CRC、不改变CRC算法。运行时、协议、队列、默认配置未修改。

源必须为1..1GiB regular file，O_RDONLY/CLOEXEC/NOFOLLOW/NONBLOCK打开，拒绝FIFO、
目录和symlink。mmap为PROT_READ/MAP_SHARED，无population/THP/advice；每worker最多
64MiB窗口，先关闭旧窗口再建新窗口。借用只在当前同步消费期间有效，不能用于异步队列。
前后核对descriptor/path的dev/ino/size/mtime/ctime；它是变化检测，不是文件快照，
并发truncate的SIGBUS尚未处理，只允许隔离诊断，core dump关闭。

## 每GiB逻辑工作与源码核对

以下为无压缩、CRC32C、完整对齐文件、当前65532B数据帧的逻辑工作；不是DRAM实测流量。
源码位置基于输入980f30d，源代码本轮未改。

| 位置 | 每GiB工作 | 所有权/意义 |
|---|---|---|
| persistent_tree_transfer.cpp:1635/1648 | 17408次帧准备/readAtAll，源到vector共1GiB复制 | producer reserve/resize并复用vector；resize初始化另有工作 |
| 同文件:1656/1659 | 帧CRC扫描1GiB，17408次range combine | combine不再次扫描payload |
| 同文件:1672；payload_pipeline.h:14/35 | vector移交队列、消费者使用后回收 | move不等于多搬一遍payload；当前payload类型拥有vector |
| framed_data_socket.cpp:165/188 | scatter/gather sendmsg覆盖1GiB逻辑payload | 没有先拼成新用户缓冲；普通TCP仍须复制入内核，短写可能增加syscall |
| 接收TCP到buffer、buffer到MappedFileWindow | 各覆盖1GiB payload，另有CRC扫描/校验状态 | 新目标还要分配/初始化页面，不能只数memcpy |

本轮只尝试消除第一行的显式源到vector复制；没有消除内核发送复制、TCP接收、
接收端复制/首次触页、队列同步、manifest、checkpoint或发布。没有测量DRAM带宽。
未发现等价只读源窗口运行时；旧 sender-inline/direct-receive不重开。

## 验证结果

编译空实现先触发CTest失败：requested diagnostic not implemented。实现后独立Python
CRC32C oracle核对真实多样字节，覆盖1字节、partial chunk、4路分区和跨64MiB窗口。
首轮失败是测试把多worker各自映射误算为全文件只映射一次；修正按各range计数。

相关56/56 CTest通过（精确列表 `tests/related-list.log`）。追加数据破坏拒绝和热路径
assert字面量免构造string后，新smoke再次通过：**9个成功场景、24个负例**。
含真实Posix短读/EINTR重试、EIO、mmap失败、CRC坏数据、时间戳变化拒绝、两臂取消、
无效参数、空/超大/非regular/symlink输入。LD_PRELOAD只作用于smoke自有精确路径，
检查只读保护、64MiB限额和实际map/unmap平衡，失败/取消无映射泄漏。

```sh
cmake --build build --parallel 4 --target cpnetflux-source-read-bench cpnetflux_source_read_fault_shim
ctest --test-dir build -R 'PosixFile|FileIo|Checksum|Crc32c|cpnetflux_source_read_bench_smoke' --output-on-failure
```

没有模拟线程创建失败、munmap失败或SIGBUS恢复。源码变化检测也不能代替future queue
借用期间的源不可变保证；诊断正确性不代表V3集成已经安全。

## 五对固定版本本机筛选

同一1GiB varied-byte resident tmpfs源，4 workers，五对AB/BA交错，十次全部保留。
每次先独立1MiB pread扫描参考range CRC，让源驻留；不预建候选窗口PTE。
setup单列，约131ms；计时service包含重新open/identity、线程和buffer准备、同步起跑、
实际读取或mmap/首次PTE、帧CRC/range combine、munmap、digest核对、源identity/close。
没有每帧追加采样时钟；POSIX本身的FileIoStats计时是被比较路径的一部分。
源生成、外部SHA和删除均在service之外，删除耗时没有进入收益指标。

| 指标，中位数 | POSIX pread | 只读mmap |
|---|---:|---:|
| service墙钟 | 52.527ms | 50.852ms |
| service进程CPU和 | 0.202024s | 0.185449s |
| service墙钟CV（population） | 1.001% | 1.754% |
| worker CPU和 | 0.198695s | 0.183751s |
| worker minor faults | 64 | 16403 |
| 源到buffer显式复制字节 | 1GiB | 0 |
| readAtAll调用 | 17408 | 0 |
| CRC扫描字节 / combine调用 | 1GiB / 17408 | 1GiB / 17408 |
| mmap/munmap调用 | 0/0 | 16/16 |
| 各worker峰值映射之和 | 0 | 256MiB |

中位service墙钟减少 **3.188%**（约1.675ms/GiB），进程CPU减少约 **8.205%**。
配对墙钟下降3.229/3.188/4.329/5.294/0.590%，5/5为正，稳定性通过，
但收益明显低于预定15%局部门。**局部门失败；不接入V3、不重跑求更大收益。**

机制事实是少了一遍显式读取复制，同时增加约1.64万次新映射fault；
不能证明这些fault独自解释全部抵消，也不能把CPU和节省直接换算成任务墙钟。
诊断没有队列/网络/接收端，不能把其局部服务率或1.675ms按8倍外推到8GiB完整任务。
源读取可能与网络发送重叠，本轮不足以建立>=10%完整任务收益预算。

## 资源、清理和备份边界

- 最小MemAvailable 29,606,096,896B（约27.57GiB）>=12GiB；owned VmSwap峰值0，
  host pswpout增量0；十次源range CRC及前后SHA一致。
- 单源1GiB，两个模式不并行；各worker<=64MiB，总映射<=256MiB。无输出payload。
  自有源及mkdtemp目录已删除；生成约1.786s、删除约56.4ms，均是外部单列成本。
- R1生产PID126766/start1191040、R2 PID164834/start1679976保持；未停服务。
- 无网络流量，未改THP、mount、网络、NUMA或RDMA。scope17接口+94仍未关联。
- 输入、二进制/CMakeCache SHA、原始stdout/stderr、资源、分析和实际驱动位于
  `/home/Su/experiments/CPNetFlux/2026-11-09-v3-source-read-20`，无大payload进入Git。

## 总体进度与下一项

三阶段工程已完成，大文件性能门未过。LF0逐字节诊断完成；大帧/CRC预算未证实，
暂缓而非判已淘汰。LF4当前seed+collapse和LF5当前只读源窗口筛选均已结束、不集成。
更广的TCP零拷贝/原生RDMA搬运还没有设计和验收，不因本轮结束宣称全部大文件方向已穷尽。
这些更大改动要有新的关键路径预算与buffer完成/回收设计，RDMA仍按用户要求暂停。

按既定有界路线，下一项 **S0：当前提交下的小文件固定开销基线**，本轮尚未执行：

1. 固定下一阶段提交，同步R2，同配置完整构建并核对版本/二进制SHA，再允许互传。
   先查现有脚本/数据生成与观测接口，复用当前V3路径；不先改微批协议。
2. 可行子集：4/16/64/256KiB各1K，4/16/64KiB各10K；256KiB×10K超过1GiB，
   本轮资源合同下不跑。每次单源/目标<=1GiB，完成后校验并精确清理。
3. tuned native明确包括pending-window auto（密集小文件实际8），默认2单独标识。
   不只拿window1作弱对照，不把历史pending-window收益包装为新成果。
4. 保留CRC、租约、成员manifest/checkpoint及发布；记录cold setup、T_ready、吞吐、
   p95、open/逐文件控制、校验、checkpoint/发布的已有计时和资源/接口证据。
   观测不足才补默认关闭的计数，并先测开销；先归因，不并改聚合/压缩/调度。
5. 固定成本主导后另立成员微批设计与故障隔离/恢复门；再分开评估固定批与adaptive。
   正式目标T_ready中位下降>=20%、p95/恢复不恶化，大文件回退<=5%。
   小文件结果不替代30–50G、70–85G和100G尚未通过的大文件门。

详细范围见[任务](2026-11-09-v3-source-read-20.md)，统一入口见[ROADMAP](../ROADMAP.md)。
