# CPNetFlux

CPNetFlux 面向超算中心节点间科学数据传输，提供可靠的分块传输、校验、断点恢复和目录编排。V3 使用 GridFTP 风格控制接口及 CPNetFlux framed data channel；数据面可使用 TCP 或显式选择的原生 RDMA SEND/RECV，控制面仍为 TCP/TLS。项目不是完整 GridFTP 实现。

## 当前性能状态

本轮目标是 R770 单端口 100GbE/RoCE 上的真实文件有效吞吐与小文件任务完成时间。已有原生 `ib_send_bw` 约 92.59 Gbps；CPNetFlux direct bench 单连接长传输约 30.35 Gbps，四连接组端到端约 82.97 Gbps。它们都不等于真实文件性能。post-RNR-fix 真实文件矩阵尚待复测；历史约 3.29 Gbps 仅作比较点。当前没有 1Tbps 多链路验证。

## 当前服务器与开发环境

| 用途 | R770-1 | R770-2 |
|---|---|---|
| SSH alias / 管理地址 | `r770-1` / `192.168.1.109` | `r770-2` / `192.168.1.102` |
| 角色 | 唯一权威源码、Git、开发和实验目录 | 同提交构建、测试和互传对端 |
| CPNetFlux 路径 | `/home/Su/projects/CPNetFlux` | `/home/Su/projects/CPNetFlux` |
| 数据网 | `eno17095np0` / `mlx5_0` / `192.168.100.1` | `eno17095np0` / `mlx5_0` / `192.168.100.2` |

两端为 PowerEdge R770、Xeon 6515P、32 逻辑 CPU、约 30 GiB RAM；Ubuntu 26.04.1，g++ 15.2、CMake 4.2.3、Python 3.14.4。动态环境值和完整开发规程见 [R770 环境文档](docs/coordination/ENVIRONMENT.md)。SSH 私钥、口令和完整 SSH 配置不入仓库。

## 当前路线

1. 先完成 [RDMA 端到端基线任务](docs/tasks/2026-11-04-r770-rdma-e2e-baseline-01.md)，在同提交/同配置下量 native、direct bench 和真实文件。
2. 依据拆开的阶段计时决定是否优化 RDMA 队列、建连、三阶段有界流水线或小文件自适应微批。
3. 计划目标：真实文件第一里程碑 10–20 Gbps；大文件并行 range 中期目标 30–50 Gbps；单端口 70–85 Gbps 是冲刺目标。它们是阶段门，不是性能承诺。
4. 本轮传输源码输入基线为提交 `a4ae9963db17bc061aadcfa8d95d13de6c86849f`；本轮文档整理不修改传输源码。

阅读 [详细性能计划](docs/tasks/2026-11-04-performance-roadmap.md)、[当前决策](docs/DECISIONS/2026-11-04-performance-route.md) 和 [文档索引](docs/README.md)。下面旧 README 摘要为历史内容，遇到冲突以以上入口及 R770 当前规则为准。

---

> 2026-10-19：单文件 STOR/RETR 与目录都只使用 V3 事务内核。目录控制面只接受 XCPNETFLUX V3 能力协商，旧 V2 greeting 被拒绝；file-transactions/1 是事务格式编号，不会选择旧执行器。项目尚未正式部署，当前 V3 只读写自己的 manifest/checkpoint；内部数字 schema 不是 V1/V2 运行版本。checksum/resume/TLS/压缩/IO/提交策略保留。

# CPNetFlux

CPNetFlux 是面向算力网的大规模可靠数据传输底座。它提供 GridFTP 风格控制接口，并在内部使用自研的分块、校验、断点恢复和目录编排引擎。

## 当前定位

这是从 2026-09-16 云端半成品提取的长期开发工作区。历史实验结果已保存在外部备份目录；本仓库只保留源码、测试、工具和必要文档。

## 技术基线

- C++20、CMake、Linux
- epoll TCP 多流传输基线
- POSIX 文件 IO，io_uring 为可选 file-IO-only 后端
- GridFTP 风格控制面与 CPNetFlux framed data channel
- manifest、chunk checksum、断点续传、目录级 file orchestration

## 开发入口

1. 先读 `AGENTS.md`、[环境](docs/coordination/ENVIRONMENT.md)、[研究基线](docs/RESEARCH_BASELINE.md) 和 [路线图](docs/ROADMAP.md)。
2. 2026-10-30 起在实验室 R770-1 的 `/home/Su/projects/CPNetFlux` 直接开发；R770-2 为同步构建/传输对端。Windows 只用于连接、查看和回收。
3. 只由当前总指挥单人串行执行，不使用子代理或并行工作区，保留已有 dirty 改动，沿用当前开发分支。
4. 修改前固定范围、输入提交、非目标和验收；只提交明确相关文件并推送 GitHub，回读远端 SHA。
5. 两端互传前同步提交并完成构建和二进制校验；测试走 RDMA 网卡的 IP 路径，当前 TCP 数据面不等于原生 RDMA verbs。

迁移范围与核验见 [R770 迁移任务](docs/tasks/2026-10-30-r770-migration.md)。旧角色和云端工作流文档属于历史，不覆盖当前规则。

CPNetFlux 不等同于完整 GridFTP 实现；`GridFTP` 在文档中仅表示外部兼容协议和对照工具名称。
