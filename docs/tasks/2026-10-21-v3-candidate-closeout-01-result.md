# V3-CANDIDATE-CLOSEOUT-01 结果

Date: 2026-10-21 (Asia/Shanghai)
Status: complete for the software-candidate boundary
Owner: current commander only; serial development in Shenzhen
Candidate source commit: `3ef398c8d896a7ca534ce89de6c21043bf115ca0`
Candidate package source: the clean Shenzhen clone at the candidate source commit above
Evidence commit: this document and the task-board update are committed separately after the package freeze

## 本阶段解决的问题

本阶段把“代码能编译”推进到“可以拿同一份候选包在两台服务器隔离验证”。候选版本、安装包、测试命令和限制都有固定记录，后续不会因为一次临时测试结果就改变候选边界。

## 已完成的验收

| 项目 | 结果 | 证据 |
|---|---|---|
| 深圳桌面构建 | 413/413 CTest 全部通过 | `/tmp/cpnetflux-runs/V3-CANDIDATE-CLOSEOUT-01/ctest.log` |
| 上海同源核心构建 | 409/409 CTest 全部通过 | `/tmp/cpnetflux-runs/V3-CANDIDATE-CLOSEOUT-01/ctest-core.log` |
| 深圳安装包自检 | DEB 解包、安装检查、14 项本地矩阵、客户端中断恢复、超时恢复、服务重启恢复全部通过 | `package-local-matrix.log`、`package-resume-evidence`、`package-timeout-evidence`、`package-restart-evidence` |
| 上海安装包自检 | 与深圳相同的 DEB/TGZ/manifest 校验通过；安装后的客户端中断恢复通过 | `SHA256SUMS`、`stage`、`package-resume-evidence` |
| 深圳↔上海候选包互传 | 1/2/4/8/10 逻辑链路上传和下载 10/10 通过；分片覆盖、checkpoint、源/目标哈希检查通过 | `package-cross-cloud/matrix.json`、`package-cross-cloud.log` |
| 包内容 | 内部候选包，不是公开发布包；不自动启动服务 | `linux-package-manifest.json` |

## 候选包指纹

- TGZ: `cpnetflux-0.1.0+git.3ef398c8d896-x86_64.tar.gz`
  - SHA-256: `343e3052ab8dedc60ffd077ee50344a26c36fd352e6ad8a7322b3473a9d6edbc`
- DEB: `cpnetflux_0.1.0+git.3ef398c8d896_amd64.deb`
  - SHA-256: `60e360dfe2e80729a9cf5a84de09267356c78cdaaf60bc0fa859785ca49e7d04`
- `linux-package-manifest.json`
  - SHA-256: `7eaa8cfc3708b4b30f63d11c286053ee3dddefd713897d752ed76ce34354dde3`
- `SHA256SUMS`
  - SHA-256: `41fc06b7f27e47d3f20f7633350a24672e6b5e17020d23805461214b18f105f4`

上海端已经用同一份 `SHA256SUMS` 校验上述包，未重新打包，也未修改正式服务或凭据。

## 已知限制

1. 上海服务器没有 Qt6 开发配置文件，因此不能在上海重新编译桌面目标；这属于构建依赖差异，不是源码测试失败。上海已完成同源核心构建 409/409，并安装、运行和验收了深圳构建的同一桌面 DEB/TGZ。
2. 本候选仍标记为内部候选（`public_release_ready=false`），不代表正式生产发布。
3. 按用户要求，物理 NIC、local bind 和不同物理链路验证后置；本阶段只验证逻辑 TCP 链路和隔离安装/恢复。

## 剩余工作

本软件候选收尾阶段剩余 **0 项**。下一阶段若继续开发，内容是把已冻结候选部署到配置好的物理网络环境，验证不同物理链路；这属于后置的网络环境验收，不是本阶段未完成的软件代码。

## 验收命令与交接

- 深圳候选源码、构建、打包和测试均在 `/tmp/cpnetflux-runs/V3-CANDIDATE-CLOSEOUT-01` 的任务隔离目录完成。
- 上海只接收同一候选包和同源代码进行对端验收，未作为日常开发副本。
- 候选源提交 `3ef398c8d896a7ca534ce89de6c21043bf115ca0` 已在候选开始前推送并由 GitHub remote 回读一致；本结果文档提交后另行记录。

