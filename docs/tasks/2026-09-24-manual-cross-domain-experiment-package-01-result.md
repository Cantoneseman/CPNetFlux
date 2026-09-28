# MANUAL-CROSS-DOMAIN-EXPERIMENT-PACKAGE-01 交付回执

- 状态：`READY_PENDING_DYNAMIC_QA`。
- 输入/输出 HEAD：执行前后均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 交付范围：新增 `tools/experiments/gridftp_compare/manual_cross_domain_experiment.ps1`、`tools/experiments/gridftp_compare/MANUAL_CROSS_DOMAIN_EXPERIMENT.md`、本结果及独立 CLI 摘要。没有修改旧 runner、传输源码、原始证据、BOARD/ROSTER；未操作 Git index。

## 脚本行为

- 默认 `-Mode plan`，计划目录默认为 `tools/experiments/gridftp_compare/manual-plans/<timestamp>/`；计划包含 single/dense/mixed、CPNetFlux/真实 GridFTP arm、方向、repeat、seed、配置和输入文件 SHA 字段。
- 显式参数包括 `-Mode plan|pilot|full`、`-Direction both|shenzhen-to-shanghai|shanghai-to-shenzhen`、`-Dataset single|dense|mixed|all`、`-LookaheadDepth 0|1`、`-Repeats`、`-Seed`、`-OutputRoot`、`-Execute`。
- `-Execute` 当前 fail-closed：无论动态 QA 状态如何都拒绝执行，明确提示 05 revision-09 和 04 动态验收尚未由脚本验证。当前脚本没有运行分支，不创建远端目录、payload 或 SSH 会话。
- 计划固定 requested CPNetFlux 配置为 POSIX、worker control reuse、scheduler/compression off、checksum none、fresh/no resume；GridFTP 记录外部 `globus-url-copy`/GSI/data privacy。初始 status 区分 transfer、integrity、evidence、wire 和性能 eligibility。
- 使用说明描述需要的动态 gate、只读 preflight、失败/blocked/skipped/unknown、不覆盖与不自动重跑、持久化后 cleanup、人工停止与向 00 交接。禁止路径显式列出 `/root/projects/GridFlux-Beta` 和 `/root/projects/CPSS(DCC)`。

## 验收与未完成门禁

- PowerShell parser 语法检查：通过，`PARSER_ERRORS=0`。
- `-Mode plan` 本地演练与生成计划字段核验：通过，退出码 0；新建计划根 `tools/experiments/gridftp_compare/manual-plans/qa-a93280287b7a41239f1cf16f0ff2d92c`，生成 24 个 case，覆盖 3 个 dataset、2 个方向、2 个 system arm、2 次重复，并生成 `case-plan.json`/`README.txt`。该小型计划未连接 SSH、未生成 payload。
- SSH、远端 preflight、构建、payload 和实验：按本任务范围未执行。
- 本版本只生成 case plan/README，并非可运行实验引擎；尚未实现只读远端 preflight、状态恢复/中断判定、逐 case 日志、summary/hash manifest、PID/disk 清理报告或有 QA gate 的真实执行器。因此不能将这些说明性字段当作已运行证据，也不能宣布实验可运行。
- 05 revision-09 固定构建与 04 动态验收状态本轮未重验；状态仍是 `READY_PENDING_DYNAMIC_QA`。交付前还需完成 parser/plan 演练和动态 QA 评审，05/04 通过后由 00 单独放行。

## 实际命令和检查

- 输入检查：`git rev-parse HEAD`、`git status --short --branch`、`git diff --cached --name-only`。执行前 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，index 空；共享工作区已有其他角色文档变更，均保留。
- 写入后待执行：PowerShell parser、plan mode 本地演练、JSON 字段断言、严格 UTF-8/尾随空白检查、`git diff --check`、HEAD/status/index 白名单复核。

## 后续

先补完本地脚本静态/plan 门禁，再由 04 review 脚本失效关闭行为、manifest/状态恢复安全性和证据持久化；05 提供 revision-09 固定构建/端点环境证据。上述动态门禁未通过前，保持不可运行。
