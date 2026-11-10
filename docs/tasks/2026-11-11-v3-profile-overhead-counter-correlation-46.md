# V3 profile扰动与接收计数窗口诊断46

日期：2026-11-11
输入提交：`bd05af69a53f45f66f19ddead3bea2a5d560ddb7`
前序结果：[诊断45](2026-11-11-v3-receiver-profile-diagnostic-45-result.md)
状态：合同与诊断工具已实现；R770-1 两个相关 Python 测试文件 26/26 通过。待固定提交、双端同提交构建及二进制核验后，才运行有界矩阵。

## 背景与工作假设

诊断45的第6个样本记录R1 `eno17095np0` 的 `rx_missed_errors` +405。当前驱动的 `before` 在启动下载客户端前采集，`after` 在子进程结束、接收树SHA与manifest检查之后采集，因此该增量不能确定是在传输中还是校验尾段发生。R2 server在整个矩阵生命周期内记录的 `rx_missed_errors` 增量为0。R1其他通用接收错误计数没有增加。此前诊断36将该netdev计数映射到mlx5接收队列缺buffer层，但诊断45没有同步采集 `rx_out_of_buffer`，所以本次事件的具体队列/触发机制仍开放。

诊断45的CPU profile也没有unprofiled对照。按其CPU profile计数和当前 `OptionalThreadCpuTimer` 实现，65,532 B样本执行约139,272个CPU计时wrapper（约278,544次 `clock_gettime(CLOCK_THREAD_CPUTIME_ID)`边界调用）；262,144 B样本约32,776个wrapper（约65,552次调用）。wall timer和range observer另有成本。此计数说明有理由测量profile扰动，不代表这些调用实际占用的任务时间。

## 假设

1. 对固定的1 GiB V3 TCP任务，接收profile启用与关闭的配对差值可以估计该profile对任务墙钟的扰动；只对本配置有效。
2. 如果R1 netdev `rx_missed_errors`在下载子进程窗口内增加，并且同窗口 mlx5 `rx_out_of_buffer`也增加，则该事件与硬件接收队列缺buffer层相符；仍不能据此归因到特定流或队列。
3. 如果变化只出现在子进程结束后的既有本地校验窗口，则它不应作为该传输的网络门失败或候选因果证据。

## 范围

- 新增1 GiB `profile-overhead`矩阵：固定256 KiB DATA、TCP receive-buffer关闭、4 lanes、8 ranges、1 MiB chunk、CRC32C/verified_chunks、实验mmap接收、tmpfs源/目标；5对AB/BA交错，共10个完整任务。
- 两臂均使用 `--phase-timing on` 和 `CPNETFLUX_RANGE_TIMING=on`；off臂关闭payload与CPU profile，on臂开启两者。除这两个profile开关外，客户端、数据路径、资源观察和命令参数一致。
- 每个客户端子进程启动前及退出后，立即使用已有只读网络观察器保存netdev、mlx5 ethtool、softnet、IRQ和TCP快照，并保存采集时间边界；另保留当前内容校验后的快照，用于识别传输后的计数变化。
- 记录配对任务耗时比、两臂CV、profile开启后的逐range观测及`rx_missed_errors`/`rx_out_of_buffer`变化。任何profile快照都不作为性能成绩。

## 非目标

- 不修改C++运行时、生产默认值、RDMA、磁盘、NIC、路由、IRQ、ring、coalesce、RSS、RPS或swap配置。
- 不重跑诊断45的帧尺寸矩阵，不启动receive-buffer矩阵，不运行8 GiB或更大任务。
- 不因计数未复现而宣布网络问题解决，不将总接口/主机计数解释成流级或队列级因果。
- 任一传输窗口 `rx_missed_errors` 或 `rx_out_of_buffer` 增量非零、完整性/资源/清理门失败时，保留本样本和侧车，立即停止，不重试。

## 验收

实现和脚本测试通过；R770-1/R770-2同提交、同CMake配置、构建成功且三份运行时二进制SHA-256一致后才运行。每个样本须满足源驻留、MemAvailable至少12 GiB、owned swap与host `pswpout`增量为0、外部SHA及1024个verified chunks通过，并精确清理本地目标与R2源/根目录。网络诊断快照须包含 `rx_missed_errors` 与 mlx5 `rx_out_of_buffer`，且窗口边界可区分传输进程与后续本地校验。

完成五对配对后报告profile on/off的 `t_ready` 配对变化、两臂CV和观察器采集时长。任一臂CV大于5%时，profile扰动结论标记为不稳定；若中位配对变慢至少5%且至少4/5配对方向一致，标记为该固定路径下观察到可重复的profile扰动。无论结果如何，不晋级传输配置，也不外推到磁盘、原生RDMA或100 Gbps。

## 输入与证据路径

代码和计划证据根：`/home/Su/experiments/CPNetFlux/2026-11-11-v3-profile-overhead-counter-correlation-46`。
唯一payload根：`/dev/shm/cpnetflux-s0-profile-overhead-46`，文件不超过1 GiB。先固定提交、双端构建和二进制哈希，再执行；若任一门失败，仅保存诊断材料并清理，不启动矩阵。

预期修改文件：

- `tools/perf/run_small_file_baseline.py`
- `tools/perf/run_large_data_ab.py`
- `tools/test/run_small_file_baseline_test.py`
- `tools/test/run_large_data_ab_test.py`
- `docs/tasks/2026-11-11-v3-profile-overhead-counter-correlation-46.md`
- `docs/tasks/2026-11-11-v3-receiver-profile-diagnostic-45-result.md`
- `docs/ROADMAP.md`
- `docs/README.md`
- `README.md`
