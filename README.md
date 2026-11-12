# CPNetFlux

CPNetFlux 面向超算中心节点间科学数据传输，提供分块校验、断点恢复和目录编排。运行时统一使用 V3 执行器、数据协议与工作租约；提供 GridFTP 风格控制接口及 CPNetFlux framed data channel，不是完整 GridFTP 实现。数据面支持 TCP 和显式选择的原生 RDMA SEND/RECV，控制面为 TCP/TLS。

## 当前入口

- [性能路线图](docs/ROADMAP.md)：当前状态、推荐配置、未通过的门和下一项工作；当前性能以此页链接的固定提交结果为准。
- [文档导航](docs/README.md)：设计、执行配置、环境、安全边界和历史实验结果。
- [开发规则](AGENTS.md)：单人串行开发、版本同步、验收、提交与 GitHub 备份要求。

当前主线是[完整 V3 大文件性能里程碑](docs/tasks/2026-11-10-v3-large-file-milestone-35.md)：256 KiB DATA与有界TCP接收缓冲已实现；[Milestone35的1 GiB完整任务](docs/tasks/2026-11-10-v3-large-file-milestone-35-result.md)分别获得中位27.33→35.82 Gbps、34.11→36.95 Gbps，保持显式实验选项。[M1-41结果](docs/tasks/2026-11-11-v3-large-file-m1-41-result.md)记录8 GiB矩阵首个control任务因host swap资源门中止，没有完整吞吐样本；8GiB正式门由[M1-44结果](docs/tasks/2026-11-11-v3-large-file-m1-44-result.md)通过：组合配置中位34.16Gbps；M1-41仍不重跑。

[Task48源码核验](docs/tasks/2026-11-11-v3-rx-oob-driver-source-audit-48-result.md)确认mlx5的rx_out_of_buffer为聚合计数，没有逐RQ/流归属；[Task49](docs/tasks/2026-11-11-v3-rx-lane-diagnostic-49-result.md)的单路pwrite样本仍出现OOB/missed +171。[Task50与本阶段收尾](docs/tasks/2026-11-11-v3-sender-pacing-50-result.md)已完成显式、默认关闭的有界发送准入机制及回归。唯一1GiB四路TCP/tmpfs样本28.221Gbps，内容、资源和网络观察窗口通过，但未达40Gbps正式矩阵准入，未运行新8GiB矩阵；该单次结果不能与历史8GiB成绩直接算收益或退化。候选仅保留诊断能力，不作为性能推荐。

Task50历史阶段状态为ENGINEERING CLOSED / M2 OPEN；40–50、70–85/100Gbps与长期稳定性仍未验收。原生RDMA优化继续暂停，仅完成重启价值、MR内存预算与数据所有权条件评估；统一工作单元/预算采样/策略记忆保持DEFERRED。冻结配置、精确版本、回归及下一步条件见[Task50结果](docs/tasks/2026-11-11-v3-sender-pacing-50-result.md)。

最新[Task57 / 交付C冻结](docs/tasks/2026-11-12-v3-private-setup-57-result.md)：默认关闭的原生必要准备路径及并发回执修复已完成，两端相关CTest各261项、kernel pacing smoke各连续10次通过。五对准备中位53.40→7.62ms；唯一1GiB完整目标任务**40.865Gbps**，包含必要准备，内容/资源/网络/页型/清理通过。仅单次可行性准入，正式8GiB M2未运行，不能称稳定40Gbps；下一步先完善有界8GiB支持和正式合同，详见[路线图](docs/ROADMAP.md)。

## 服务器与开发环境

| 用途 | R770-1 | R770-2 |
|---|---|---|
| SSH alias / 管理地址 | `r770-1` / `192.168.1.109` | `r770-2` / `192.168.1.102` |
| 角色 | 唯一权威源码、Git、开发和实验目录 | 同提交构建、测试和互传对端 |
| 项目路径 / 用户 | `/home/Su/projects/CPNetFlux` / `Su` | `/home/Su/projects/CPNetFlux` / `Su` |
| 数据网 | `eno17095np0` / `mlx5_0` / `192.168.100.1` | `eno17095np0` / `mlx5_0` / `192.168.100.2` |

两端为 PowerEdge R770、Xeon 6515P、32 逻辑 CPU、约 30 GiB RAM；Ubuntu 26.04.1，g++ 15.2、CMake 4.2.3、Python 3.14.4。环境快照、构建方法与实验边界见 [R770 环境](docs/coordination/ENVIRONMENT.md)；动态值以实机为准。项目使用 C++20/CMake/Linux、POSIX 文件 I/O，可选 io_uring 仅用于文件 I/O。

## 开发与证据

1. 在 R770-1 项目根目录单人串行开发；R770-2 只接收固定提交。Windows `D:\Project\CPNetFlux` 用于连接、查看和应急回收，不另建并行工作区或使用子代理。
2. 修改前记录范围、输入提交、非目标与验收；保留已有无关修改，只提交明确文件并推送已核实 GitHub remote、回读 SHA。
3. 双机互传前核对同提交、构建配置与二进制 SHA-256；数据明确绑定直连接口，TCP 经该网卡不等于原生 verbs。
4. 原始实验保存在 R770-1 `/home/Su/experiments/CPNetFlux`；Git 只保留代码、测试及结果说明，不提交凭据、大 payload 或构建产物。CPSS(DCC) 独立管理。

历史结果按输入提交和环境解释，见[研究基线](docs/RESEARCH_BASELINE.md)；该历史基线不覆盖当前 V3 规则。迁移与旧环境来源见[迁移记录](docs/tasks/2026-10-30-r770-migration.md)。
