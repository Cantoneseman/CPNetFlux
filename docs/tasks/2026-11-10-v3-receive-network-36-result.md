# 接收网络36：定向归因结果与下一开发项

日期2026-11-10，输入965357b8829ce23020da5272dc76d10a1a21dd02。
R770-1唯一写入；R770-2同步固定提交；未使用子代理、worktree或桌面。

## 结论

已交付有界接收网络诊断、相关测试和一次实际采集；本轮不修改传输默认或系统网络。
rx_missed_errors已定位到mlx5接收队列缺缓冲这一层，但本轮未复现，根因仍OPEN。
不能据此认定CPU饥饿、内存分配失败、特定TCP流或某个RSS队列导致历史事件。
下一开发项转回减少接收处理工作量：在现有256KiB原型上支持显式1MiB逻辑DATA，
保持1MiB恢复chunk，完整实现协商、预算及故障测试，再做有界完整任务对照。
不再以复现历史计数为理由串起多轮通用采样；网络稳定性继续作为独立开放门。

## 实际执行与失败

两端同配置RelWithDebInfo，RDMA/TLS/io_uring/ISA-L=ON；每次互传前同提交、三程序SHA相等。
数据R770-2→R770-1，显式bind 192.168.100.2/.1、eno17095np0；TCP，不是原生verbs。
固定256KiB DATA、请求1MiB SO_RCVBUF（实际2MiB/socket）、4 lanes、8个128MiB range、
inline实验mmap、CRC32C/verified_chunks、checkpoint16、TLS/压缩off、commit_sync=none。
驻留1GiB tmpfs源→全新tmpfs目标。外部SHA在任务计时外，内容与发布/manifest均检查。

| 执行 | 固定提交 | 结果 | 带观测T_ready | 带观测Gbps | rx_missed / out_of_buffer增量 |
|---|---|---|---:|---:|---|
| 第1次 | 7c3977ed | 完整通过 | 0.251326s | 34.179 | 0 / 0 |
| 第2次 | 7c3977ed | 采集器ProcessLookupError，中止并清理 | 不作为成绩 | 不作为成绩 | 仅保留原始边界证据 |
| 第3次 | 5f483839 | 修复采集器后完整通过 | 0.278119s | 30.886 | 0 / 0 |

共启动3个任务，达到事先上限，没有补足第三个成功样本或剔除失败。
第2次读取/proc/PID/task/TID/stat时线程退出，内核返回ESRCH，对应ProcessLookupError；
采集器此前只容忍FileNotFoundError。修复仅容忍这两种退出异常，权限错误继续上抛。
回归先红（真实ProcessLookupError），后绿；run_guarded的观察异常清理测试也通过。
两次输入间只改变Python诊断处理和测试，运行程序SHA不变；不把跨提交样本包装为A/B。

原始证据根为两端/home/Su/experiments/CPNetFlux/2026-11-10-v3-receive-network-36。
首轮run.log/failure.json/results.json保留失败；第三次使用remaining/独立目录，未覆盖首轮。
round-summary.json汇总3启动/2完成/1失败；remaining/analysis.json只代表该子运行的1次完成。

## 证据支持的判断

Linux v7.0 mlx5源码将qcnt.rx_out_of_buffer累加到rx_missed_errors；
官方定义是接收队列缺少软件buffer。实机R1前后两项均544380，整个采集窗口增量0。
出处：[驱动源码](https://raw.githubusercontent.com/torvalds/linux/v7.0/drivers/net/ethernet/mellanox/mlx5/core/en_main.c)、
[官方计数定义](https://docs.kernel.org/networking/device_drivers/ethernet/mellanox/mlx5/counters.html)。
实机Ubuntu 7.0.0-38不等于未打补丁上游源码；这说明计数层级，不等于已找到历史触发机制。

R1共32 combined queues，RX ring1024，RSS均匀32队列，RPS off，自适应合并on。
第1次4个主要RX队列为0/6/14/17，第3次为13/21/26/31，各约18.59万包；
四条连接没有全部堆到一个接收队列。IRQ有效亲和、CPU拓扑及配置只读保存，未调整。
两次完整窗口物理/分配/恢复错误无增长，softnet dropped/time_squeeze及受筛选TCP丢弃/重传无增长。
这些零计数不能证明历史异常已修复，也不能排除未采到的短暂CPU服务不足。

两次任务中，实际客户端数据socket Recv-Q采样分别约1.19–1.56MB、1.16–1.56MB；
4个活跃线程在约85ms采样间隔各增加约70–80ms CPU，多数采样为R，个别S/D或迁移。
发送端ss记录rwnd_limited约96.3%–98.2% busy；与接收端消费速度限制一致。
它仍不能直接区分缺页、复制、校验和状态维护，也不能把TCP接收背压等同于硬件RQ缺缓冲。
whole-task边界含SHA与0.5s统计刷新等待，不用于稀释后声称CPU低负载；短间隔原始CPU/线程另存。

观测器分别3/4次，外部命令及读取合计0.0960/0.1142s，约任务墙钟的38%/41%暴露窗口；
它与任务并行，不能当作可直接相减的任务开销，也没有无观测配对去估算扰动。
因此本轮30.886/34.179Gbps只供诊断，不表示优化收益或相对Milestone35退化。
最新无定向观测吞吐仍是Milestone35同提交五对中的1GiB组合候选36.952Gbps。

## 验证、资源与备份

新增receive_network解析/采集/分析，run_receive_network最大3次；复用现有受控驱动，
默认调用不启用observer。计数回退/缺失、IRQ稀疏CPU列、softnet CPU字段、netstat配对、
全局计数不推断流因果、线程退出以及观察器异常清理均有测试。
R1/R2相关4/4 CTest通过（baseline driver、receive network、大帧、buffer中断恢复）；
修复后两端脚本门2/2再通过；Python -W error编译与git diff --check通过。
两次完整任务各1024 verified chunks、4096 DATA帧、SHA、8 range映射生命周期及发布通过。
两端源驻留、MemAvailable最低约27GiB，owned swap0、host pswpout增量0；
原生产服务PID/start_ticks未变。两个批次的自有源、目标、payload根均精确清理，回执保存。
未新增8GiB源；scope34的环境阻塞仍未解释，没有放松资源门。

代码7c3977ed5f8dc5b0a46f450b97ccca65351366f8，修复5f48383936d135142e2ac89a068988a7990533be，
均已push到已核实GitHub origin并回读同SHA，R2均固定同步构建后才互传。
结果文档的最终备份/回读见证据根closure-backup.txt，不在文档中写自身SHA。

## 下一交付及边界

1. 将显式1MiB逻辑DATA贯穿V3能力协商、分类型长度检查、socket/session限制和真实接收路径。
   每chunk从4帧变1帧；wire=1048580B。控制帧/FileBegin上限保持独立，unsupported peer在payload前拒绝。
2. 保持总payload预算64MiB，按当前4 lanes、depth2与额外6持有者上界计算：
   4*(2+6)*(1048576+4)=33554560B，约32MiB；实现时仍需核对所有实际所有权。
   mmap总窗口256MiB及SO_RCVBUF上限不扩大，不减CRC、checkpoint、租约或恢复保证。
3. 先跑短尾/截断/坏CRC/协商拒绝/取消/恢复/默认和旧256KiB兼容；提交固定双机后，
   以256KiB为对照、1MiB为候选做最多一轮五对交错1GiB真实任务，详细采样off，保留接口/资源边界。
   依据配对吞吐、CPU/GiB与内存决定保留，不预承诺40–50Gbps，不以小负载替代8GiB M1。
4. 当前crc32cCombine已缓存64个按字节长度幂算子；256KiB/1MiB长度为2的幂，调用只需一次matrixTimes。
   单纯再缓存这些长度的完整算子不能消除既有多次矩阵应用，应避免重复实现同一优化。
   不同尾长或slice lookup属于另一个候选，需有当前成本依据后单独评估。

上述顺序承接Milestone35的接收处理路线；网络OPEN、8GiB BLOCKED_ENV、RDMA优化暂停、
默认晋级及生产/100Gbps未验收的边界全部保留。
