# 接收网络36：Milestone35接口事件定向归因

输入965357b8829ce23020da5272dc76d10a1a21dd02，日期2026-11-10。
R1唯一写入者；R2只同步固定提交。保留October14未跟踪文档。

范围：把现存rx_missed_errors追到mlx5硬件接收缓冲层，定向区分
H1接收queue补给/CPU服务不足，与H2TCP接收/应用消费背压。
先实现可复用的有界网络诊断并验证，再同提交最多3个1GiB真实任务采集。
保持256KiB DATA、1MiB接收buffer、4 lanes、inline mmap、checkpoint16和原资源门。
0.5s空闲/前后边界与任务中采样，接口统计异步更新的延迟窗口单独保存，
不把边界计数增量武断归到某流/某个样本。诊断耗时不冒充新的无观测性能成绩。

已核实：R1内核7.0.0-38、mlx5_core、firmware16.35.8002；R2内核7.0.0-34、firmware16.30.1004。
32 combined queues、RX ring1024、adaptive coalescing on、RSS均匀32队列、RPS off。
rx_missed_errors与ethtool rx_out_of_buffer当前均544380；物理CRC/丢弃、buff_alloc_err当前0。
Linux v7.0 en_main.c把qcnt.rx_out_of_buffer累加至rx_missed_errors，官方counter定义为RQ缺少软件buffer。
参考：https://raw.githubusercontent.com/torvalds/linux/v7.0/drivers/net/ethernet/mellanox/mlx5/core/en_main.c
https://docs.kernel.org/networking/device_drivers/ethernet/mellanox/mlx5/counters.html
这是层级归因，尚非CPU饥饿/分配失败/驱动固件根因。

非目标：不改sysctl、网卡ring/IRQ/RSS/offload、THP/swap/cgroup或现有服务；
不重跑8GiB、不改默认、不恢复RDMA、不扫描lane/buffer参数。
只有本轮真实证据支持具体软件修正时才实施；未复现/证据不够则保留OPEN并给出下一具体行动，
不自动串起通用profiler或多轮盲测。

文件：tools/perf/receive_network.py（新解析/采集/分析），
tools/perf/run_receive_network.py（复用large_data_ab与small_file_baseline，最大3次），
tools/test/run_receive_network_test.py；tools/perf/run_small_file_baseline.py仅添加可选进程观察接口；
CMakeLists.txt注册脚本测试；本记录/结果及README/ROADMAP状态更新。
必要时只在上述范围扩展实际完成观测，不更改数据协议或校验。

验收：解析器带真实格式夹具和计数回退/缺失/CPU映射边界测试，明确先红后绿；
baseline时钟/清理既有脚本、相关CTest、Python编译；固定提交push/readback后R2同配置
构建且三binary SHA一致，再1GiB完整任务。收集ethtool队列/物理/内存分配，
IRQ/softirq/softnet/CPU以及实际PID线程/ss四元组和TCP重传；源驻留/内容SHA/1024认证chunk。
采集线程和外部工具开销另报，最多3任务全部保留，不以复现零错误认定修复。
证据根/home/Su/experiments/CPNetFlux/2026-11-10-v3-receive-network-36。
执行后精确清理自有payload，保留原始证据，记录提交/远端回读/下一交接。
