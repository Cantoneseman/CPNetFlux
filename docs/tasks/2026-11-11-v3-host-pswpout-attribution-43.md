# 无payload host pswpout归因诊断 43

日期：2026-11-11
输入提交：`e04d1c0fe972f0e407229857c52365c99ca835d9`
状态：计划已定；只读采样，不运行CPNetFlux传输。

## 范围

在R770-1与R770-2同时进行一次120秒无payload观察。host `/proc/vmstat` 的 `pswpout`/`pswpin` 每100毫秒读取一次；MemAvailable与SwapFree一并记录。每秒枚举可见进程的 `VmSwap`、PID/启动时间/cgroup，并读取对应cgroup-v2的 `memory.swap.current`；单独跟踪正在运行的CPNetFlux生产server和desktop agent。遇到host计数变化时保存紧邻前后的样本与进程/cgroup快照。

证据写入 `/home/Su/experiments/CPNetFlux/2026-11-11-v3-host-pswpout-attribution-43/R770-1.json` 和 `R770-2.json`。采样器只通过SSH启动，不修改项目源码或系统配置。

## 非目标

- 不启动客户端、服务器新实例或任何payload传输；不重跑M1-41。
- 不修改swap、sysctl、THP、NIC、路由、服务或现有CPNetFlux进程。
- 不把“未观察到计数变化”写成根因已解决；进程/cgroup同步变化只记为归因候选，不能超出观测证据断言内核因果。

## 验收与受影响文件

两端均完成至少1000次host计数采样和100次进程/cgroup快照；JSON包含单调时钟范围、host计数、MemAvailable低水位、SwapFree范围、进程VmSwap变化和CPNetFlux进程身份；结束时核对没有新建传输进程且计数结果可复读。若计数变化，报告同一窗口内的可见进程/cgroup变化；若没有变化，结论只能为本次观察未复现。

本任务只更新此计划、`docs/ROADMAP.md`和`docs/README.md`；实验JSON放在experiments目录，不提交到Git。结果文档记录采样完整度、计数变化、可归属候选、局限和下一步。保留host `pswpout`增量为零的正式M1门槛。
