# 交付C：当前版本冻结、验收与边界
日期：2026-11-12。输入：7d7992fe78fce0acf075f9e9797ae4ce30eb269d。
权威开发：R770-1 /home/Su/projects/CPNetFlux；单人串行，R770-2仅同步固定提交。

## 任务范围与验收合同（修改前固定）
范围：在既有Task50交付C历史记录之外，为Task51/52后的当前版本补齐配置、代码、
二进制、回归、失败/未测项、清理与备份清单。保留所有原始结果及两份既有未跟踪文档。
非目标：不改运行时/协议/默认值，不运行跨机payload或新性能矩阵，不调整系统，
不启动原生RDMA或DEFERRED研究，不把工程关闭解释为40Gbps达标或总目标完成。
受影响文件仅：
- docs/tasks/2026-11-12-performance-delivery-c.md（本合同与结果）；
- docs/tasks/2026-11-12-performance-delivery-c-freeze.json；
- docs/README.md（当前冻结入口）；
- docs/ROADMAP.md（当前冻结入口，原开发方向不变）。

验收：读取并核对既有CTest日志及hash、受测/当前代码对象和两端配置/二进制；
复核自有payload清理、49337临时端口与既有2121身份。复用同代码/二进制的254项相关回归，
不为文档修改重跑无关全套测试。运行JSON/链接/证据hash/版本/配置门禁及git diff --check；
明确文件提交推送、GitHub SHA回读、R2 fast-forward后复核。只有这些通过才宣告交付C完成。
证据：/home/Su/experiments/CPNetFlux/2026-11-12-performance-delivery-c；
最终提交回执放delivery.json，避免在受版本管理清单内自引用其提交SHA。


## 交付结果
交付C完成：当前Task51/52后的工程代码、配置、回归证据和边界已冻结。
阶段判定为 **DELIVERABLE C COMPLETE / M2 OPEN**；总性能优化目标未完成。
Task50原C冻结记录保留原版本，不覆盖、不冒称适用于新二进制。

[当前冻结清单](2026-11-12-performance-delivery-c-freeze.json)固定：
- 输入提交7d7992fe78fce0acf075f9e9797ae4ce30eb269d；
  受测运行时代码ac2544558d71cf7fc44e7e07d57e5499871d4272。
- 两端RelWithDebInfo、ISA-L/TLS/RDMA/io_uring构建开关及三个二进制SHA，
  src/include/tests/tools/CMake对象ID。构建启用RDMA不代表实验使用verbs。
- 默认queued发送/接收、pwrite、full校验，pacing off、exact-read off。
- M1显式参考为四路TCP、256KiB有效DATA+4B CRC、1MiB chunk、CRC32C/verified_chunks、
  checkpoint16、queued sender/inline实验mmap receiver、每路64MiB映射窗口、
  接收buffer请求1MiB/实读2MiB、TLS/压缩off、commit_sync=none。
  该参考保持两个诊断候选off；当前HEAD尚无新正式性能验收。
- pacing仅实际sender设置，timerfd每lane懒创建，最多四个fd；
  exact-read仅实际receiver设置，plain TCP/256KiB/最多四channel。
  两者均无默认晋级，不推荐为已证明收益的性能配置。

退出方法：unset CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND和CPNETFLUX_TCP_EXACT_READ，
或显式设为off；没有协议迁移，不改变CRC、attempt/lease、恢复和发布。
关闭exact-read不需要撤回已修复的Python receipt检查。

## 验收与测量版本
两端现有构建、配置和三个二进制SHA一致；本次只修改文档与冻结清单。
读取并核对Task52原回归日志及其SHA：两端各254/254相关CTest、
R1开启exact-read的恢复测试2/2、修正统计索引后的driver门禁两端各3/3。
相关集合覆盖大帧/预算、短读/EOF/超时/shutdown、取消/deadline、CRC、
V3持久化流程、中断恢复及publication；不是全仓库全部测试，也不是所有生产故障证明。
src/include/tests/CMake与受测ac254455相同；tools仅run_sender_pacing.py的索引修复与run_tcp_exact_read_test.py相应回归，
对应独立3/3门禁。文档更新未重跑254项或新增互传，复用理由保存在机器验收中。

| 证据 | 同提交负载和性能 | CPU/内存/完整性/门 |
|---|---|---|
| 历史M1，506b63b | 五对8GiB；combined中位34.159410Gbps、2.011729s；default28.125991Gbps，5/5改善，时间CV3.238% | 接收CPU采样中位0.84625秒/GiB；原SHA/chunks/资源/网络门通过，非当前HEAD新成绩 |
| Task51，52acbeb9 | 唯一1GiB，36.478374Gbps、0.235480194s；无A/B/CV | CPU采样下界接收0.84/发送0.77秒/GiB；RSS峰267132928/14761984B；SHA/1024 chunks及资源/观察窗网络门通过；不足40，不证明长期稳定 |
| Task52，ac254455 | 唯一1GiB原始耗时0.238535375s、原始36.011156Gbps；有效性能样本0 | CPU采样下界0.84/0.77秒/GiB；RSS峰216604672/14745600B；目标SHA/1024 chunks通过；OOB/missed各+52，网络门FAIL |
| 本次C交付 | 新性能样本0；正式M2矩阵0 | 版本/原始hash/回归日志/JSON/链接/现场清理复核；不产生新的吞吐结论 |

Task52各4096大负载一次recv完成，只证明调用粒度机制。
原driver因错误使用lane_index在传输后exit1；修正后仅离线复核，原failure/results不改。
传输后source-check为NOT_RUN，不补写通过；初始源驻留与目标SHA仍为独立已存证据。
OOB来自共享设备聚合计数、missed异步刷新，不能归因为本流、某RQ或exact-read。
RSS不包含全部tmpfs文件占用；CPU是采样下界，不能相加当墙钟或计算跨提交优化收益。

Task51/52源与新目标资源合同均为MemAvailable≥12GiB、owned swap=0、
host pswpout增量=0，源准备全页驻留；实际数值与原日志分别见
[Task51结果](2026-11-12-v3-pacing-wait-51-result.md)和[Task52结果](2026-11-12-v3-tcp-exact-read-52-result.md)。
原始失败和两份既有未跟踪任务文件保留；实验payload、凭据和构建产物不入Git。
本次两端现场/dev/shm均0B、无项目payload目录，49337未监听；
2121仍为R1 PID126766/start1191040和R2 PID164834/start1679976。

## 原生RDMA价值与重启条件（仅评估）
当前后端已有注册send/receive池、SEND/RECV、完成处理和slot归还；
send span→MR、MR→调用方仍有memcpy，不能把已有能力当作待开发项。
源码常量为16 send、2048 posted receive、32 spare，每槽1MiB+4KiB：
每连接(16+2048+32)×1052672=2206400512B，
四连接8825602048B/端（约8.219GiB），不含CQ、元数据和分配开销。
这是源码预算推算，非原生RDMA实测，也不是所有情况下的实际MR占用。

原生verbs可能绕开一部分TCP/netdev工作，但仍要处理HCA队列、目标页分配、
CRC、manifest和发布，不能承诺自动消除OOB或达到40/100G。
继续暂停。重启前需明确路线范围、预算/MR/memlock、实际协商深度及完成/字节/QP摘要；
验证取消/deadline、lease/attempt、buffer归还、恢复和原子发布。
先用两种后端均支持的相同帧/校验合同对照；256KiB TCP参考不能直接冒充RDMA成绩。
两端同提交/构建/SHA、资源/网络/清理门仍必须保留。

## 可复核命令与交接
~~~bash
python3 /home/Su/experiments/CPNetFlux/2026-11-12-performance-delivery-c/verify.py
git diff --check
~~~
verify.py核对原始证据hash、已执行CTest日志、代码对象、配置/二进制、
预算公式及导航链接；acceptance.json的PASS只代表文档/证据门，不代表性能门。

受影响运行时代码的原验收命令见Task52结果；完整正则冻结在JSON的reproduction内。
历史driver为tools/perf/run_sender_pacing.py和tools/perf/run_tcp_exact_read.py，
须读取各自固定提交和one-gib receipts复现；禁止把历史SHA参数传给当前不同HEAD，
也不在本次C重复运行停止的候选。未来如批准新机制，使用新证据/marker目录，
同提交同配置构建/SHA后才启动互传，保留全部失败记录。

本次最终文档提交、GitHub远端SHA回读、R2同步与两端终检记录在
/home/Su/experiments/CPNetFlux/2026-11-12-performance-delivery-c/delivery.json；
清单不自引用最终提交，最终SHA以该外部回执为准。
Windows仅保存本次报告/清单/验收/回执的查看副本，不覆盖本地项目已有修改。

下一开发方向继续当前TCP接收/CRC/新目标页管理之间的数据所有权与搬运审查：
先找到区别于失败population/direct-receive/collapse/queued-mmap的实际减工作机制，
再冻结新实现及验收合同。不增加通用profiler轮次、不扫描rate/lane/frame/队列，
不放宽CRC、网络或内存门。原生RDMA、微批和策略记忆等DEFERRED项保持暂停。
40–50、70–85/100Gbps、OOB归因与长期稳定性、TLS/磁盘持久化/反向和生产验收仍开放；
交付C完成不改变这些未完成状态。
