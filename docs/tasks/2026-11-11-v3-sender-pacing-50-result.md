# V3发送节奏50与当前性能阶段收尾结果

日期：2026-11-11
输入：ea69f952d0f51518d55487a123c47ecc64332bbb
冻结运行时代码/实机输入：057159ce02099e4a88c94e591f02880ca1669834
结论：**ENGINEERING CLOSED / M2 OPEN**。
本次工程、回归、证据与版本收尾完成；M2和长期稳定性未通过，不是100G性能目标完成。
统一工作单元/预算采样/策略记忆保持DEFERRED，本轮没有实施，也不自动解禁。

## 交付A：实际生效的有界发送候选

V3 sendPersistentTree 的writePayload前增加每lane独立的单调时钟DATA准入pacer，
显式环境变量CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND=1500000000。
off为默认；非法值和超过4路、TLS/RDMA/压缩组合在该sender入口拒绝。
plain TCP upload/download sender共用该实现；控制/结果帧不节流，不改协议或CRC。
每路12Gbps应用帧准入上限、4路合计48Gbps；不是吞吐承诺。
首次DATA允许提交，此后按上一帧完整应用wire字节数计算间隔。
实际准入时刻重建deadline，不积攒信用或补发追赶；单次sleep请求不超过1ms，醒后重检既有失败/总deadline，
进入WorkExecutor IO许可前完成等待。短写和错误继续由原socket路径处理，未增payload缓存、
队列深度、线程、内核socket容量；只有固定小型lane计时/计数元数据。
不提供跨lane带宽公平/同步保证。

该机制证明的是应用准入，位于租约IO许可与实际socket write之前；
租约等待、阻塞send、内核排队、TSO仍能改变网卡输出，不是线速包整形。
本轮只读发现qdisc为mq+fq_codel，未改qdisc/NIC/IRQ/RSS/MTU/THP/swap。
没有将socket参数读回等同于pacing生效。

6个新增C++行为测试包含非法值的红→绿回归、间隔/暂停不追赶、等待取消不提交、
默认关闭零时钟/等待/计数、更大lane拒绝及真实V3内容往返。
5个Python机制门测试拒收缺lane、漏帧/字节、未实际等待、间隔违反或伪造间隔统计。
支持边界在代码显式检查；本轮未运行原生RDMA互传或加密性能测试。

## 一次1GiB可行性结果

R770-2→R770-1，显式bind 192.168.100.2/.1、路由eno17095np0；
驻留tmpfs单源/全新tmpfs目标，4 lanes、256KiB DATA、1MiB chunk，
queued sender/inline实验mmap receiver、请求1MiB接收buffer/实际2MiB每socket，
CRC32C/verified_chunks、checkpoint16、commit_sync=none、pending-window2、depth2/batch1，
payload预算64MiB、映射64MiB/lane总256MiB，TLS/压缩off。
phase-timing on保留M1式file汇总用于核实后端，range/receiver详细profiler off；
沿用10ms资源保护采样及低频ss，非零观测开销。

| 项目 | 结果 |
|---|---|
| 完整任务T_ready / task_rate | 0.304377075秒 / **28.221358624Gbps** |
| 计时/字节 | 进程创建至成功退出；成功有效字节1,073,741,824，包含内部准备/校验/checkpoint/发布，外部SHA不在其中 |
| 实际DATA帧 | 4,096；8个range，4 lanes各1,024帧 |
| paced DATA wire字节 | 1,074,020,352；包括4096×68B头和CRC，不包括控制/结果帧 |
| 机制实际等待 | 四路分别1015/1001/934/989次；总等待0.495288269 lane秒，彼此重叠，不能当墙钟损失 |
| 首末准入跨度 | lane0 238.996ms、lane1 243.996ms、lane2 291.797ms、lane3 257.824ms |
| 规定间隔 / 违例 | 每lane累计至少178.829ms；四路spacing_violations均0，逐lane实际跨度均覆盖规定间隔 |
| 窄窗口 / 0.5秒刷新尾段 / 累计 | R1 OOB、missed三个窗口均0 |
| 本地SHA后宽窗口 / R2服务窗口 | missed增量均0 |
| 完整性 | source/target SHA树一致；1,024 verified chunks；member与tree manifest完成 |
| 源驻留 | 262,144/262,144页驻留 |
| 客户端采样CPU下界 | 0.96 CPU秒/GiB；不是完整rusage |
| 服务进程采样CPU下界 | 0.85 CPU秒/GiB，含该次服务生命周期，不与客户端墙钟或其他合同直接相加 |
| 最低MemAvailable | R1 29,634,555,904B；R2服务29,030,772,736B，均大于12GiB |
| 采样RSS峰值 | 客户端251,748,352B，服务14,749,696B；tmpfs与映射预算另计 |
| 资源 | 源准备、客户端、服务及整轮两端host pswpout增量均0，owned swap峰值0 |
| 清理 | 两端marker自有payload目录均移除；/dev/shm均0B；49337临时服务停止 |
| 外部2121服务 | R1 PID/start 126766/1191040、R2 164834/1679976均保持 |

SHA树：6cc70017200433b604d821184e0c2668111811de758a2cce1f532ef8fdeb51b0。
四路均实际等待且全部4096帧计入准入，故不是只设置了未生效参数。

## 交付B：M2判定与候选处置

本轮唯一候选样本的网络/资源/内容门通过，但28.221Gbps低于事前合同40Gbps速率准入线；
没有进入8GiB五对交错正式矩阵，正式8GiB样本数为0，没有新的正式基线或配对收益。
单次无事件不能证明pacer消除了OOB，也不能证明长期稳定。
Task49是1-lane pwrite，本次为4-lane inline mmap，不能相减计算pacer收益。
历史M1是8GiB五对，本次只有1GiB单次，不能用28.221/34.159算退化百分比。

等待确实给发送路径添加了节奏成本，各lane0.112–0.128秒实际等待且结束时间分散。
其中有主动限速、调度、生产者/内核背压等重叠工作；未测出各项因果占比，
不将全部等待当成可消除墙钟，也不声称只改sleep方法就能到40G。
不做busy-spin、速率/lane/frame/queue扫描或再次缩负载；
保留显式关闭的诊断能力，**不作为性能推荐、不改默认**。

历史最好已验收参考保持M1-44：输入506b63be71bc5aea5eca92cab549c6d4adb2ff51，
8GiB combined中位34.159Gbps、对照28.126，两组中位数之比提升21.451%，
5/5配对改善、CV3.238%；TCP/tmpfs、非持久化、TLS off。
它不是057159c上的新8GiB成绩。40–50、70–85/100Gbps均未验收。

### 2026-11-12交付B离线复核

输入提交a75dac30d68d3e3ba404afdaddc89e293ea4445a；未改运行时/默认值，无新payload。
B的成绩复核与判定报告已交付，结论为**NO-GO / NOT ADMITTED，M2 OPEN**。
不能把A工程完成解释成A性能准入通过；本次不是环境BLOCKED。
40Gbps准入已存在于实测代码提交057159c的任务计划，非结果出来后增设。

复算使用目标actual_tree的成功有效字节/T_ready，并核对成功退出、SHA、verified chunks、
输入提交/匹配构建、驻留/资源/网络/输出移除证据；历史M1四份公开记录的SHA-256均匹配。
没有重写原始数据或用planned bytes替代成功字节。

| 数据组 | 输入提交 | 负载/样本 | 中位T_ready | task_rate中位/单次 | 判定 |
|---|---|---|---:|---:|---|
| 历史M1 default | 506b63b | 8GiB，5次 | 2.443273117秒 | 28.125990605Gbps | 历史对照 |
| 历史M1 combined | 506b63b | 8GiB，5次 | 2.011729058秒 | 34.159409520Gbps | 历史M1通过 |
| A paced combined可行性 | 057159c | 1GiB，1次 | 0.304377075秒（单次） | 28.221358624Gbps（单次） | 未达40Gbps准入 |
| 本候选新正式M2 | 未运行 | 0次/0对 | 未测 | 未测 | NOT RUN / NOT ADMITTED |

统计口径明确为：

- 历史两个中位吞吐之比增加21.4514%；不是配对提升的中位数。
- 原analysis的pair_improvements为1−候选时间/对照时间，五对为16.697%、17.135%、15.863%、21.644%、17.727%。
- 按候选吞吐/对照吞吐−1重算，五对为20.043%、20.679%、18.853%、27.622%、21.546%；仍为5/5改善。
- 旧CV为各臂T_ready的总体标准差/均值：default 1.8518%、combined 3.2380%。
  同样用总体标准差/均值计算task_rate CV，分别为1.8583%、3.2616%。未据此改旧门或旧结果；
  未来正式矩阵须在运行前冻结CV统计对象/定义。
- M1接收CPU采样下界中位为7.99/6.77 CPU秒，按8GiB折算0.99875/0.84625 CPU秒/GiB；
  不等于完整CPU开销，更不能作为A相对收益。

本候选正式M2的中位数、CV、改善对数和矩阵完整性/网络门均标记未测（机器报告为null），不计为pass。
单次A无同提交对照，pacer吞吐收益保持未知，不用28.221与历史34.159相减。
若未来恢复B，必须先有新机制/改动及原门内可行性准入，再冻结同一新提交M1 combined与候选的8GiB五对合同。

离线复核命令：

~~~bash
python3 /home/Su/experiments/CPNetFlux/2026-11-11-v3-sender-pacing-50/b-acceptance-2026-11-12.py
~~~

脚本及JSON报告在同一证据根，保存输入文件hash与逐对两类改善率；脚本成功表示判定可复核，不表示性能目标达标。
本轮为离线统计/文档变更，只执行相关脚本与链接/diff门；复用未改变运行时代码的既有CMake/CTest结果。

## 交付C：最终集成、回归与冻结配置

两端RelWithDebInfo构建，配置与三个运行二进制SHA一致：
- server：c412a92774c6314ad7a457b1c27e0066bee8a9da7c3249938af0e38373b6d74c
- tree-download-client：f756380a7b2f10e0ecc5447a31c13743b598b2f573d9ef519eb18e8971f8f9ed
- tree-upload-client：0f25e73aa24d03bee4b3512d4abf5105e99831f2332b2085c6f80d99f84cfb14

相关CTest两端各126/126通过（R1 16.60秒，R2 16.92秒），覆盖FramedDataSocket、
SenderPacer、取消/超时/期限、budget、恢复与publication，以及大帧和driver。
另在候选开启时复用4个真实loopback回归：大帧upload/download、两项中断恢复、
publication恢复，4/4通过。Python警告作错误语法检查、git diff --check通过。
这证明受影响回归，不等于所有生产故障均通过，也不是加密/原生RDMA性能验证。

冻结行为：
- 默认sender/receiver queued、pwrite、pacer off，原默认full校验不变。
- 大文件显式实验参考仍为M1 combined，并保持pacer off；在新HEAD重新引用性能须重新正式验收。
- 1500000000仅为显式诊断选项，不向TLS/RDMA/压缩或大于4路外推。
- 启用必须在实际sender进程；下载的sender是远端server，client本地设置不自动传播。
- 关闭方法为unset CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND或值off，无协议迁移。

### 2026-11-12交付C销项与最终冻结

**C完成；阶段状态为ENGINEERING CLOSED / M2 OPEN。**
A工程验收为默认关闭诊断能力；B成绩/准入报告交付，正式M2未准入；C完成最终集成与边界收尾。
本次新增[配置冻结清单](2026-11-11-v3-sender-pacing-50-freeze.json)，是可审查的配置/证据清单，程序不会自动加载它。
清单固定默认值、显式M1参考和pacing诊断差异、运行时代码提交、三个二进制SHA、构建配置及src/include/tests/tools/CMake对象ID。

| C要求 | 完成依据 |
|---|---|
| 最终代码与配置 | 运行时代码057159c；此后提交只改README/docs，src/include/tests/tools/CMake对象相同。当前两端构建配置和三个运行二进制SHA与原测试/样本一致 |
| 相关回归 | 两端原日志各126/126、候选开启R1 4/4，已读取尾段并保存日志SHA；A销项7/7也已记录。复用同代码/同二进制证据，不将文档变更描述成新跑全套测试 |
| 成绩、资源与正确性 | 上表和B复核区分历史8GiB与当前1GiB；Task50完整性/1024 chunks、CPU采样下界、内存/映射、三个计数窗口及整轮swap/清理证据已汇总，无新正式成绩 |
| 现场清理复核 | 两端自有payload根缺席、49337未监听、/dev/shm各0B；原2121 PID/start身份与样本记录一致。本轮未启动互传或临时服务 |
| 原生RDMA评估 | 既有MR池/完成处理已存在，尚有两端复制；源码公式每connection 2,206,400,512B，4 connections共8,825,602,048B/端数据池，未含CQ/元数据等。只交付预算/所有权/正确性和对照重启条件，继续暂停 |
| 备份与交接 | 本次明确文件提交推送后读回GitHub SHA并同步R2；最终提交、两端构建/二进制及清单hash写入证据根c-delivery-2026-11-12.json，完成状态不替代M2或生产验收 |

最终C验收证据为同目录c-acceptance-2026-11-12.json，包含两端运行时对象、二进制、
原回归日志hash、现场清理和MR预算；最终交付提交以c-delivery-2026-11-12.json为准。
当前HEAD只具有同运行时代码的回归/二进制一致证据，未运行新的8GiB性能矩阵。
下一结构性选择仍是原生RDMA的MR预算与数据所有权审查；本次不启动它，所有DEFERRED项保持。

回归命令和历史实验调用：

前两条为同运行时代码的相关回归；第三条仅记录原实验实际调用，不是当前HEAD上的自动重跑入口。
原实验要求HEAD为057159c；后续文档提交不能直接传入旧commit冒充同版本。若未来获准复现实验，
须先按项目恢复规则保留当前改动、串行选定明确版本，两端同提交/配置/构建/SHA，再使用新的证据和marker自有payload路径；原路径不得覆盖。
```bash
ctest --test-dir build -R 'FramedDataSocketTest|SenderPacerTest|.*(Cancel|Timeout|Deadline|Publication|Resume|Budget|large_data|publication_resume|v3_interrupted).*|cpnetflux_(sender_pacing_driver_test|small_file_baseline_driver_test|receive_network_test)$' --output-on-failure
CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND=1500000000 ctest --test-dir build -R 'cpnetflux_(large_data_smoke|large_data_interrupted_resume_smoke|large_data_receive_buffer_resume_smoke|publication_resume_smoke)$' --output-on-failure
python3 tools/perf/run_sender_pacing.py --evidence /home/Su/experiments/CPNetFlux/2026-11-11-v3-sender-pacing-50 --payload-root /dev/shm/cpnetflux-s0-sender-pacing-50 --commit 057159ce02099e4a88c94e591f02880ca1669834
```

## 原生RDMA重启条件与价值评估（仅评估）

现有RdmaDataSocket已使用注册send/receive池、SEND/RECV、完成事件/轮询和接收slot生命周期；
不能把这些已有能力重复列为“尚未实现”。代码中仍有send span→MR池、MR→调用方的memcpy。
潜在价值是绕过TCP socket和netdev协议栈的部分工作；它仍经过HCA/接收队列，
不能保证自动解决所有缓冲耗尽，也不消除新tmpfs目标首次触页、CRC、manifest、租约和发布。

重启需要：
1. 用户明确批准恢复该路线；本轮不改或运行RDMA，新idea同样不自动重启。
2. 两端同提交构建，实际data_transport=rdma并有QP/字节/完成摘要一致；控制面仍TCP/TLS。
   socket经RDMA网卡不算verbs，现有256KiB DATA只支持plain TCP，RDMA不能直接套用M1大帧。
   比较须先用双方均支持的相同逻辑帧/校验合同，另列M1完整配置，避免混淆机制。
3. 先解决可行的MR预算。当前常量2048 receive+32 spare、16 send，每槽1MiB+4KiB，
   按源码公式每connection约2.055GiB数据池、4连接约8.219GiB/端（未含CQ/元数据/allocator开销）。
   对约32GB机器，叠加8GiB源/目标后可能逼近12GiB可用内存门，不能盲套四路正式矩阵。
   需冻结实际协商depth/占用、memlock/设备许可、posted低水位与spare耗尽及MR释放/取消证据，
   不是默认扩大队列或锁页。数值为源码预算推算，非本轮原生RDMA实测。
4. 完整V3 CRC、断连/取消、deadline、lease/attempt、buffer归还、checkpoint恢复、原子发布回归；
   两端资源、网络/HCA状态、临时空间及清理门全部保留。先机制正确性后完整任务同提交对照。

如果首要目标继续逼近100G，现有结构性选择中，**重审原生RDMA的MR占用与数据所有权路径**
比继续增加TCP并发或sleep参数更有依据：已有TCP发送成本证据和原生后端可复用，
但收益/内存准入尚未验证，不能承诺40/50/100G。Task17接收fault成本是另一现存证据，
但已失败的population/direct-receive等候选不原样重跑。
这些是后续选择的依据，本阶段不自动启动。

## 结束状态、未过门与最小条件

- 工程交付：A候选/测试/显式回退、B准入判定、C冻结/回归/限制/评估完成。
- M2：OPEN。本轮不是环境BLOCKED；环境门通过，候选速率未达准入，未进入正式验收。
- 稳定性：OPEN。历史接口OOB仍未归因，单次无事件不能关闭。
- 继续当前候选进入正式验收的最小条件：有新的机制证据和明确改动使有门可行性达到≥40Gbps，
  随后同提交M1 combined对照8GiB五对；不是放宽计数/资源门或增加同类试验。
- 本阶段停止实验和源码写入，保留可复用提交和证据。后续结构性开发需用户选择/授权，
  不自动延伸一串新诊断。磁盘持久化/TLS/反向/长时/更广负载为独立验收。
- 两份2026-10-14未跟踪文档保持；无凭据、payload、构建产物或CPSS内容入库。

证据根：/home/Su/experiments/CPNetFlux/2026-11-11-v3-sender-pacing-50。
含matching-builds、preflight/postflight、source驻留、results/receipt/summary、
pacing-receipts/机制、idle/三个网络窗口、server/资源、cleanup、final-audit、红绿与CTest日志；
R2源/资源/现场证据复制于peer-evidence-r2，原始服务日志位于R2同根peer。
实现提交057159c已推送并读回GitHub相同SHA；结果文档与导航后续提交另记录delivery.json，
不将未推送内容称为备份。
