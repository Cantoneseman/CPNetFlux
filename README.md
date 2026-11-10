# CPNetFlux

CPNetFlux 面向超算中心节点间科学数据传输，提供分块校验、断点恢复和目录编排。运行时统一使用 V3 执行器、数据协议与工作租约；提供 GridFTP 风格控制接口及 CPNetFlux framed data channel，不是完整 GridFTP 实现。数据面支持 TCP 和显式选择的原生 RDMA SEND/RECV，控制面为 TCP/TLS。

## 当前入口

- [性能路线图](docs/ROADMAP.md)：当前状态、推荐配置、未通过的门和下一项工作；当前性能以此页链接的固定提交结果为准。
- [文档导航](docs/README.md)：设计、执行配置、环境、安全边界和历史实验结果。
- [开发规则](AGENTS.md)：单人串行开发、版本同步、验收、提交与 GitHub 备份要求。

当前主线是[完整 V3 大文件性能里程碑](docs/tasks/2026-11-10-v3-large-file-milestone-35.md)：在固定完整性语义和测试条件下跨过 30 Gbps，并验证向 40–50 Gbps 扩展的机制。下一项为完整 V3 TCP 大负载 DATA 原型；三阶段工程已收口，大文件性能目标尚未通过，RDMA 优化仍暂停。历史 benchmark、局部服务率和 tmpfs 成绩不代表磁盘持久化、生产或 100 Gbps 文件吞吐。

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
