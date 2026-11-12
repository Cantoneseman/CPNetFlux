# Task55 / 交付C：私有tmpfs完整V3准入与冻结结果

日期：2026-11-12。工程交付完成，完整任务准入失败，M2与100G目标仍开放。
权威目录：R770-1 /home/Su/projects/CPNetFlux；R770-2仅同步固定提交构建测试。
合同见[Task55计划](2026-11-12-v3-private-tmpfs-55.md)，
版本/配置/原始证据hash见[本次冻结清单](2026-11-12-v3-private-tmpfs-55-freeze.json)。
此前Task50、Task51/52的交付C保留原版本，本清单不覆盖历史结果。

## 已交付代码

初版驱动2c4890191e24bc383b0b5297b3be4e821fe91450已在实验前提交推送，
R770-2同步、两端构建/配置/二进制SHA一致后才启动唯一一次1GiB跨机样本。
后续资源报告修正f795dbe37b3dfa3f7579ef0b72e3aa712ada75ed已提交推送并同步两端。

- tools/perf/private_tmpfs.py：默认关闭的有界私有mount namespace启动器；
  root仅创建1280MiB、huge=within_size私有挂载，实际payload/SSH/SHA以Su UID/GID1002执行。
  超时/取消后终止进程组、回收后代、卸载；只清理inode/owner核验的自有目录。
- tools/perf/run_private_tmpfs_v3.py：固定1GiB、四路V3 TCP、256KiB有效DATA、
  CRC32C/verified_chunks、1MiB chunk、checkpoint16、inline mmap接收；
  paging策略、payload、校验与目标清理全程在同一私有namespace。
- tools/perf/run_small_file_baseline.py：单独记录目标目录准备成本；
  CMake与tools/test/run_private_tmpfs_test.py注册对应行为回归。
- 资源门分类修正：网络门先失败时仍独立计算内存/换页门，不把资源状态误归到网络；
  新增回归覆盖正常、内存不足及换页发生的资源结果。

没有修改生产运行时、默认协议/数据帧、全局THP、共享/dev/shm挂载、网卡设置或原服务。
实验发送pacing、exact-read、TLS、压缩和详细receiver profile/range timing均关闭。
退出方式是不传--enable-private-tmpfs；此后端没有默认晋级。

## 验证

当前f795dbe3两端各18/18相关CTest通过，Python私有tmpfs单元测试14/14通过，
CMake构建、Python编译、git diff --check通过。相关集合覆盖映射配置/错误/生命周期、
大帧driver、网络门、V3中断/服务重启/超时恢复；不是全仓库全部785项测试。
两端构建为RelWithDebInfo，ISA-L/TLS/RDMA/io_uring开启；启用RDMA构建不代表实验使用verbs。
三个二进制SHA完全一致，完整值见冻结清单。

初版先有RED再有GREEN；真实私有namespace的正常/故障/超时/取消、外部取消清理通过。
128MiB本机完整V3 mmap闭环通过（该V3测试源+目标最多256MiB）。
验收过程中一次mapped lifecycle因MADV_COLLAPSE返回EAGAIN（errno11）失败；
后续独立重跑与整组通过，原失败日志保留，未将该偶发现象宣称为已修复。本机smoke仅验证机制与清理，不计为跨机性能样本。

## 唯一1GiB跨机结果

条件：R770-2→R770-1，源1GiB全页驻留；全新私有tmpfs目标；
显式bind192.168.100.2/.1，eno17095np0；四路TCP/V3，实验mmap；
CRC32C/verified_chunks，非持久化commit_sync=none。
这是内存文件后端的受限实验，不能外推磁盘持久化、TLS、原生RDMA或生产。

| 检查/口径 | 结果 |
|---|---|
| 客户端启动到退出原始窗口 | 0.187053863s，原始45.922252Gbps；网络门失败，不是合格吞吐成绩 |
| 私有namespace/目标挂载准备 | 0.052808412s，必须计入本合同完整目标任务 |
| 目标目录准备 | 0.000687761s，同样计入 |
| 完整目标任务 | 0.240550036s，35.709554Gbps；低于40Gbps准入 |
| 源/目标独立SHA与verified chunks | SHA通过，1024 chunks，传输后source-check通过 |
| 实际接收目标页型 | 13次采样，目标VMA ShmemPmdMapped峰值209715200B（200MiB）；不是整个1GiB的大页覆盖率 |
| 资源 | 接收/发送最低MemAvailable分别29403664384/29031833600B；owned swap与host pswpout增量均0；源262144页全驻留 |
| 接收窄窗/settled窗口/整体窗口 | OOB和missed各+261，网络停止门FAIL |
| 清理 | 源、目标和自有根目录已删除，私有挂载已卸载，host挂载/全局shmem状态未变 |
| 正式M2 | 未准入，新8GiB矩阵0样本 |

源驻留准备、外部SHA/观测和清理单列；完整目标任务包含必要目标准备，
不允许把这些必要成本移出计时来宣布40G达标。只有一个有观测样本，无A/B、CV或稳定性结论。
接收窗口与settled记录为同一事件的累计边界，不把两个+261相加为522。
OOB是共享设备聚合计数，没有逐RQ/逐流归属；buff_alloc_err/wqe_err等计数不能证明该事件根因。
本轮不声称CPNetFlux本流、某RX队列或大页造成丢包，且不靠调规模/并发重复试验过门。

原始analysis.json在网络断言后未执行资源赋值，错误保留resources=false；
本次独立resource_gate及analysis-reconciled.json复算为true。
原始analysis/results/acceptance/failure均保留，纠正资源分类不改变网络失败或未达40G结论。
子进程exit1是停止门触发，后续清理/源校验依然执行成功。

## 交付冻结与现场

冻结代码/配置/三个二进制、原始与复核证据hash、回归集合和开放边界。
现场复核两端/dev/shm均0B，Task55目录无残留，49337未监听；
原2121仍为R1 PID126766/start1191040、R2 PID164834/start1679976。
两份原有未跟踪Task01b文档保留；payload、构建产物与凭据不提交。

本轮最终提交/推送/远端SHA回读及R2同步回执：
/home/Su/experiments/CPNetFlux/2026-11-12-v3-private-tmpfs-55/delivery.json。
该回执在最终文档提交后生成，避免Git清单自引用最终提交SHA。
可离线核验：
~~~
python3 /home/Su/experiments/CPNetFlux/2026-11-12-v3-private-tmpfs-55/verify-delivery.py
~~~
Windows只保存本报告、冻结清单和小体积JSON查看副本，不覆盖本地源树。

## 总体进度与下一步

M1历史五对8GiB组合中位34.159410Gbps是现有已验收成绩，受测提交506b63b。
Task53已减少ID分配扫描，Task54已证明局部首次大页机制；Task55现已接入完整V3实验闭环，
但没有取得新的合格性能成绩，不能计算跨提交收益或宣布40G/100G完成。

本候选停止追加payload、参数扫描与8GiB正式矩阵；默认关闭并保留工程能力。
下一开发先依据现有接收窗口/源码证据确定可检验的新机制，说明怎样减少接收服务工作量，
以及如何处理OOB停止门；不再原样重跑population/direct-receive/queued-mmap/帧大小候选。
若现有可见性不足以区分原因，应明确记录缺口与单次定向取证范围，再决定新合同；
不以放宽资源/网络门替代解决问题。原生RDMA、微批及策略记忆仍暂停。
40–50、70–85/100Gbps、长期稳定性、TLS/磁盘持久化/反向和生产验收仍未完成。
