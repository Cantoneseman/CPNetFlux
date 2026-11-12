# 阶段71：取得TCP提交/RX补给证据，网络门仍失败

输入提交65e93483ddace78d60950315b69a7903702428be，运行时代码仍为42718ac26be24d53ec17525a7b1b3bcb1e340931。
双端固定同提交、同配置构建成功，四二进制SHA相同。没有产品源码、协议、默认值或系统网络改动。
阶段68双端各156项回归按相同源码和二进制复用，本轮没有重跑这些测试。

## 结论和新证据

唯一一次实际1GiB任务：**FAIL_NETWORK，R1 rx_out_of_buffer/missed各+8，有效性能样本0。**
最终刷新没有继续增量；R2接口与两端HCA错误增量0。内容、资源、实际大页、native setup及应用pacer门通过。
正式比较仍NO_GO，稳定性NOT_RUN，上机NOT_READY。不能由+8小于69的+132或70的+328宣布改善：
本轮有BPF观测扰动、没有匹配A/B、源文件内容也不同。

先用总量4096字节的显式bind TCP校准四队列：9、27、7、28均有typed探针记录，
发送TCP有效负载合计与接收SHA/长度完全对应，接口错误和swap无增量。
真实RQ布局四队列一致：wq_type=2、sz_m1=7（8个槽位）、128 strides/WQE、stride=2048字节、min_wqe_bulk=2。
不能把ethtool ring=1024直接解释成1024个独立WQE，也不能用理论槽位数代替实际硬件可用空间。

实际1GiB取证：R2 net_dev_start_xmit按.2->.1和受控端口过滤，19518次提交、其中19505次带负载，
TCP有效负载合计1074034200字节（含重传和协议开销），四条tuple与V3 summary完全相同。
固定10微秒桶的最大合计有效负载为501828字节；单流最高325800字节。
大量提交为32–63个GSO segment，MSS=1448。**这证明有成批的驱动提交，不是线上的瞬时发送速率，不能宣布超过100G。**
一条流比另外三条多11584字节，恰为8×1448；未记录TCP序列/重传事件，因此仅保留为线索，不当作已证明的重传/逐RQ归属。

R1 fentry/fexit mlx5e_post_rx_mpwqes：

| 队列 | 配对入口/出口次数 | 入口软件WQE最小值 | UMR in progress最大值 | 单次函数观测最大耗时 |
|---|---:|---:|---:|---:|
| 7 | 5089 | 5 | 2 | 24.510 us |
| 9 | 6136 | 4 | 2 | 30.395 us |
| 27 | 3715 | 5 | 2 | 49.234 us |
| 28 | 5931 | 5 | 2 | 35.540 us |

共20871对。未见入口软件记账归零，但cur_sz可能滞后于硬件消费，不能据此排除硬件耗尽。
补给调用最大间隔包含无数据和连接收尾；例如q28约22.2ms的尾部空档，不能直接称为NAPI调度延迟。
当前没有序列级唯一字节覆盖证明，也没有硬件到包时间戳。map内部字节/调用/hist计数一致、无工具丢事件报告，
不等于证明捕获了每个唯一TCP字节。两端时钟只分别解释，没有同步跨机事件或单向延迟结论。
BPF开销未隔离，所以既不能按本次时长报新性能，也不能由本次单独证明无观测时的根因。

上游语义参照：[Linux v7.0 en_rx.c](https://raw.githubusercontent.com/torvalds/linux/v7.0/drivers/net/ethernet/mellanox/mlx5/core/en_rx.c)、
[wq.h](https://raw.githubusercontent.com/torvalds/linux/v7.0/drivers/net/ethernet/mellanox/mlx5/core/wq.h)。
本机布局/附着由当前运行内核BTF验证；未取得发行版完整构建源码，不把上游文件当作发行版二进制逐字对应证明。

## 任务、计时与资源边界

.2 resident tmpfs源 -> .1全新native私有tmpfs，4TCP、256KiB DATA、1MiB chunk、CRC32C/verified_chunks/checkpoint16，
queued sender/inline+mmap receiver，每lane64MiB、接收请求1MiB、应用pacer每lane1.5e9B/s。
TLS/压缩/exact-read/kernel pacing off，commit_sync=none；接收mask为0–31，没有沿用70绑核候选。
这是TCP/tmpfs非持久化诊断，不是native verbs或磁盘成绩。
1024个verified chunks及外部文件SHA一致：5030a73dac4cfb02a712110025172c73af5823ed6cb07c3a88be2cc6af4a6e08。
源为随机8MiB块重复128次并全部物化驻留，压缩关闭，不与前轮声称bitwise相同。

原始client窗口0.256009125s/33.553236Gbps；native setup+mkdir+client窗口0.267784805s/32.077752Gbps。
必要RSS/plan预检0.005696005s另计，外部探针握手1.600650549s另计；这些原始字段不是严格用户全流程，
全部标记为失败诊断数字，不作为比较分母、正式性能或稳定吞吐。
成功准备源1.338791165s另列；之前准备及工具校准失败也保留，不能隐藏。

双端root observer实际243/221次，覆盖真实client/server和BPF进程。
MemAvailable最低R1 29066395648B、R2 28832579584B；MemFree最低R1 16084967424B、R2 6661767168B。
全程host pswpout增量0，owned进程及leaf cgroup swap0；共享祖先历史swap不称为0。
实际memory.max/high仍为max，没有新增有限cgroup强制上限；应用预算、BPF每map最多32768keys和5秒捕获限制另列。
map数量未达到key上限；采样不是严格瞬态峰值证明。两次源准备也有独立root资源和网络边界记录。

## 准备失败与清理

保留所有失败而不反复发送1GiB：初始BPF tuple对齐和64字节str缓冲触发校验器拒绝，改为64位key及ifindex过滤后通过exact-env dry-run；
第一次校准发送1024B后，脚本在EOF后查询getpeername报错，剩余三流只补3072B，总量仍4096B。
初始native隔离子进程具有NoNewPrivs=1，无法sudo启动探针；当时client未启动、无1GiB传输。
将探针启动移到外层Su监测进程，保留客户端NoNewPrivs=1、UID/EUID=1002，再执行唯一实际任务；
没有修改隔离或提权规则。准备错误的标记、日志、watch、namespace与cleanup均保留在preparation-*目录。

两端本轮源/目标roots、挂载、49337监听、数据进程和BPF探针已退出/清理；原2121 PID/startticks保持。
未更改系统网络/IRQ/RSS/RPS/ring/THP/cache/swap/信任，未改CPSS或历史证据。8/64GiB未运行。

## 验收和下一步

64项离线一致性检查通过，包括错误字节计数/缺失流的负例拒绝；这验证执行与失败记录一致，**不是网络PASS**。
可只读重放：python3 /home/Su/experiments/CPNetFlux/2026-11-12-tcp-rx-service-71/audit71.py。
--record仅在冻结前生成audit.json，冻结后不要使用。bpftrace exact-env dry-run、脚本编译与双端CMake构建成功；
产品回归明确复用阶段68相同源码/二进制的各156项，不写成此次重新运行。

下一步先补两个缺口：目标连接的TCP重传/序列事件，以及NAPI poll自身耗时、返回work/budget和活跃区间。
已只读确认mlx5e_poll_rx_cq、mlx5e_napi_poll、tcp_retransmit_skb typed/tracepoint入口可用，尚未启动下一轮探针或payload。
先校验探针及开销/过滤，再按新明确范围决定是否需要一次定向诊断；不要由当前TX桶峰值加入新pacer，
也不要把post间隔直接当RX服务迟到。停止RSS/affinity/rate/frame/lane扫描。

完整用户目标保持开放：解决网络门后重新做最新固定版本五对CP/GridFTP、目录/反向/安全组，
再做>=1800秒且>=100任务常驻服务及故障/取消/恢复/TLS/重启/坏数据，最后上机报告。
历史工程交付C的闭环不替代这些要求。本阶段没有新的正式性能、M2或100Gbps结论。

原始证据在/home/Su/experiments/CPNetFlux/2026-11-12-tcp-rx-service-71。
冻结清单记录脚本、小证据和报告hash；GitHub备份仅限明确提交的报告/清单/导航，完整原始证据另存Windows归档。
最终提交、远端回读、双端同步及本地核验回执在冻结清单外保存，避免自引用。
