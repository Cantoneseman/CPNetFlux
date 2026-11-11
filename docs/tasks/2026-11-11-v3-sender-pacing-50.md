# V3 发送节奏候选50与本阶段收尾

输入：ea69f952d0f51518d55487a123c47ecc64332bbb；日期2026-11-11。
状态：ENGINEERING CLOSED / M2 OPEN；唯一写入者为既有“开发”。统一工作单元idea仍DEFERRED。

范围：一个显式任务局部应用DATA提交pacer，CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND=off（默认）或1500000000。
每路12Gbps应用帧上限，最多4路合计48Gbps；数据帧最大262212B，因此每次提交的有界突发不增大。
使用单调时钟，实际提交时间重建下一deadline，暂停后不攒信用、不补发追赶；不增加线程/缓冲/内核socket容量。
每DATA提交前等待，控制/结果帧不节流；发送线程最多1ms间隔检查既有失败/总deadline。
取消检查在获得WorkExecutor IO许可前；保持短写、错误、CRC、租约、checkpoint、恢复和发布语义。
支持V3 persistent plain TCP sender的upload/download及1..4 lanes；TLS/RDMA/压缩显式拒绝，
其它用户路径不自动启用。每lane独立上限，不承诺跨lane同步、公平带宽或线速包间隔。
非目标：不改协议/默认、qdisc/NIC/IRQ/RSS/MTU/THP/swap，不开发新idea/原生RDMA。

依据：Task49单路pwrite仍伴随OOB；突发为待检验假设，不是已知根因。
现场只读qdisc为mq+fq_codel；不将SO_MAX_PACING_RATE读回等价于实际生效，选应用准入机制。
限制内核提交无法消除TSO/内核排队导致的线速突发，这也是候选解释边界。

预计文件：include/cpnetflux/core/io/detail/sender_pacing.h、
src/core/io/persistent_tree_transfer.cpp、tests/unit/framed_data_socket_test.cpp、
tools/perf/run_small_file_baseline.py、tools/perf/run_sender_pacing.py、
tools/test/run_sender_pacing_test.py、CMakeLists.txt、本计划/结果/ROADMAP/README。
收尾导航范围另含根README.md（清除已被Task48/49/50覆盖的下一步），只更新当前结论与结果入口。
仅如检查需要调整直接相关测试；不混入历史两份未跟踪文档。

验收：
1. 先失败行为回归：虚拟时钟的提交率、暂停不追赶、取消不提交、默认无等待；
真实V3发送/完整性及环境非法值、TLS/RDMA支持边界。运行实际代码，不只检查字符串。
2. pacer开启独立于profiler输出每lane charged wire bytes/frames、wait次数/实际等待、
首末提交时间与间隔违例；保证每次提交间隔符合前一帧字节/rate，失败样本亦保留。
计数证明应用准入改变，不能证明线上包节奏。
3. 相关CMake/CTest（大帧/预算/取消/错误/恢复/发布）通过；R1先固定提交推送，再同步R2，
两端配置和运行二进制SHA相等。保留原2121服务与路由/显式bind证据。
4. 一次1GiB四路、M1 combined同条件加pacer可行性任务；详细profile off。
沿用Task49窄窗口+0.5s刷新/宽missed门，首个OOB/missed/resource异常停止，不换矩阵补跑。
MemAvailable≥12GiB、源全页驻留、owned swap/host pswpout增量0、每端payload≤8GiB、SHA/manifest/清理。
5. 单次候选须网络/资源/机制通过且task_rate≥40Gbps才具备M2速率可行性；无同提交对照不称收益。
仅机制/环境/速率可行性准入均通过才固定合同做同提交8GiB至少五对交错正式M2；
对照M1 combined，候选仅pacer。task_rate中位≥40Gbps、CV≤5%、4/5配对改善及全部门，
否则如实M2 OPEN或BLOCKED；不以降速消除事件冒充收益。
6. 无论准入结果，冻结显式配置/版本、最终回归、限制与原生RDMA重启评估，精确提交推送/SHA回读。
不再延伸通用诊断。候选默认一轮原型+最多一轮有原因修正，异常门不绕过。

证据：/home/Su/experiments/CPNetFlux/2026-11-11-v3-sender-pacing-50。

## 2026-11-12交付A销项

输入2f338eba0ef1a2868c966fec048892c3a8fe44fa；本次只补交接文档的逐项验收与A状态。
范围文件：本计划、2026-11-11-performance-stage-closeout-handoff.md。
非目标：不改代码/默认值、不新增候选、不重复互传、不启动B正式矩阵或RDMA。
验收：直接相关pacing/driver测试、既有唯一样本证据复核、相对链接与diff检查、精确提交推送和远端SHA回读。
证据写入原Task50目录a-acceptance-2026-11-12.json；工程完成不改变M2 OPEN。

## 2026-11-12交付B判定

输入a75dac30d68d3e3ba404afdaddc89e293ea4445a。范围：对历史M1十个完整任务与A唯一样本离线复算，正式记录B准入/NO-GO和M2状态。
受影响文件：本计划、Task50结果、performance-stage-closeout-handoff、docs/ROADMAP.md、docs/README.md。
非目标：不修改运行时/配置/默认值，不放宽既定40Gbps准入，不新增payload或正式矩阵，不改历史原始证据，不启动RDMA/新idea。
验收：实际成功字节/T_ready重算、完整性/资源/提交及证据hash核对、配对时间与吞吐口径/CV定义、未运行门不计pass、链接/diff与精确提交推送回读。
离线验收脚本/报告保存在原Task50证据根b-acceptance-2026-11-12.py/.json；其通过表示判定可复核，不表示M2通过。

结果：历史十个M1完整任务与A唯一样本复算通过；B判定NO-GO / NOT ADMITTED，正式M2未运行，M2 OPEN。详见Task50结果的交付B离线复核；脚本退出0仅代表判定证据通过。
