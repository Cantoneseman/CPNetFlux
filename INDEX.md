# GridFlux 项目索引

> 面向算力网的高速可靠数据传输底座。对外兼容 GridFTP 接口，对内采用自研高性能传输引擎。
> 覆盖三种传输场景：专线 TB 级网络、虚拟网络、广域网。

## 文档

| 文档 | 内容 |
|------|------|
| [DESIGN.md](docs/DESIGN.md) | 项目背景、目标场景、系统架构、技术选型、可行性与风险 |
| [ROADMAP.md](docs/ROADMAP.md) | 分阶段路线图、当前状态、决策记录 |
| [ENGINEERING.md](docs/ENGINEERING.md) | 工程规范、性能基准、验证方案、开发策略 |
| [DIRECTORY_TRANSFER.md](docs/DIRECTORY_TRANSFER.md) | 多文件/目录传输 alpha 用法、manifest 与恢复边界 |
| [DEMO.md](docs/DEMO.md) | Alpha demo 数据集、local/private demo runner 与 operator quickstart |
| [ARCHITECTURE_ALPHA.md](docs/ARCHITECTURE_ALPHA.md) | 完整 alpha 原型架构：控制面、framed data、manifest、目录、安全和 release gate |
| [SECURITY.md](docs/SECURITY.md) | Phase 6A token auth、Phase 6C control TLS 与 Phase 6D STOR/RETR data TLS alpha 安全边界 |
| [OBSERVABILITY.md](docs/OBSERVABILITY.md) | Phase 6B/6C/6D JSONL event log、稳定错误码、demo/gate/soak 排障入口 |
| [ALPHA_LIMITATIONS.md](docs/release/ALPHA_LIMITATIONS.md) | 完整 alpha 限制清单与后续路线 |

新接手请按 DESIGN → ROADMAP → ENGINEERING 顺序阅读。

## 项目状态

**当前阶段：** Phase 6E — 完整 alpha 原型收口与长跑验收包（已完成）

**技术栈：** C++20 · epoll 网络基线 · POSIX file IO 默认后端 · 可选 file-IO-only io_uring · CMake · Linux only

**目标场景：** 专线 TB 级（主线）· 虚拟网络 · 广域网

**下一步：** 进入 alpha 后续评审：决定 beta readiness、长稳测试时长、生产化部署/安全边界和 100G 专项验证计划；默认仍保持 anonymous、`tls-mode=off`、`data-tls-mode=off`、POSIX backend、full final verify 和现有 framed STOR/RETR 语义。

## AI 协作

本地私有 [AGENTS.md](AGENTS.md) 包含 AI 协作的上下文指引和私有测试拓扑，不能公开发布。公开仓库使用 [AGENTS.example.md](AGENTS.example.md) 作为脱敏模板。接手时先读本索引和 [ROADMAP.md](docs/ROADMAP.md)。
