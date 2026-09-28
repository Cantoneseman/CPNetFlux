# PERF-MATRIX-01：100 Mbps 单文件与目录匹配对照矩阵设计

- 状态：blocked（02 设计已交；04 的 PERF-QA-01 v1 判定当前工具/比较契约有 blocker，须先冻结路线并修订工具口径，再交 04 复核；本任务不获准执行）
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：02 实验；验收角色：00，后续由 04 独立审查
- 目标及理由：设计一个尽早可执行、规模受控的 100 Mbps 跨域筛选矩阵，同时覆盖单文件和目录，与真实 GridFTP 在相同数据、方向和计时口径下对照。
- 非目标：不 SSH、不构建、不运行任何 case、不生成或清理 payload、不修改 runner/schema/证据/BOARD；不将旧 dirty 实验数据当作新基线。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；00 实查工作树干净。
- 必读资料和证据路径：本任务、`docs/coordination/BOARD.md`、`docs/coordination/ENVIRONMENT.md`、`docs/RESEARCH_BASELINE.md`、两份 2026-09-17 路线决策、`docs/perf/README.md`、`tools/perf/run_gridftp_private_matrix.py`、`tools/perf/run_gridftp_tree_private_matrix.py` 及真实 GridFTP 对照 runner/schema。
- 允许修改的文件/目录；禁止修改的资源：仅追加本角色 `docs/coordination/receipts/02-experiments.md` 的任务回执；不得改任务单、决策、BOARD/ROSTER、源码、runner、历史证据或其他角色文件。
- 工作分支/worktree：只读设计，无代码 worktree；不得操作共享 index。
- 前置条件、环境占用和停止条件：本任务依赖用户已确认的两类目标和校验可不启用完整末尾重读；不假设上海资源已恢复。若仓库无真实 GridFTP 对照入口，指出并停止，不用 GridFTP-like 私测代替。
- 验收标准及实际可执行命令：在回执提出明确分阶段筛选矩阵，列出方向、数据集/精确字节数、单文件连接数、目录 file-parallelism、每文件连接数、两档校验、重复数、seed、顺序随机化、GridFTP 映射、计时边界、外部 hash、有效样本与失败分类、预计 case 数和空间预算。筛选矩阵应先少量重复找候选，再只对候选做确认；CPNetFlux 内部阶段不得伪造为 GridFTP 指标。只读任务无需 CMake/CTest；完成后执行 `git diff --check` 并核对回执输入 HEAD 和白名单。
- 云端运行目录、端口、资源上限、清理与归档方案：本任务不分配云端运行目录/端口，不启动实验。方案需遵守每个相关挂载点至少 10 GiB 加 payload/build/证据预算、使用隔离目录、逐 case 保留证据后再清理。
- 预期产物路径：更新 `docs/coordination/receipts/02-experiments.md`。

## 派给角色的消息

请续接 ROSTER 中既有 02 聊天。核对实时 HEAD/status 和本任务后，仅设计矩阵，不执行实验。明确识别现有脚本是否真的启动外部 GridFTP，避免把 `gridftp` 命名的 CPNetFlux 私网 runner 当成真实对照。报告旧跨云结果只能作假设来源，固定提交的新结果才进入当前性能结论。完成后更新自己的回执并交付摘要，不派发其他角色。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- case 规模与资源估算：
- 未解决问题与下一步：

## 验收与路线变化

00 总指挥验收；待 01 路线与 04 QA 冻结后，才能另发运行任务。
