# Task56 / 交付C：内核TCP pacing、完整任务结果与冻结

日期：2026-11-12。状态：ENGINEERING COMPLETE / NOT ADMITTED / M2 OPEN。
本轮完成代码、正确性回归、唯一可行性样本和最终配置/证据冻结；未达到40Gbps，
不把工程收口当作性能目标完成。Task55及更早交付C保留各自历史版本。
合同见[开发与单样本合同](2026-11-12-v3-kernel-tcp-pacing-56.md)，
完整配置/版本/证据hash见[冻结清单](2026-11-12-v3-kernel-tcp-pacing-56-freeze.json)。

## 实现和版本

输入b26018fa9742cd8fe3fb6eef66d6f73520341b0e；实现固定提交
423f20cdbf06e72ace57a93864a5dcd1b00d296e，已在互传前推送GitHub并回读相同SHA。
R770-1唯一写入；R770-2同步该提交、RelWithDebInfo构建及三个二进制SHA核对一致后才传输。

- FramedDataSocket新增Linux SO_MAX_PACING_RATE显式候选，默认off；只允许2,000,000,000B/s。
  查询TCP协议、set/get一致性核验；失败显式返回，回读失败尝试恢复，恢复失败关闭socket。
  重置恢复原上限，move/close保留socket所有权边界。
- V3 sender最多四lane、plain TCP、256KiB逻辑DATA、无压缩；与旧应用pacer互斥，TLS/RDMA拒绝。
  每lane回执标明socket ceiling配置，不冒称实际发送速率或精确线速整形。
  四路合计配置上限64Gbps，无逐DATA帧应用timer等待，不增加线程、fd、payload池或队列深度。
- 私有tmpfs启动器两次转交都传递显式开关；性能驱动保留四lane机制回执和独立验收门。
  旧应用pacing回执必须缺席。默认不传开关，已有默认协议/帧/后端不晋级。
- 未修改qdisc/NIC/IRQ/RSS/MTU/sysctl/全局THP或共享/dev/shm；原2121服务保留。

## 代码验收与失败记录

新增8个C++行为测试、真实本机V3四路upload/download内容与回执测试、Python driver回归；
17个private tmpfs driver单元测试通过。单元及实际驱动缺陷均留有RED日志。
R770-1相关集合初次258/259通过：新增flag传递回归捕获启动器第一跳漏传开关；
修复后相关5/5复测通过，包含该回归和真实双向传输。
新smoke初次还发现测试helper返回tuple被当dict使用，修正测试后通过；原失败日志保留。
R770-2最终相关集合259/259通过。候选显式开启的两项中断恢复两端各2/2通过。
不把R1初次失败写成259/259单轮成功；上述是相关集合，不是全仓库全部测试。

CMake构建、git diff --check通过；最后收口只改文档，运行时/测试/tools/CMake对象与实机输入一致。
回归命令及各次日志/覆盖范围固定在freeze；原日志在实验根，不依赖聊天摘要。

## 唯一1GiB完整V3样本

R770-2→R770-1，直连192.168.100.2/.1显式bind、eno17095np0；四路V3 TCP。
源1GiB全页驻留，全新私有tmpfs目标，huge=within_size，挂载上限1280MiB，工作用户Su/1002。
256KiB有效DATA+4B CRC、1MiB chunk、CRC32C/verified_chunks、checkpoint16、commit_sync=none；
queued发送、inline实验mmap接收，64MiB/lane映射窗口、64MiB payload预算，
SO_RCVBUF请求1MiB、四路实际2MiB；TLS/压缩/exact-read/应用pacer关闭。
10ms资源保护及低频ss沿用，详细receiver/range profiler关闭；存在观测开销。

| 项目 | 结果 |
|---|---|
| 完整目标任务 | 0.242736980s / **35.387828Gbps**，未达40Gbps准入 |
| 客户端启动至成功退出窗口 | 0.187952127s / 45.702779Gbps，仅子窗口 |
| 必要私有namespace/挂载准备 | 0.054108289s，已计入完整任务 |
| 目标目录创建 | 0.000676564s，已计入完整任务 |
| socket ceiling | 四lane均set/get回读2e9B/s；应用pacer未启用，只证明配置 |
| 完整性 | 独立源/目标SHA树一致、传后源校验通过、1024 verified chunks |
| 源驻留 | 262144/262144个4KiB页驻留 |
| 目标页型 | 13次采样，实际目标VMA ShmemPmdMapped峰值196MiB，不代表全文件覆盖率 |
| 两端最低MemAvailable | 接收29496406016B；发送28999270400B，均>=12GiB |
| 换页 | 自有负载swap为0，host pswpout增量0；不把其他既存服务换页计为自有负载 |
| 网络 | 接收窄窗、settled、整体OOB/missed增量均0，发送服务窗口missed为0 |
| 清理 | 源/目标及自有路径已删除、进程回收、私有挂载卸载；host挂载/全局shmem状态未变 |
| 正式M2 | 未准入；新8GiB样本0 |

完整目标任务分母=必要namespace/挂载准备+目标目录创建+client创建至成功退出。
源准备、外部SHA/观测、清理单列；不把45.70Gbps子窗口当完整任务达标。
SHA树为4081503f9953959b162a537177ac8f4708d9a33b02fe6eaec385b51582c5a783。
所有工程/资源/网络/内容门通过，但速率门失败；程序正常exit0表示流程执行成功，
acceptance.json的formal_admission=false才是准入判定，m2_pass仍为false。

本样本没有A/B、CV、长期稳定性或OOB逐流/RQ因果证据；一次0事件不能关闭历史OOB问题。
不能跨Task55/51/M1不同提交、配置或负载计算收益；也不能据此声称TCP、磁盘持久化、TLS或原生RDMA普遍达到该速率。

## 交付C、回滚和后续

配置/代码对象/二进制、回归和原始证据hash已冻结；最终文档提交不再修改运行时代码。
不启用环境变量/不开启--kernel-tcp-pacing即可退出候选；不改系统网络即可恢复默认路径。
两端最终/dev/shm均0B、49337无监听；原2121仍为R1 PID126766/start1191040、
R2 PID164834/start1679976。两份原有未跟踪Task01b文档保留，无凭据/大payload/构建产物入库。
最终提交、GitHub SHA回读和R2同步见本实验根delivery.json；避免清单自引用最终提交SHA。
离线验证：python3 /home/Su/experiments/CPNetFlux/2026-11-12-v3-kernel-tcp-pacing-56/verify-delivery.py。
Windows仅复制报告、冻结清单与小体积JSON到_codex_stage56，不覆盖源树。

本候选停止追加payload或rate/lane/frame扫描，不运行8GiB；保留默认关闭的机制。
下一步先核对必要目标准备路径能否减少实际重复进程启动工作：本次54.108ms中实际mount子区间
2.797ms，剩余约51.311ms包括sudo/unshare/Python启动等，尚未拆分，不能全部归因Python。
完整任务距40Gbps允许时间214.748ms仍差27.989ms；这是时间预算，不是优化收益预测。
新的启动路径实现须先写安全/所有权/失败清理合同，所有必要工作继续计时；不得通过提前准备或
改分母获得达标。若这一项无可减工作，再审查TCP接收与目标页管理的数据搬运，
不原样重跑已关闭的population/direct-receive/queued-mmap候选。

历史已验收M1仍为506b63b的五对8GiB组合中位34.159410Gbps；本次不是新的M1/M2正式成绩。
40–50、70–85/100Gbps、长期稳定性和生产验收仍开放。原生RDMA、微批和策略记忆继续暂停。
