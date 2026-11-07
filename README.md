# CPNetFlux

CPNetFlux 面向超算中心节点间科学数据传输，提供可靠的分块传输、校验、断点恢复和目录编排。V3 使用 GridFTP 风格控制接口及 CPNetFlux framed data channel；数据面可使用 TCP 或显式选择的原生 RDMA SEND/RECV，控制面仍为 TCP/TLS。项目不是完整 GridFTP 实现。

## 当前性能状态

当前按用户要求优先优化非RDMA三阶段流水线，RDMA队列/注册池优化暂停。[三阶段最新结果](docs/tasks/2026-11-07-v3-stage-decoupling-01-result.md)完成独立读/网络/写并发、接收共享就绪writer池与有界payload预算；R1全量607/607、对端相关270/270、pool故障9/9，118项1GiB内存任务完成校验清理。池化改善相对准入方案的交接开销，默认仍read0/write0；精确同版本吞吐/CV/双端CPU见结果，三阶段吞吐门仍开，不能把工程完成称为100G。

当前盘上源读取约4Gbps，软件实验采用小规模驻留内存并保留空间/交换门。历史ib_send_bw约92.59Gbps、direct bench四连接约82.97Gbps均不是真实文件成绩；没有100G文件或1Tbps多链路验收。

## 当前服务器与开发环境

| 用途 | R770-1 | R770-2 |
|---|---|---|
| SSH alias / 管理地址 | `r770-1` / `192.168.1.109` | `r770-2` / `192.168.1.102` |
| 角色 | 唯一权威源码、Git、开发和实验目录 | 同提交构建、测试和互传对端 |
| CPNetFlux 路径 | `/home/Su/projects/CPNetFlux` | `/home/Su/projects/CPNetFlux` |
| 数据网 | `eno17095np0` / `mlx5_0` / `192.168.100.1` | `eno17095np0` / `mlx5_0` / `192.168.100.2` |

两端为 PowerEdge R770、Xeon 6515P、32 逻辑 CPU、约 30 GiB RAM；Ubuntu 26.04.1，g++ 15.2、CMake 4.2.3、Python 3.14.4。动态环境值和完整开发规程见 [R770 环境文档](docs/coordination/ENVIRONMENT.md)。SSH 私钥、口令和完整 SSH 配置不入仓库。

## 当前路线

1. [路线图](docs/ROADMAP.md)为当前主线；[流水线优先决定](docs/DECISIONS/2026-11-07-v3-pipeline-priority.md)覆盖旧先做RDMA顺序。
2. 三阶段工程门交付、吞吐门保持开放。read0/write0、depth2、batch1为默认；显式配置与预算见[指南](docs/V3_TRANSFER_STAGES.md)。下一优化依据profile选择交接或kernel写路径一个因素。
3. 小文件微批、RDMA/CQ/peer-mapping、长期稳定性尚未关闭；30–50Gbps和70–85Gbps为计划门，不是成绩或保证。
4. 最新实测提交`ccca9b66bab457ecb6e6c3780d7c6fe1d1e33179`（主要性能矩阵`8d44802a`，八二进制相同）。R1单人直接开发、R2同提交构建验收；GitHub备份、八摘要与实验边界见最新结果和外部证据目录。

阅读[文档索引](docs/README.md)、[详细计划](docs/tasks/2026-11-04-performance-roadmap.md)和[研究边界](docs/RESEARCH_BASELINE.md)。下面旧摘要为历史，冲突以当前入口和R770规则为准。

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
