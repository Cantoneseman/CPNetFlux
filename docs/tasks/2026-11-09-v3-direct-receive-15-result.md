# V3 Direct Receive 15 结果：可行性收益未过门，关闭候选

日期：2026-11-09。输入：`1bdd7bc5aac86f6f609594613b48f2be1f719d37`。
源码提交：`d4a110adec342e2d5afbaa758366022544814d42`，已推送 GitHub 并回读相同 SHA。
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`。R770-1 单人串行开发，R770-2 固定提交构建验收。
范围与预设停止边界：[Direct Receive15 任务](2026-11-09-v3-direct-receive-15.md)。

## 结论和性能边界

完成真实 TCP/V3 帧直接接收到有界文件映射窗口的诊断原型及五对8GiB对照。
普通接收后复制中位 **37.299649 Gbps**，直接接收中位 **38.346358 Gbps**，
中位数之比提高 **2.806217%**；两组 CV 为 **2.881999% / 1.733292%**。
未达到预设 >=10% 收益门槛，故 **关闭当前直接接收候选，不集成到 V3 文件运行时**。
保留显式诊断工具及测试，运行时接收策略和默认值不变。

这里是帧级 TCP→tmpfs 诊断，不是完整文件任务成绩。发送端每 lane 复用最多65532字节
的0x5a热 payload，无8GiB真实源文件读取、发送流水线、RangeFileCoordinator、checkpoint、
恢复或发布。不能把38.35Gbps写成完整CPNetFlux文件吞吐，更不能与上一轮完整任务27.932Gbps
直接计算优化收益。完整任务30–50Gbps中期门仍未通过，100Gbps未达到。

本轮实现消除了诊断候选中的 heap→mapping 用户态复制，但内核 TCP 复制、首次写入和
页分配仍然存在。两个实验臂均约209.8万 minor faults，major faults为零；这支持
“单独省去用户态复制不足以达到本轮门槛”，不证明所有成本都来自缺页，也没有确定唯一根因。
不重开已失败的 population、sender inline，也不继续追加线程、队列或映射参数搜索。

## 开发内容与生命周期

- `FramedDataSocket::readAllV`：仅显式明文 TCP 的前缀/负载分散接收；支持短读、跨 iovec
  边界、EINTR、空 span，EOF/EAGAIN/非法指针长度立即返回错误；拒绝 TLS/RDMA。
- `MappedFileWindow::writableSpan`：沿用实际存储类，一个对象最多一个64MiB窗口；返回
  到文件/窗口边界的临时 span。下次 borrow/write/close 后失效，借出不计为已认证写入。
  原 `write` 与借出共享映射建立逻辑，原有 profile 计数语义保留。
- `cpnetflux-tcp-frame-bench` 新增显式 server 后缀：
  `mapped_copy|mapped_direct output-directory`。原有 memory CLI/默认行为保留。
  映射模式只接受 V3/CRC32C、1–4 lane、总量<=8GiB，非法组合启动前拒绝。
- 两臂先读取/校验64字节帧头、精确身份/attempt/offset/length，再取得真实 WorkExecutor
  IO lease。直接接收将4字节 CRC prefix 和首个映射 span 一次 readv，跨窗口时先消耗
  当前 span 的 CRC 再替换映射。只有整帧 CRC 正确才推进 PersistentDataSession。
- FILE_END 整流 CRC、映射 close 和 lease finalize 均在成功 FILE_RESULT 前完成。
  负载失败会排空 IO lease、reject 并断开，禁止成功应答；单 lane 同步持有映射和 fd。
  工具未实现 RangeFileCoordinator 的 attempt writing pin，因此不能直接用作生产集成。
- 独占临时目标在 mkstemp 后即 unlink，fd 持有到完整独立 pread 字节比较结束；每次
  退出释放，包含异常/进程退出。禁用 core dump；SIGBUS 仍为隔离进程致命错误，不宣称恢复。
  本轮没有创建、发布任何对用户可见的成功文件。

新增 smoke 同时覆盖两个实验臂：1字节、四 lane 不等长、真实跨64MiB帧，及 CRC损坏、
CRC前缀截断、payload截断、错误offset、错误identity、错误attempt共12个负例。
身份/offset/attempt错误均证明映射次数为0；所有负例均无成功确认、无遗留目标。
单元测试额外覆盖借出边界/失败、无认证计数、短读/EINTR及EAGAIN/其他传输拒绝。

## 正式对照

R770-2 -> R770-1，四路 TCP，显式 bind 192.168.100.2/.1；双方路由均为 eno17095np0，
接口字节计数支持真实数据路径，rx/tx error/drop增量均0。构建启用RDMA不代表本次使用verbs；
本工具选择TCP，没有实施原生RDMA优化。未改变系统网络、THP、亲和性或生产服务。

每次全新8GiB稀疏tmpfs目标、四个2GiB分区，每 lane单个64MiB映射；不预热输出。
帧逻辑最大65532字节，每组每次131084个DATA帧。两臂使用相同帧头前置验证、CRC、
lease start/finish和成功确认。与完整1MiB chunk/range路径的帧几何不同。
实验顺序：copy/direct、direct/copy、copy/direct、direct/copy、copy/direct。

| 指标 | mapped_copy | mapped_direct |
|---|---:|---:|
| 正式样本 | 5 | 5 |
| ready→ACK吞吐中位 Gbps | 37.299649 | 38.346358 |
| 吞吐 CV | 2.881999% | 1.733292% |
| ready→ACK wall中位秒 | 1.842363 | 1.792073 |
| 接收进程 CPU中位秒，含独立复读、尚未最终close | 9.472056 | 9.340358 |
| 外部time CPU中位秒，含最终清理 | 9.87 | 9.74 |
| 外部进程 wall中位秒，含启动/等待/独立复读/清理 | 4.84 | 4.77 |
| 独立全量复读 wall中位秒 | 2.323082 | 2.311099 |

吞吐只使用 ready→ACK；包含首次触页、内核接收、帧/整流CRC、lease和映射回收。
独立复读不放入此吞吐分母，但其成本同时报告于进程/外部 time；不混用这两个口径。
CPU是进程累计，不能从 wall直接相减，也不是纯接收/复制CPU。
逐对吞吐变化：-3.201853%、+8.013796%、+1.556936%、+3.800772%、+4.457392%；
配对变化中位3.800772%，同样不足10%。五对样本不支持更强的统计或普适收益结论。

每次 mapping map/unmap=128/128、结束active=0，峰值上限256MiB；IO租约完成字节8GiB。
copy臂写入类计数8GiB；direct臂用户态mapped copy计数0、实际direct receive8GiB。
两端全部CRC/完整帧流状态及ACK成功；接收端每个输出均独立全量pread检查0x5a，
不是SHA源文件对照，也不能取代完整V3文件任务的外部SHA/恢复/发布门。

## 验证、失败记录、资源与备份

- 旧可执行程序缺少新 mapped sink，首次行为测试按预期红，保留 `red.log`。
- 首次green的代理测试误将attempt写入44字节（实际应为56），改成真实协议位置后
  全部负例通过。这是测试fixture错误，不是服务器数据面修复；保留首轮错误说明。
- R1相关CTest初轮233/233，提取旧测试名单时截断了含空格的参数化名称；按完整名称
  修正并补跑12/12，总计 **245个唯一相关测试全部通过**。R2按修正名单一次245/245。
  包括恢复、服务重启、超时重试、既有mapped/profile及帧回归；不是全量测试声明。
- 两端同 `d4a110ad`、RelWithDebInfo、测试/ISA-L/RDMA构建开关和版本一致，八个原
  二进制加tcp-frame/mapped-cost诊断共10个SHA-256一致，互传前核验并留证。
- 二十个主机/样本监控均 owned VmSwap=0、host pswpout增量0。最低MemAvailable
  **20.411549GiB**（门12GiB）；最大轮询owned RSS **250.683594MiB**，接收工具
  getrusage RSS最大 **260.386719MiB**。tmpfs目标占用单独计，不包含在该RSS上限内。
- 正式接收进程time报告 major faults/文件系统input/output均0。全局swap-in若存在
  不等于本轮进程换页；本轮不清空既有swap或更改系统配置。
- 每次目标fd关闭释放；两端本轮tmpfs根均不存在，`/dev/shm`最终used=0，25315监听
  与诊断进程均不存在。生产PID126766/164834及Oct30 19:36:11/13启动时间保留，
  两份October14未跟踪文档保留。未删除任何其他项目/实验数据。
- 源码已提交并推送、远端SHA回读；结果和路线图另作文档提交。证据不含凭据、大payload
  或构建产物，不写入Git。GitHub备份与服务器原始证据的存放范围明确区分。

原始证据：`/home/Su/experiments/CPNetFlux/2026-11-09-v3-direct-receive-15`，含任务红绿日志、
两端构建/CTest、10二进制匹配、10次双端receipt/计数/资源、独立审计分析、清理和依赖摘要。
复核命令：`python3 <证据目录>/audit_direct_15.py`；测试名单 `related-tests.txt`，
R1命令为 `ctest --test-dir build --tests-from-file <名单> --output-on-failure -j1`；
本轮R1实际分233与12两个集合执行，R2执行完整名单。工具两端必须先固定相同源码/构建。

## 总体进度与下一步

三阶段工程与正确性工作已收口，当前仍处于大文件吞吐性能门；本轮完成一个候选的
可行性实现、双端验收、五对对照与淘汰，没有取得新的完整任务吞吐收益。

1. 直接接收候选关闭，不开展RangeFileCoordinator集成、checkpoint/publish改造和
   后续完整文件候选矩阵；不以“还差一点”继续扩大架构。旧population/sender-inline保持关闭。
2. 下一独立任务先用现有完整V3路径做一次受控测量复核：同源码/配置、CRC/lease/
   checkpoint/发布语义相同，仅对照既有range/payload详情观测开关，新增CPU clocks始终关闭。
   保留最小整任务计时、外部SHA、恢复与资源门，先确认lean完整任务基线；不得把关闭
   诊断的收益包装为算法提速。这一步尚未执行，也未声称无观测可达到30Gbps。
3. 复核后才判断是否有单个可消除的完整路径热点。scope13的非physical range CPU约
   0.702s、租约约0.168s、帧CRC约0.225s都是累计/嵌套测量，不能简单加减推算100G。
   仅有明确机制和预算才再立一个有界候选，否则记录性能门未过并停止扩展三阶段方向。
4. RDMA稳定性、磁盘端到端、跨域与小文件微批仍是独立方向；不将其结果替代当前
   大文件吞吐门，不在本轮自动重开。100Gbps保留目标，不承诺达到。
