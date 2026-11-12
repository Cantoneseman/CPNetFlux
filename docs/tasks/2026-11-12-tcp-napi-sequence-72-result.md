# 阶段72：TCP序列覆盖闭环，本次未复现OOB

输入固定提交b9bf22198875b3b1a0699719f78037d277e433ba，产品运行时代码仍42718ac26be24d53ec17525a7b1b3bcb1e340931。
双端同已提交版本、配置和四二进制SHA；无产品源码/默认值、系统网络或隔离政策更改。
阶段68两端各156/156回归按相同源码和二进制复用，不写成此次重跑。

## 结果与限制

唯一一次1GiB、四TCP、resident tmpfs源→全新native私有tmpfs目标的诊断，
内容/资源/实际页型/应用pacer/清理通过，双端接口、mlx5和HCA错误增量均0，最终刷新无追加OOB。
**PASS_ONCE_INSTRUMENTED / OOB_NOT_REPRODUCED；正式有效性能样本0。**
本次没有丢包，故没有取得失败时的重传序列与NAPI对应关系，不宣布修复或唯一根因。
69/70/71的OOB失败不被此结果覆盖；探针开销未隔离，也不按本次时长或零计数宣布收益。
正式比较尚未完成、稳定性NOT_RUN、上机NOT_READY，M2/100G结论保持开放。

## 序列证据

先完成四流双向合计4096B校准：每流前向960B、回包64B，内容SHA、tuple和SYN→FIN覆盖一致；
四RX队列/NAPI为9/8202、27/8220、7/8200、28/8221，RSS预测与实际任务summary对应。
实际任务前向四流各268505654唯一TCP有效负载字节，合计1074022616；
回向四流各320字节，合计1280；双向唯一TCP字节1074023896。
四流两方向均从SYN ISN归一化至FIN前完整、无序列缺口、无重复提交覆盖。
发送端tcp_retransmit_skb已附着但目标flow事件0，不把未观察到事件称为永久无重传保证。
map最大key数量RX 7053、TX 20109，均低于每map65536上限；
实际捕获无helper错误、丢事件或stderr报告。完整序列闭环独立于内部计数相等，不能仅以无工具报错宣称覆盖。

summary wire_bytes=1074023128，其application_frames_both_directions统计比双向唯一TCP少768B。
此差额显式保留；未抓取payload逐帧解码，不猜测每一字节归属、不强行配平两个统计口径。
它不改变外部文件SHA、SYN→FIN完整覆盖或logical bytes验收，也不作为性能分母。
后续若要比较wire efficiency，须先补清应用计数边界。

## 接收NAPI证据

时间均为R1本机monotonic；已调度至poll入口、poll函数时长、上一轮满budget后的repoll间隔分别记录。
函数时长含观测开销，repoll间隔不等于硬件到包延迟；没有跨机时钟相关或硬件到包时间。

| RX队列 | poll入口/出口配对 | schedule→poll最大us | RX活跃poll最大us | 满64预算poll次数 | 满预算后repoll最大间隔us |
|---|---:|---:|---:|---:|---:|
| 7 | 5210 | 71.273 | 101.743 | 1734 | 621.711 |
| 9 | 5043 | 33.399 | 52.636 | 1770 | 688.122 |
| 27 | 3992 | 31.951 | 71.170 | 2042 | 873.634 |
| 28 | 4658 | 36.239 | 99.761 | 1908 | 606.082 |

共18903对poll；所有目标poll均在其对应CPU执行，RX队列累计745076 packets，不能把RX包数当应用帧数。
本次即使存在满budget及最长约0.874ms的忙repoll间隔，仍未出现OOB；这些现象本身不足以认定失败因果。
不继续RSS/affinity/rate/frame/lane扫描，不据此新增pacer或架构。

## 内容、计时、资源与现场

配置沿用71：4lane、256KiB DATA、1MiB chunk、CRC32C/verified_chunks/checkpoint16，
queued sender/inline+mmap receiver、64MiB/lane、接收请求1MiB、应用pacer每lane1.5e9B/s；
TLS/压缩/kernel pacing/exact-read off、commit_sync=none、不绑核，明确TCP而非verbs。
源为随机8MiB块重复128次、完全物化驻留；8个range对应1024 chunks，外部源/目标SHA一致：
e6d7e0ce667147cebc1667c144ee27315bc64f6d279eec0b27da1a47f3a7ea3d。
原始client窗口0.244568415s/35.122829Gbps；native setup+mkdir+client窗口0.255113519s/33.671029Gbps。
必要RSS/plan预检0.005954422s另列、外部探针握手3.043919325s另列；这些不是严格用户全流程分母，
也不是新正式成绩。源准备1.335641074s另计且双端独立root监测通过。
- r1: root observer 281次，MemAvailable最低28318113792B、MemFree最低15318224896B；host pswpout增量0、owned进程及leaf cgroup swap0。
- r2: root observer 258次，MemAvailable最低28629327872B、MemFree最低6453002240B；host pswpout增量0、owned进程及leaf cgroup swap0。

实际memory.max/high仍max，不能称有限cgroup硬上限；探针5秒、每map65536keys、应用和挂载预算单独有界。
原2121服务PID/startticks保留；本轮源/目标、49337服务、数据进程、探针、namespace均已清理。
只检查owned mountpoint，避免把无关cpnetflux-lab的shared:720字段误判为本轮挂载；未删除该历史路径。
8/64GiB未运行，不改凭据、系统网络/ring/RSS/IRQ/RPS/THP/cache/swap，不触碰CPSS。

## 校准工具修正与验收

初始校准-k产生34条map_lookup_elem返回空指针通知（retcode=0/Success），来自预期缺失键的条件读取，
不是非法内存读取；校准原始记录保留。改为has_key后exact-env dry-run及一次无新应用payload的5秒附着成功，
实际1GiB无这些通知。序列捕获逻辑未改，没有第二轮校准payload或1GiB补跑。
脚本适配时旧hash被替换的错误在断言处提前拒绝，保护hash/长数值重建；未启动负载。
离线审计脚本初始selftest返回类型使用错误已修正，未因此补跑负载。

16项序列行为测试覆盖重排、重复、不同GSO分组重叠、缺口、缺SYN、32位回绕；
56项离线一致性检查通过，含错误计数/缺SYN负例拒绝。只读重放：
python3 /home/Su/experiments/CPNetFlux/2026-11-12-tcp-napi-sequence-72/audit72.py
冻结后不用--record。脚本编译、双端CMake build及四hash核验通过；产品回归明确复用阶段68。

语义依据是本机typed BTF/附着及上游参考：
[Linux NAPI调度](https://raw.githubusercontent.com/torvalds/linux/v7.0/net/core/dev.c)、
[TCP重传trace位置](https://raw.githubusercontent.com/torvalds/linux/v7.0/net/ipv4/tcp_output.c)、
[bpftrace 0.25 stdlib](https://raw.githubusercontent.com/bpftrace/bpftrace/v0.25.0/docs/stdlib.md)。
上游代码不是Ubuntu发行内核逐字构建证明；retcode0重传事件只表示内核尝试成功返回，不能代表对端收到。

## 下一步和完整目标

本次只说明固定运行时能完成一次有探针的零网络错误任务；不再增加诊断负载。
冻结提交后，串行交还既有“配置和实验”聊天，按新范围从0做最多五对无BPF完整CP/GridFTP交错配对；
首个CP若出现任何硬门失败立即停止，不利用本次仪器扰动来放宽正式门。
正式比较至少5对、CV<=5%、完整性/资源/网络硬门全过，才做目录/反向/安全组和
>=1800秒且>=100完整任务的常驻服务、故障/取消/恢复/TLS/重启/坏数据验收，再生成上机结论。
8GiB因资源门不准入仍NOT_RUN；旧64唯一有效配对不拼入新版样本；默认、RDMA/微批/策略记忆保持边界。
没有通过全部要求前，不把历史工程交付C或本次诊断当作整个用户目标完成。

原始小证据在本实验目录；GitHub只备份紧凑报告/清单，完整证据另归档Windows。
最终提交、GitHub远端SHA回读、双端最终构建和归档验证在冻结清单外记录，避免自引用。
