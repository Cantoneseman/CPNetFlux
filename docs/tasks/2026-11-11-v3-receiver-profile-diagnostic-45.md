# V3接收端profile证据保留与定向诊断 45

日期：2026-11-11
输入提交：8831f8973afa32fb60f1b44b764d56102dea89de
分支：codex/V3-GRIDFTP-OPTIMIZED-RERUN-01
状态：**ABORTED / NETWORK COUNTER GATE；无性能结论。** 驱动与测试已在代码提交 `82784e540c11b8ac444be0414467e1c488b22342` 完成；帧尺寸矩阵第6个样本触发 `rx_missed_errors` 增量门后停止，receive-buffer矩阵未启动。详见[结果](2026-11-11-v3-receiver-profile-diagnostic-45-result.md)。

## 背景和范围

M1-44在8 GiB任务中，combined配置相对default减少了DATA帧数并降低接收端进程CPU采样值，但combined同时改变了DATA帧尺寸和TCP接收buffer，无法拆分单项影响。当前C++已有CPNETFLUX_RECEIVER_PROFILE和CPNETFLUX_RECEIVER_CPU_PROFILE观测；共享Python驱动会清除所有外部CPNETFLUX_*变量，且压缩摘要时没有汇总这些profile字段。

本任务增加显式、默认关闭的--receiver-profile矩阵选项。开启时仅传输客户端启用CPNETFLUX_RANGE_TIMING=on、CPNETFLUX_RECEIVER_PROFILE=1和CPNETFLUX_RECEIVER_CPU_PROFILE=1；驱动将各range的payload与CPU观测汇总到receipt，保留schema/scope，计数与时间按range加总，峰值容量取最大。现有控制配置和外部环境清理继续生效。关闭时运行环境和receipt格式保持原行为。

## 非目标

- 不改C++运行时或生产默认值；不改RDMA、磁盘、NIC、路由、swap或系统配置。
- 不运行大于1 GiB的文件，不重跑M1-41/M1-44；不把profile运行的吞吐作为性能成绩。
- 不用此任务推断两项机制可相加；不扫描lane、队列、帧尺寸或buffer尺寸。
- 不因诊断失败重跑同一矩阵。保留receipt、资源失败侧车和部分结果，完成精确payload清理后停止。

## 代码与测试范围

- tools/perf/run_small_file_baseline.py：profile开关显式传递；汇总DATA分配/复用、logical copy、prefix shift、CRC、whole-file write，以及接收线程的socket/CRC/state/lease/write/range/physical-write/push/pop CPU和调用计数。
- tools/test/run_small_file_baseline_test.py：验证profile聚合、峰值规则、缺失观测处理和外部环境隔离。
- tools/perf/run_large_data_ab.py：仅1 GiB frame与receive-buffer矩阵接受--receiver-profile；核验每个range均产生profile，并将分析结果标记为诊断、吞吐不可作为性能成绩。
- tools/test/run_large_data_ab_test.py：覆盖profile准入、计划配置及完整profile观测验收。
- 更新本合同、docs/ROADMAP.md和docs/README.md；最终结果另写配对结果文件。

## 诊断合同

最终代码提交在R770-1固定并推送，再同步到R770-2。两端运行CMake构建、相关Python测试与ctest --test-dir build -R cpnetflux_large_data --output-on-failure；比较提交、CMake配置和三份运行时二进制SHA-256。

依次运行两个有界矩阵，各五对AB/BA、每臂五个样本，总计20个完整1 GiB任务：

1. frame：65532 DATA/off TCP receive buffer 对 262144 DATA/off TCP receive buffer。
2. receive-buffer：262144 DATA/off TCP receive buffer 对 262144 DATA/1048576 TCP receive buffer。

证据根为/home/Su/experiments/CPNetFlux/2026-11-11-v3-receiver-profile-diagnostic-45，子目录分别为frame和receive-buffer。每个矩阵独立使用精确标记payload根/dev/shm/cpnetflux-s0-receiver-profile-45-frame和/dev/shm/cpnetflux-s0-receiver-profile-45-buffer，顺序执行且前一矩阵彻底清理后才进入下一矩阵。

每个样本保留外部SHA、1024个verified chunks、8个平衡range、全页源驻留、MemAvailable至少12 GiB、owned swap/host pswpout增量为0、接口计数、逐range profile和精确清理回执。profile诊断要求rx_missed_errors增量为0；若资源、完整性、清理或接口门失败，停止，不重跑，并保留部分证据。profile字段和scope必须齐全且聚合总帧数符合实际帧尺寸。诊断配置的吞吐只作为观测背景。

## 完成判据

测试先红后绿；R1/R2固定最终提交的脚本测试、构建和相关CTest通过；两个矩阵全部样本和完整性/资源/网络/清理门通过；profile receipt能区分两项候选的接收端处理成本。结果只用于选择下一项单一优化机制，不晋级默认，也不直接宣称向40–50或100 Gbps达标。
