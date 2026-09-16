# CPNetFlux Alpha RC 本地原型包说明

更新时间：2026-05-19

本目录是 CPNetFlux 完整 alpha 原型的本地交付包，已做脱敏处理，可用于本地留档、代码阅读、二次开发和公开备份。

## 包内容

| 路径 | 内容 |
|---|---|
| `src/` | C++ 核心实现，包括控制面、传输、文件 IO、TLS、目录传输等 |
| `include/` | 对应头文件 |
| `tests/` | GoogleTest 单元测试 |
| `tools/` | smoke、perf matrix、demo、release gate、artifact sync 等工具 |
| `docs/` | 项目设计、路线图、安全、观测、release 和 alpha 架构文档 |
| `交付说明/` | 面向交接的中文说明文档 |
| `AGENTS.example.md` | 脱敏后的协作说明样例 |

## 当前版本状态

这是 Phase 6E 后的 Alpha Release Candidate 快照。

已具备：

- 单文件上传、下载、断点续传。
- GridFTP-like 控制面子集。
- 目录 upload/download/resume。
- CRC32C checksum，支持软件/硬件 backend。
- manifest/verified_chunks 恢复事实源。
- token auth alpha。
- control-plane TLS alpha。
- STOR/RETR framed file data channel TLS alpha。
- JSONL event log、稳定错误码、demo、release gate。
- Alpha Release Candidate 一键验收脚本。

仍不属于 beta/production：

- 未实现完整 GSI。
- 未验证 100G 跑满。
- 未保留 owner/group/xattr/ACL/空目录。
- LIST/NLST listing data channel 仍是明文 alpha 限制。
- event log 仍是本地 JSONL，不是集中式观测系统。

## 推荐阅读顺序

1. `交付说明/01_项目说明.md`
2. `交付说明/02_部署流程.md`
3. `交付说明/03_使用说明.md`
4. `交付说明/04_验收与测试.md`
5. `交付说明/05_限制与后续路线.md`
6. `docs/ARCHITECTURE_ALPHA.md`
7. `docs/release/ALPHA_LIMITATIONS.md`

## 安全说明

本包不包含真实服务器密码、GitHub token、私钥、远端私网 IP 配置或 build 产物。需要部署到真实机器时，请按 `交付说明/02_部署流程.md` 自行配置运行环境和凭据。
