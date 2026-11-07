# V3 TCP lane 扩展复核与有界优化 01

日期：2026-11-07；状态：执行中；单人串行 R770-1，R770-2 只同步验收。

## 范围、输入与非目标

输入提交：6e1b7534e14f6d6ec11ad6d10bc84a3ee599849e。解释 1 lane 比 4 lanes 快，并先审计执行路径等价配置差异。先完成一轮固定版本、平衡顺序的测量复核，再依据剖析最多选择两轮不同且有证据的主要因素。没有净收益则停止扩大三阶段架构，保留性能门未通过的结论。

非目标：不恢复 RDMA 优化，不改变网络/内核/NUMA 系统配置，不扩大 payload 队列，不绕过 CRC/恢复/发布语义，不以小文件工作替代大文件吞吐门，不承诺 100G。

## 验收

- 两端同提交、tracked clean、同 CMake 配置/版本与八个二进制 SHA-256 后互传；管理 SSH 与 192.168.100.1/.2 数据面分离，显式 local bind，保留路由/接口计数。
- 每格至少 7 次，顺序交替或轮换；1 lane 默认与 read4/write1 的等价路径作为负对照；1/2/4 lane 扩展与低观测开销控制分组解释。记录 task/应用 elapsed、各阶段、双端 CPU、离散程度，不跨提交/完整性模式混算。
- CV<=5% 为稳定性门；若未达标先报告波动，不能把微小收益当通过。候选晋级建议 task 中位数至少 +10% 且超过配对波动，完整性/恢复/内存无退化；CPU 单独获益只作为效率结果。
- 1GiB tmpfs 输入，最多一个当前输出，逐次 SHA 后删除。MemAvailable>=12GiB，owned VmSwap 和 host swapout 增量均为0；guard 与采样证据完整。收尾核对精确路径、进程身份和清理回执。
- 本轮修改需相关 CMake/CTest 或脚本检查，阶段完成显式提交/推送并回读 GitHub SHA；原有两个未跟踪任务文档保留。

## 受影响文件

初始：本任务记录；实验脚本和原始证据在仓库外。诊断后才确定核心改动文件，预期仅 src/core/io/persistent_tree_transfer.cpp、相关 storage/队列实现及对应 tests/unit；改动前补充精确范围。结果更新 docs/ROADMAP.md、docs/DECISIONS/2026-11-07-v3-pipeline-priority.md 和对应 result，不新增无关流程。

## 步骤与证据

1. 固定基线，审计旧计时与配置；执行 7 轮等价/伸缩复核。
2. 依据等待、CPU、write/checksum/checkpoint 与低观测控制定位主因；诊断采样不混入性能矩阵。
3. 最多两轮单因素候选；未取得收益则 no-go，不继续堆结构。
4. 相关测试、双端对照、清理、提交推送、结果与交接。

证据根：/home/Su/experiments/CPNetFlux/2026-11-07-v3-tcp-scaling-audit-01。
本轮工具已发现 shell 与聊天工具；只调用 shell，无派单。perf stat 权限探针失败（perf_event_paranoid=4），不修改系统配置，使用既有计时和受控探针。
