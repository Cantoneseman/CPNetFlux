# V3 mlx5 rx_out_of_buffer 源码归因核验48

日期：2026-11-11
输入提交：efeb656ac366e2a8739c2f82ee9312af94b9de64
状态：DONE / READ-ONLY DIAGNOSTIC；不产生性能成绩。

## 任务范围

精确核对 R770-1 正在运行的 Ubuntu 7.0.0-38-generic 对应 mlx5 源码，确认
rx_out_of_buffer 的统计范围；只读检查当前驱动、ethtool、debugfs、
devlink health 与 tracepoint 是否能将计数映射到 Linux TCP RX 队列。

## 非目标

- 不运行 TCP/RDMA 传输，不产生或读取 payload。
- 不修改应用、内核、驱动、NIC、IRQ/RSS/ring、路由或系统参数。
- 不将历史 page-pool 计数解释为接口 OOB 的队列或 lane 因果。
- 不恢复 Task46 profile 矩阵。

## 验收

- 使用与 R1 已安装内核包匹配的 Ubuntu 源码标签，记录不可变 commit。
- 源码确认统计导出和更新路径是否带 RX queue 标识。
- 记录 R1 现场已查的只读接口及其限制。
- 明确网络根因和性能状态，不以源码审计替代运行时证据。

## 下一步边界

源码和现有只读接口若均不能定位到 RX 队列，不做驱动补丁或系统调优。
仅准备一轮有资源停止门、无 profiler 的小型复现诊断：同配置下比较
1-lane 与 4-lane 的 1 GiB TCP 任务，采集前后接口级 OOB 与逐队列 packet、
page-pool、WQE/buffer delta。该诊断只判断 OOB 是否可复现及是否与 lane
并发有关，不能证明具体队列或流的因果；未过资源/版本/完整性门不得传输。

## 受影响文件

- docs/tasks/2026-11-11-v3-rx-oob-driver-source-audit-48.md
- docs/tasks/2026-11-11-v3-rx-oob-driver-source-audit-48-result.md
- docs/ROADMAP.md
- docs/README.md
