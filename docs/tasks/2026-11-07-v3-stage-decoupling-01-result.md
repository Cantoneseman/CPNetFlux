# V3 三阶段执行收口 01 结果

日期2026-11-07。输入`63586ed792646f2e64f2247bd0188c719aba15f6`；性能矩阵实测`8d44802a33cd8d3bfc17ad9e5bc5a8da8c23ee2f`；上传控制`ccca9b66bab457ecb6e6c3780d7c6fe1d1e33179`仅增加guard脚本识别，八个传输/测试二进制与性能矩阵完全一致；实现`19d6eb2e8b7f79b1e0ea8d67c215c81420d790ee`。R1单人串行开发，R2只同步明确提交；原有两份10/14 untracked文档保留。

## 结论

**三阶段工程解耦与有界性验收完成，吞吐性能门仍开放。** 源read/CRC活跃并发、网络lane和接收payload write并发可独立配置，接收batch共享就绪writer池、预算与观测已接通V3上传/下载。源端仍是每lane producer加共享读准入器，没有全局读线程池，也没有消除每lane原有线程。

c4在相同read4/write1下，admission→共享就绪池的task中位数8.404→14.226Gbps（+69.29%），双端CPU2.950→2.800秒/GiB。相对每lane默认，池的task中位数变化+0.75%，双端CPU变化-32.04%。这是三次小样本中位数，需结合CV；不能宣称长期吞吐提升或所有负载收益。

因此保留pool作为显式写上限低于lane数时的执行器，read4/write1可作为CPU开销对照配置，不推广为通用吞吐默认。默认read0/write0、depth2、batch1、AvailableCapacity唤醒、preallocate off不变，完整性/持久化策略不改。工程交付不等于全部性能瓶颈或100G已解决。

## 交付内容

- 新增transfer_stages.h/.cpp及单元测试，DynamicFileQueue/PersistentTransferContext共享本batch控制器；严格解析read/write 0..64、executor pool/admission和payload预算，创建时冻结配置。
- 源读许可只覆盖read+CRC，网络/队列等待前释放。读及诊断写准入使用队首单独CV、最多32次就绪绕过，之后让队首执行；截止移除等待者并唤醒后继，RAII覆盖错误/异常/移动释放。
- 只有显式写上限小于lane数才创建共享ReadyIoExecutor。固定writer数，active+queued最多lane数，仅派发已有payload的存储/认证回调；池内无网络或空payload队列等待。提交者等完成才回收payload，借用捕获在完成通知前销毁，容量先释放再通知，异常/过期转换错误、配平finishIo，析构排空/join。
- 请求上限达到自然lane数时跳过准入/派发；发送端不创建空闲writer池。未增加payload复制、未扩大队列。预算`lanes*(depth+6)*65536`在FILE_BEGIN/DATA前校验，4lane/depth2为2MiB；配置不足明确拒绝，不等待整条stream的预算而产生跨端死锁。
- stage_execution报告请求并发/是否需准入、实际executor/worker、读写调用/等待、budget/bound、pool CPU。池内CPU与等待writer线程CPU分别记录；主要比较双端进程CPU，累计重叠计时不能直接从task wall相减。
- 既有V3 attempt/epoch/lease、CRC、checkpoint、缺失块恢复、原子发布和FILE_END/RESULT/DirectoryEnd不变。后台checkpoint、最终复读及元数据IO不属于payload写并发上限；预算不含transport/RDMA注册池、元数据、栈、tmpfs，也不是整台服务器的总内存上限。

详见[执行与预算指南](../V3_TRANSFER_STAGES.md)。

## 最终同提交对照

TCP显式bind192.168.100.2→192.168.100.1，route与eno17095np0计数留证；没有应用原生QP创建。每次任务先核对双端同commit、tracked clean、同CMake配置/版本、八个二进制SHA-256。上传控制采用仅修改guard脚本的后继提交，八摘要与矩阵构建逐一相同。1GiB总文件，源/目标tmpfs，压缩off、CRC32C、POSIX direct（非O_DIRECT）、every16 checkpoint、commit_sync=none、depth2、batch1、preallocate off。

源为随机8MiB块重复128次；性能样本verified_chunks，full仅独立完整性控制，默认full未改。task计时含CLI启动/连接/传输/提交终态，外部SHA在计时外。下载性能样本源页传前后全驻留、发送物理读0；上传为独立full控制，源在计时前预热并做完整SHA。每次输出SHA后清理。三轮lane顺序轮换、配置顺序交替，保留波动，不当作长期稳态性能。

pool-unrestricted为read0/write0；pool-r4w1为read4/write1、pool；pool-r4w2为read4/write2、pool；pool-admission为read4/write1、admission。

| lane | 配置 | n | task Gbps中位数 | CV | 双端进程CPU秒/GiB |
|---:|---|---:|---:|---:|---:|
| 1 | pool-unrestricted | 3 | 21.146 | 18.33% | 1.200 |
| 1 | pool-r4w1 | 3 | 16.262 | 11.31% | 1.440 |
| 2 | pool-unrestricted | 3 | 16.642 | 9.20% | 2.430 |
| 2 | pool-r4w1 | 3 | 16.235 | 6.06% | 2.170 |
| 4 | pool-unrestricted | 3 | 14.121 | 6.04% | 4.120 |
| 4 | pool-r4w1 | 3 | 14.226 | 0.43% | 2.800 |
| 4 | pool-r4w2 | 3 | 13.630 | 3.21% | 3.280 |
| 4 | pool-admission | 3 | 8.404 | 1.07% | 2.950 |

c4的pool/admission对照只改writer执行器；双writer单列作并发候选。read4在1/2/4lane不额外限读，不是读线程池收益实验。c1的r4w1跳过准入和池派发，两组差异不能归因于writer池。高CV与三次样本限制均保留，不承诺默认提速。

最终默认full下载一次11.481Gbps；read4/write1/pool full上传一次11.321Gbps，协议终态与独立SHA通过，实际共享writer池证据保留。两次控制不混入verified_chunks中位数，也不是双向重复性能结论。

## 负结果和迭代边界

1. f8eae6f严格FIFO/广播准入24样本，4对/1、2、4lane。默认→read4/write1 task中位数18.822→15.659、18.037→10.568、14.635→6.524Gbps；不推广初始默认。
2. e336357去掉自然上限多余锁与禁用gate共享引用计数，31样本含控制；严格FIFO多lane仍负收益。
3. f6a3675队首单独通知/最多32次就绪绕过，37样本。c4默认14.133Gbps/双端4.150CPU秒，read4/write1为8.914/2.850；CPU下降不能抵消吞吐损失，不推广。
4. 19d6eb2e共享就绪池，8d44802a修正远端角色观测断言，最终25下载+1上传控制。额外read-cap配对仅准备，未执行。

本轮117下载+1上传共118正式样本全部pass；不同提交组分别解释，不以跨组差异作A/B。256MiB局部写调度/ready-burst探针仅选候选；局部c4 burst约18.039→25.104Gbps不是网络收益。原始stdout/stderr分开，避免JSON/trace交织；脚本与大证据保存在仓库外。

## 测试与失败记录

性能矩阵提交R1 CMake构建与全量CTest **607/607**，R2同配置构建相关 **270/270**；后续guard脚本提交只改Python与记录，两端重构建、四项guard测试和八个原二进制摘要复核均通过。显式read1/write1/pool/depth2另跑中断、服务重启、超时、坏checkpoint、数据故障、终态回复丢失、lane恢复、统一执行器和完成文件审计 **9/9**。

测试覆盖cap/公平/超时后继/异常继续、有界排队、容量释放与borrow生命周期、真实上传/下载写并发、预算不足在帧/输出前拒绝、既有Entry锁外写入和attempt排空。初始准入行为测试6/8 RED后修复；池化初次R1 602/607、R2 265/270，五项相同观测断言用本地列表判断远端角色，改为measurement_scope后两端整组重跑全过。失败日志保留，不改写首轮结果。

验收日志：acceptance/pool-role-full-ctest-r1.log、pool-role-related-ctest-r2.log、pool-enabled-recovery-ctest-r1.log。构建配置与八摘要见matching-builds-*.json。额外上传首轮因下载专用CLI参数在DATA前拒绝，第二轮协议/输出SHA成功但guard未识别上传客户端；两项保留在acceptance/upload-cli-rejected*与upload-memory-unobserved*，不混入118个验收完整样本。ccca9b66为guard补上传精确binary/owned源路径识别，行为测试RED→GREEN，两端各4/4；最后guarded上传重跑全部通过。

## 内存、清理和备份

每样本MemAvailable门槛12GiB、owned RSS门槛4GiB、50ms采样；118项owned VmSwap与host swapout增量0。R1最低可用27.401GiB/最高owned RSS13.85MiB；R2为27.020GiB/12.51MiB。RSS不含tmpfs；最多每端1GiB源或当前输出，预算不可冒充整个任务内存。

每次输出SHA后删除，最终source按inode/size/SHA确认删除，本轮精确tmpfs根和探针已移除。清理/空间/生产PID核验在acceptance/final-fixture-cleanup.json；R1 PID126766、R2 PID164834未操作；CPSS/共享数据/历史证据保留。最终GitHub push与远端SHA回读、双端提交/八摘要回读见acceptance/final-verification.json与HANDOFF.md；只有确认推送内容属于GitHub备份。

证据根`/home/Su/experiments/CPNetFlux/2026-11-07-v3-stage-decoupling-01`。metrics/analysis.json包含逐样本/中位数/CV/CPU，metrics/final-quality.json含内存汇总；两主机日志/快照含路由、接口和资源采样。

## 下一步，尚未执行

三阶段工程归档，吞吐门继续开放。只在新profile支持下选择64KiB逐帧跨线程交接、kernel pwrite/同inode争用、socket/frame中的一个因素，不盲目减少writer或增加缓冲。全局源读线程池需要独立供数/线程数证据。小文件自适应微批单独立项，固定成员完整性/恢复和T_ready验收；RDMA优化仍暂停，CQ/peer-mapping、长期双向负载、持久化、100G/1Tbps未验收。
