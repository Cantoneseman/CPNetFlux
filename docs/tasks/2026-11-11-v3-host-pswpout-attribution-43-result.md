# 无payload host pswpout归因诊断 43 结果

执行日期：2026-11-11。计划：[任务43](2026-11-11-v3-host-pswpout-attribution-43.md)。
输入提交：`0b18a2486369879cc84f6ad01b03898966d0c944`。

## 结论

两台服务器均完成同步的120秒只读观察。host `pswpout`、`pswpin`、SwapFree、被观察进程的 `VmSwap` 与对应cgroup `memory.swap.current` 均未变化。M1-41中R1增加的单页 `pswpout` 事件在本轮没有复现，仍然无法归因；不能将此次空闲观察解释为问题已解决，也不能据此放宽正式M1的零增量门槛。

## 观测结果

| 节点 | 时长 | host采样 / 进程-cgroup快照 | pswpout | pswpin | MemAvailable最低 | SwapFree |
|---|---:|---:|---:|---:|---:|---:|
| R770-1 | 120.046秒 | 1201 / 115 | 4,679,614 → 4,679,614页 | 37,624 → 37,624页 | 30,741,278,720 B | 8,069,861,376 B，稳定 |
| R770-2 | 120.085秒 | 1201 / 114 | 2,292,431 → 2,292,431页 | 212,976 → 212,976页 | 30,209,257,472 B | 7,698,169,856 B，稳定 |

两端均高于12GiB MemAvailable资源线。host已有非零swap占用，但观察窗口内SwapFree保持不变。CPNetFlux生产server和desktop agent全程存在，二者进程 `VmSwap` 与服务cgroup swap前后不变；未启动实验客户端或临时server，R1既有2121生产监听保持不变，49335实验端口未监听。没有传输payload、性能样本或系统设置变更。

证据文件：

- R770-1：`/home/Su/experiments/CPNetFlux/2026-11-11-v3-host-pswpout-attribution-43/R770-1.json`，SHA-256 `28b2b7dea65f4f856dc492b2e94c0aa47ad2e0289a6aa8fedbb1bdaa840eb632`。
- R770-2：`/home/Su/experiments/CPNetFlux/2026-11-11-v3-host-pswpout-attribution-43/R770-2.json`，SHA-256 `c310f3c7a6ce7d458710f84b6de2ed9f0dd377363f362cc5ddc4c326504cee4f`。

## 下一步

M1-41保持中止，不重复执行。下一阶段应单独制定新的8GiB完整M1任务合同，使用已修复的失败资源侧车；先核实两端资源基线和运行中系统服务，再进行一次完整门控矩阵。若host计数再次变化，新的侧车必须保留客户端与服务端owned swap、host前后计数和终止时间，失败任务仍不重试。该计划不把本次未复现当作资源门保证。
