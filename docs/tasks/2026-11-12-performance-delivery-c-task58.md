# 交付C：Task58后的最终集成与边界复核

日期：2026-11-12；输入abc049a1c92560c63f75466b029809edeb670a9f。
权威目录R770-1 /home/Su/projects/CPNetFlux，单人串行；R770-2仅验收固定提交。

## 修改前范围与验收合同

范围：对Task58最终版本完成交付C逐项核对；冻结默认/实验配置、源码对象、四个二进制、
相关回归与历史/当前成绩边界，复核双端现场和GitHub备份，纠正文档导航仍指向Task56的
“最新执行/当前开发”等入口。保留Task50、Task51/52及Task55-58原始冻结与失败记录。
非目标：不修改运行时/协议/默认值，不运行新payload/矩阵、不调整系统或清理他人内存，
不启动RDMA/DEFERRED工作，不将工程交付等同M2达标或性能优化目标完成。
受影响文件：本文件、新增同名前缀-freeze.json、README.md、docs/README.md、docs/ROADMAP.md。

验收：复核Task58冻结的65份原始证据SHA与实际失败/清理，核对相关262/262和候选恢复2/2
的原输入版本，以及最终修正8/8的覆盖，源码未变时不因文档修改重跑无关CTest。
新采集双端同提交/配置/四SHA、route、接口配置、qdisc、host mount/THP、内存、生产2121
身份、自有payload与49337状态；快照只说明当前现场，不覆盖历史+1页失败。
验证JSON/相对链接/文档状态、git diff --check；只提交明确文件，推送已核实remote并SHA回读，
R2同步该文档提交后再核对现场和代码对象。最后复制小报告/清单/回执到Windows并核对SHA。
验收证据：/home/Su/experiments/CPNetFlux/2026-11-12-performance-delivery-c-task58。
最终提交与备份回执写实验根delivery.json，避免受版本管理文件自引用最终SHA。

## 结果

工程收口判定：**DELIVERABLE C COMPLETE / M2 OPEN（正式资源门未通过）**。
这是Task58之后的最新C；[Task51/52的旧C](2026-11-12-performance-delivery-c.md)和Task50
历史C仍按原提交解释。代码/配置/证据冻结见[当前清单](2026-11-12-performance-delivery-c-task58-freeze.json)。
最终提交/推送/SHA回读/R2同步和Windows副本验收见实验根delivery.json。

## 代码、配置与退出方式

最终实现998cba8883dc3594d976beb0e3d97881d28d8f37，输入文档提交abc049a1c92560c63f75466b029809edeb670a9f；
正式失败输入为f2b12f02293df7766b2c3acad9f52b9814390d8a。当前C只改五份说明/清单，
src/include/tests/tools/CMake对象不变，精确对象ID、构建配置和四个二进制SHA均固定在清单。
两端RelWithDebInfo，ISA-L/io_uring/TLS/RDMA编译支持开启；本轮没有原生verbs测量。

| 类别 | 冻结配置与用途 |
|---|---|
| 日常默认 | queued发送/接收、pwrite、full校验；私有后端、应用pacer、exact-read、kernel pacing均默认关闭；本轮不晋级默认 |
| 历史M1参考 | 4路TCP、256KiB有效DATA+CRC、1MiB chunk、CRC32C/verified_chunks、checkpoint16、queued发送/inline实验mmap接收、每路64MiB映射、接收buffer请求1MiB；仅原506b63b有8GiB正式验收 |
| Task58 control | 同M1协议/预算，native私有tmpfs huge=never，kernel pacing off；8GiB/挂载8448MiB |
| Task58 candidate | 同control，huge=within_size，内核ceiling每lane 2e9B/s；组合候选，未完成任务，不作为已证明收益推荐 |
| 两臂边界 | 显式192.168.100.2→.1 bind/eno17095np0；TLS/压缩off、commit_sync=none；payload64MiB、总映射256MiB；应用pacer/exact-read off |

完整时间仍为必要namespace/挂载+目标目录+client启动至成功退出；外部SHA/观察/清理单列。
正式实际分配前MemAvailable≥21.25GiB、MemFree≥9.25GiB，负载期MemAvailable≥12GiB、
owned swap0、host pswpout增量0。当前前置保护只能拒绝风险，不是压力已解决或零swap保证。

退出：不传--enable-private-tmpfs/--enable-formal-m2/--native-setup等实验入口；unset
CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND、CPNETFLUX_TCP_EXACT_READ和
CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND。无需撤销CRC、租约、恢复或结构化日志修复。
旧命令保存于[Task58清单](2026-11-12-v3-private-m2-58-freeze.json)，它标注历史输入，
不应把旧--commit传给不同HEAD，也不据此重跑已停止的矩阵。

## 回归与性能证据分开验收

| 版本 | 已执行并复核的相关回归 | 使用边界 |
|---|---|---|
| f2b12f0 | 两端各262/262，候选开启恢复各2/2 | 大帧/预算、超时/取消/错误、V3状态、恢复与发布等相关集合；不是全仓库全部测试 |
| 998cba8 | 两端构建与最终专项各8/8；真实native原8生命周期、2种容量/页策略、11个危险参数拒绝、物理free拒绝 | 修改在perf驱动/native helper；传输核心与三传输二进制不变；未在最终修正上重跑262项 |
| 当前C | 65份Task58原证据hash与离线语义验证；当前双端源码/配置/四SHA/现场核对、JSON/链接及diff检查 | 仅文档变动，复用上述同对象回归；新增互传0，不新增性能样本 |

| 测量提交/规模 | 完整任务成绩 | CPU/内存/完整性及门 |
|---|---|---|
| 历史M1 506b63b / 五对8GiB | combined中位34.159410Gbps、2.011729秒；5/5改善，时间CV3.238% | 原SHA/8192 chunks、资源/网络门通过；仍是正式参考，不是当前HEAD成绩 |
| 历史Task57 118211f / 单次1GiB | 40.865345Gbps、0.210200957秒；包含7.740488ms namespace/setup与0.670771ms目录准备 | 接收/发送CPU采样下界0.56/0.55秒/GiB，RSS峰225169408/14716928B；SHA/1024 chunks、实际页型、网络/资源/清理门通过；无A/B/CV/稳定性结论 |
| Task58 f2b12f0 / 计划8GiB | 源write到8002732032B时host pswpout +1页停止；完整有效任务0，网络传输0 | 源owned swap0、leaf回收0，user-1002祖先swap +4096B；具体对象未识别；资源门FAIL，完整内容/吞吐/CPU每GiB/网络/页型/配对/CV均未测 |
| 当前C | 新性能任务0 | 只完成工程核验与备份；不把NOT_RUN计为PASS |

RSS不包括所有tmpfs数据页，CPU是采样下界，不能与墙钟等同；不同提交/规模不能计算候选收益。
一次零OOB也不关闭历史因果；聚合共享设备计数不提供逐流/RQ归因。
原失败、部分源清理及卸载回执完整保留。当前现场两端/dev/shm使用0、自有cpnetflux-s0路径为空、
49337无监听，原2121的PID/start_ticks不变；新现场内存数值记录在before/after.json，不回写历史证据。

## 原生RDMA重启价值与条件（仅评估）

当前源码已有注册池、SEND/RECV、CQ完成和slot归还；发送span→MR和MR→接收调用方仍有memcpy。
include/cpnetflux/core/io/rdma_data_socket.h固定16 send、2048 posted receive、32 spare，
每槽1MiB+4KiB；(16+2048+32)×1052672=2206400512B/连接，四连接8825602048B/端，
约8.219GiB，未计CQ/元数据。该值为源码预算推算，不是实测锁页总量或已经准入。

原生verbs可能绕开部分TCP socket/netdev路径；不会自动去除目标页分配、CRC、manifest和发布，
也不能保证解决共享设备OOB或达到100G。目前两台约30GiB机器先需处理MR与源/目标共同预算。
重启条件为明确授权路线、可核对的实际MR/memlock与协商深度、buffer所有权及CQ/slot释放合同，
取消/deadline/错误/恢复/发布回归、双端同提交/配置/SHA，以及实际data_transport=rdma、QP/字节/摘要证据。
和TCP对照时帧/校验/计时/负载须双方支持；不把RDMA编译开启或TCP走RDMA网卡说成原生verbs。
本次仅评估，RDMA运行时优化继续暂停，微批/策略记忆继续DEFERRED。

## 下一步与重启门

1. 先定位项目拥有的内存和分配/回收工作，提出可减少实际回收的有界资源/后端设计；
   不drop_caches、swapoff、删除他人数据或以owned swap替代host门，不原样重跑Task58。
2. 新设计先固定范围、预算、失败清理、计时合同并测试；代码先提交备份，同步双端构建与SHA。
   资源条件变化须有可核验证据，不能仅因暂时安静或一次入场检查通过就宣告问题解决。
3. 条件满足后另立正式8GiB合同；保持五对AB/BA、候选中位≥40Gbps、两臂时间CV≤5%、
   ≥4/5改善、正确性/资源/网络/恢复所有门。70–85/100Gbps、磁盘/TLS/反向/长时和生产验收仍开放。

离线复核（不启动payload，不改原证据）：

    python3 /home/Su/experiments/CPNetFlux/2026-11-12-performance-delivery-c-task58/verify.py

历史输入、测试正则、实际停止命令及资源证据由Task58原冻结可追溯；本轮清单补充当前C证据。
回退需先保留当前修改，再选择GitHub明确备份恢复指定文件；不删除实验证据或其他项目。

