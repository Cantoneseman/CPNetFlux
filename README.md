# CPNetFlux

CPNetFlux 面向超算中心节点间科学数据传输，提供分块校验、断点恢复和目录编排。运行时统一使用 V3 执行器、数据协议与工作租约；提供 GridFTP 风格控制接口及 CPNetFlux framed data channel，不是完整 GridFTP 实现。数据面支持 TCP 和显式选择的原生 RDMA SEND/RECV，控制面为 TCP/TLS。

## 当前入口

- [性能路线图](docs/ROADMAP.md)：当前状态、推荐配置、未通过的门和下一项工作；当前性能以此页链接的固定提交结果为准。
- [文档导航](docs/README.md)：设计、执行配置、环境、安全边界和历史实验结果。
- [开发规则](AGENTS.md)：单人串行开发、版本同步、验收、提交与 GitHub 备份要求。

当前主线是[完整 V3 大文件性能里程碑](docs/tasks/2026-11-10-v3-large-file-milestone-35.md)：256 KiB DATA与有界TCP接收缓冲已实现；[Milestone35的1 GiB完整任务](docs/tasks/2026-11-10-v3-large-file-milestone-35-result.md)分别获得中位27.33→35.82 Gbps、34.11→36.95 Gbps，保持显式实验选项。[M1-41结果](docs/tasks/2026-11-11-v3-large-file-m1-41-result.md)记录8 GiB矩阵首个control任务因host swap资源门中止，没有完整吞吐样本；8GiB正式门由[M1-44结果](docs/tasks/2026-11-11-v3-large-file-m1-44-result.md)通过：组合配置中位34.16Gbps；M1-41仍不重跑。

[接收网络定向归因36](docs/tasks/2026-11-10-v3-receive-network-36-result.md)将历史计数定位到mlx5接收队列缺缓冲层；[1MiB DATA候选37](docs/tasks/2026-11-10-v3-data-frame-1m-37-result.md)与[queued mmap接收候选38](docs/tasks/2026-11-10-v3-receiver-pipeline-38-result.md)均未通过完整任务对照并已撤回。[诊断40](docs/tasks/2026-11-11-v3-rx-queue-irq-correlation-40-result.md)在当前固定路径的一次1GiB任务中未复现计数，历史因果仍开放，不再重复抓数。[任务42](docs/tasks/2026-11-11-v3-resource-gate-failure-evidence-42-result.md)补齐失败资源证据；[诊断43](docs/tasks/2026-11-11-v3-host-pswpout-attribution-43-result.md)的两端120秒空载观察未复现host `pswpout`，旧单页变化仍未归因；[M1-44结果](docs/tasks/2026-11-11-v3-large-file-m1-44-result.md)记录8GiB正式门通过；M1-41不重跑。RDMA优化仍暂停；100 Gbps文件吞吐、磁盘持久化与生产readiness尚未验收。

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
