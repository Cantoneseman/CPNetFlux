# PERF-ROUTE-01：单文件与目录联合性能路线

- 状态：blocked（CLI resume 于 2026-09-23 10:25:58 再次返回 `already has an active writer`；不启动第三个控制器、不抢占，待原 writer 释放或其持有者通过同一 app-server 调度）
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：01 架构；验收角色：00，后续由 04 独立审查
- 目标及理由：按用户确认的目标，定义 CPNetFlux 在现有 100 Mbps 跨域云链路上同时改进单文件与多文件目录传输、并接近匹配配置真实 GridFTP 的路线。将未来 100G readiness 作为独立高速环境验收，不从 100 Mbps 结果外推。
- 非目标：本任务不改代码、测试、默认值或协议；不运行 SSH、构建、CTest、性能实验；不将历史 dirty 结果作为当前基线。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；00 实查工作树干净。
- 必读资料和证据路径：`AGENTS.md`、`docs/coordination/BOARD.md`、`docs/RESEARCH_BASELINE.md`、两份 `2026-09-17` 路线决策、`docs/perf/README.md`、`PHASE4L_STABILITY_AND_RETR_BREAKDOWN.md`、`PHASE5B_TREE_DATASET_MATRIX.md`、相关 options / final-verify / 单文件数据通道源码。
- 允许修改的文件/目录；禁止修改的资源：新增 `docs/DECISIONS/2026-09-23-100m-performance-route.md`；可更新本角色 `docs/coordination/receipts/01-architecture.md` 记录任务回执。不得修改 BOARD/ROSTER、源码、测试、旧证据和其他角色回执。
- 工作分支/worktree：文档任务使用当前工作区，不切分支；不得操作共享 index。
- 前置条件、环境占用和停止条件：用户已确认双路径目标、目前仅有 100 Mbps 跨域服务器、可先不启用完整校验。若源码能力与交接描述不符，记录实查证据并报告，不自行扩大范围。
- 验收标准及实际可执行命令（不要只写“所有测试”）：决策文件须明确目标/非目标、100 Mbps 对照门槛、未来 100G 边界、单文件与目录独立指标、两档校验口径、风险、依赖与下一步角色任务；不得将 100 Mbps 宣称为 100G 验证。完成后执行 `git diff --check` 并回读文件核验链接、输入 commit 和状态语义。本任务不运行 CMake/CTest。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用；不得访问云端。
- 预期产物路径：`docs/DECISIONS/2026-09-23-100m-performance-route.md` 及本角色回执更新。

## 派给角色的消息

请续接 ROSTER 中既有 01 聊天。在 `D:\Project\CPNetFlux` 核对本任务、BOARD、实时 HEAD/status 后再执行。决策中建议区分两种测量：A) `checksum=none` 的 fresh-transfer 吞吐诊断，独立 SHA-256 在计时外验收；B) `crc32c + verified_chunks`，chunk 校验仍开启，只有全覆盖且 manifest flush 成功才跳过全文件重读，fallback 必须记录。核实 `checksum=none` 时当前 final verify 的实际行为及输出标签，避免把“策略请求 full”写成确实发生完整重读。近期目标建议以匹配配置 GridFTP 中位数比例与波动阈值表达；数值须作为建议门槛并解释 100 Mbps 链路限制。路线决策更新不得改写历史证据。只写授权决策文件和本角色回执，不派发其他角色。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- 未解决问题与下一步：

## 验收与路线变化

00 总指挥验收；待 04 审查后冻结实现和实验任务口径。
