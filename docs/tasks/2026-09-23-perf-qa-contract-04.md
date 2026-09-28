# PERF-QA-CONTRACT-04：适配映射与环境证据联合复审

- 状态：done，B5/B6/B7 blocked；未授权代码
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：04 测试与质量；验收角色：00
- 输入 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；若输入变化则停止并报告 superseded。

## 目标

独立复审 03 的 `PERF-IMPL-ADAPTER-01 v1` 与 05 的 `PERF-ENV-EVIDENCE-02 v1`，判断 B5 `CompressionObservationV1`、B6 服务/流证据关联、B7 durability/cleanup 前置是否可以从 partial 升级，或明确剩余硬缺口。只产出质量结论，不开始正式实现。

## 非目标与允许修改

- 不改 C++、Python、schema、runner、测试、任务板、路线、云端状态或历史证据。
- 不运行项目 validator、CMake/CTest、runner、传输、实验、清理或新的 SSH；本任务只读取已有回执和源码引用。
- 只追加 `docs/coordination/receipts/04-quality.md` 的本任务小节。

## 必须检查

1. 03 的 raw→normalized 映射是否覆盖 single/tree 双向、participant roster、file/worker/lifecycle、失败/重试/并发和五类计数；缺失时必须保持 `unknown/ineligible`。
2. 05 的 PID/服务、挂载点、备份索引和 SHA 证据是否足以支持 GridFTP evidence association 与 B7 cleanup gate；区分文件自身 hash、payload hash、历史 PID 归属和可释放量。
3. 给出 B5/B6/B7 的 `pass`、`partial` 或 `blocked` 结论及进入正式实现前的最小追加条件；不得把纯规格或只读环境证据写成系统通过。

## 验收

- 回执记录输入提交、引用小节、逐项结论、未运行项和是否授权 02/03 代码任务。
- HEAD 不变，`git diff --check` 通过，暂存区为空；不得修改其他文件。

## 执行回执

- 已由既有 04 质量聊天完成，CLI session `9268` 正常终态，退出码 0。
- 交付位置：`docs/coordination/receipts/04-quality.md` 的 `PERF-QA-CONTRACT-04 v1` 小节。
- 结论：B5 blocked（无真实 normalized adapter/roster/全路径五计数）；B6 blocked（无 case 级 server stream evidence，01/03 effective mapping 未冻结）；B7 blocked（无 payload manifest/hash durability、批准归档回读和 cleanup gate）。
- 未运行 validator、Python/CMake/CTest、runner、构建、传输、实验、故障注入、cleanup 或新的 SSH；HEAD 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，暂存区为空，`git diff --check` 通过。
