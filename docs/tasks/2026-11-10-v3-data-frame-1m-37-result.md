# Milestone37：1 MiB DATA 候选结果

日期：2026-11-10。输入 `d26a96ea171fde6c20ca3bdb821635cc5c5ab773`。
候选实现提交 `145b45e38f0c337a692a8786459af70903d49dd5`，评估后回退提交
`7d63fa026f7c51cdf5560db2babf968510537924`。R770-1 唯一写入，R770-2 固定提交构建。

## 结论

1 MiB DATA 将每个 1 MiB chunk 从 4 帧减为 1 帧，但同提交五对 1 GiB 完整任务没有取得吞吐收益，
并与 R770-1 `rx_missed_errors` 明显增加相伴。该候选为 **NO-GO**，已移除运行时选项，
保留现有默认 65,532 B 与显式 262,144 B 路径。不再扩大 DATA 帧做尺寸扫描。

这次只证明特定主机、链路和 1 GiB 任务下 1 MiB 候选不合适，不能推断所有系统或网络上的普遍结果。
`rx_missed_errors` 是接口范围计数，驱动统计可异步刷新；它与每个传输样本的对应关系很强，
但不足以单独证明某个 TCP 流的逐包因果。完整性均通过，TCP 可恢复丢包不等于硬件接收队列无压力。

## 完整任务对照

固定提交 `145b45e`、RelWithDebInfo、RDMA/TLS/io_uring/ISA-L=ON；R770-2→R770-1，
显式 bind `192.168.100.2/.1`、`eno17095np0`，TCP 数据面。两端 server 与 upload/download 客户端
SHA-256 完全匹配。每个样本均为驻留 1 GiB tmpfs 源、全新 tmpfs 目标、4 lanes、8 个 128 MiB ranges、
CRC32C/verified_chunks、1 MiB 恢复 chunk、checkpoint16、TLS/压缩关闭、非持久化提交；
无额外网络/CPU 采样器，按 AB/BA 交错，五对全部保留。

| 指标 | 262,144 B DATA | 1,048,576 B DATA |
|---|---:|---:|
| T_ready 中位数 | 0.249412 s | 0.250915 s |
| 中位吞吐 | 34.441 Gbps | 34.234 Gbps |
| 配对更快次数 | 基线 3/5 | 候选 2/5 |
| T_ready CV | 3.93% | 9.21% |
| 每个 1 GiB 实际 DATA 帧 | 4,096 | 1,024 |
| 采样接收端 CPU 秒下界中位数 | 0.85 s | 0.84 s |
| 采样 RSS 峰值 | 264.5 MiB | 246.4 MiB |

候选中位吞吐变化为 **-0.60%**，五对时间改善率为 -0.60%、-18.24%、-19.37%、+4.64%、+9.56%。
帧处理调用减少 75%，接收 CPU 秒下界基本不变；候选任务波动更大，未达到优化保留条件。
以上仅为 1 GiB 受限完整任务，不是 8 GiB M1、磁盘、生产或 100 Gbps 结果。

## 接收计数和资源

R770-1 在五个 1 MiB 候选样本的 `rx_missed_errors` 区间增量分别为 294、554、765、348、208，
合计 **2,169**；五个 262 KiB 基线样本为 0、36、0、0、0，合计 **36**。
完整矩阵起始至结束接口计数总增量为 2,205，与十个样本区间增量完全核对。
这支持把 1 MiB 候选判为网络稳定性负担，但计数仍是接口范围，不能归属到特定流或单一队列。
两臂 `rx_dropped`、`rx_errors` 与 CRC error 均无增长；每个 1 GiB 输出 SHA、8 ranges、
1,024 verified chunks、manifest/发布状态均通过。

十个任务源均驻留，最低 `MemAvailable` 约 27.3 GiB，owned swap 与 host `pswpout` 增量均为 0；
服务进程 PID/start_ticks 未改变。两端 `/dev/shm/cpnetflux-s0-data-frame-1m-37` 在结束时均不存在，
临时目标按样本清理；未运行 8 GiB 测试，也未改网卡 ring、IRQ、RSS、offload、swap 或服务设置。

## 构建和撤回

候选版本 R1/R2 均完成 596 项单元测试、1 项原生 RDMA 硬件用例跳过，相关传输 CTest 7/7 通过；
数据对照前固定提交和三运行程序 SHA 匹配。
候选回退后 R1/R2 在最终提交 `7d63fa0` 同配置重建，单元测试各 589 通过、1 项 RDMA 硬件用例跳过，
相关保留路径 CTest 各 6/6 通过。回退仅撤销本轮 1 MiB 功能与测试，没有重写历史实验。

候选提交 `145b45e` 和回退提交 `7d63fa0` 均已推送并由 GitHub 回读；两端最终源码/构建状态
与 SHA 回执及完整原始测量保存在 `/home/Su/experiments/CPNetFlux/2026-11-10-v3-data-frame-1m-37`。

## 下一步

继续使用 256 KiB DATA 上限，不以 1 MiB 候选结果为由改动网络系统参数。下一轮先围绕接收端
socket drain、CRC 校验和 mmap 写入的服务交接定义一个有界软件候选，验收同时要求完整任务吞吐
不退化、`rx_missed_errors` 不显著增加、恢复语义与现有 64 MiB payload/256 MiB mmap 预算不变。
开始实现前先定清机制、输入提交、文件范围和配对门；若无法提出区别于已测 inline/direct-receive
路径的机制，就保持主线冻结，转向 8 GiB 环境阻塞和原生 RDMA 恢复条件，不继续盲目加并发。
