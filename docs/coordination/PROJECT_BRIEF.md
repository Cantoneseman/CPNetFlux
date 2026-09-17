# 项目简报

更新：2026-09-17。这是新六角色接手时的有限事实快照；代码、测试和云端状态均须在具体任务开始时重新核实。

## 背景与目标

CPNetFlux 是面向算力网的大规模可靠数据传输研究项目，从旧 GridFlux 半成品提取。目标是建立可复现的性能证据和可长期维护的实现；目前不是生产就绪产品，也没有证明 50G/100G readiness。

技术栈：Linux、C++20、CMake；epoll TCP 多流；POSIX 文件 IO；可选 io_uring file-IO-only；GridFTP 风格控制接口、自定义 framed data channel、manifest、CRC32C、resume、目录编排和 worker 控制连接复用。它不是完整 GridFTP 协议实现。GridFTP 是外部对照名称，保留；历史 SSH 别名、路径和原始证据名称也保留。

## 代码和测试状态

- 接手资料建立前本地 HEAD：`6dad8bf609d5081b170dfe2afc79dc2b39c10a89`；当时工作树干净。后续文档提交可能改变 HEAD，始终运行 `git -C D:\Project\CPNetFlux rev-parse HEAD`。
- 最近实现提交：`3b0820d`。已修改结果分类、compression off 参数传播和实验资源清理。后续提交主要是协作和路线文档。
- 已有 Python 定向测试通过记录；云端隔离 Linux 构建执行过 CTest。**没有全套 CTest 全绿证据**：token-auth/event-log smoke 缺少 `CPNETFLUX_TEST_TOKEN`；scheduler smoke 调整为显式 `--compression auto` 后定向通过；io_uring 有环境跳过项。
- 不能把 parser/参数传播测试等同于证明全部 compression off 热路径为零；完整证据分类、压缩 wire accounting、清理后的证据保留仍值得独立审查。
- 目录阶段 instrumentation 尚未实现；新固定提交的代表性性能矩阵尚未执行。

## 最近实验告诉了我们什么

证据目录：`D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results`。
报告：同级 `RETEST_REPORT_ZH.md`。原始备份在 `D:\Project\GridFlux Beta\_server_backups`。
用户汇报：`D:\读研\干活\算力网项目——存储优化\汇报进度\实验数据汇报.pptx`。

2026-09-16/17 深圳/上海双向重测使用 worker control reuse、真实 GridFTP 对照。实验来自云端 HEAD `16b377359494f19f386ba5d375d353449e45f7a0` **加未提交修改**，不能仅凭这个 commit 重建实验，也不能称为干净的固定提交构建。

- 计划 218 个 case，产出 216 行；报告分类为 124 pass、68 fail_correctness、18 IO blocked、6 runtime failure，另有 2 个 GridFTP resume case 缺失。分类是旧审计器的原始标签。
- 68 个被旧审计器标为 fail_correctness 的 case，其 file/tree hash 相同；问题指向 wire accounting 或 manifest evidence，不能把这些标签直接解释为数据损坏。
- 单文件平均吞吐 CPNetFlux 77.14/78.79 Mbps，对照 87.35/85.42 Mbps；dense 42.40/29.75 对照 79.78/51.03；mixed 40.66/32.62 对照 79.27/48.40。目录差距约 33–49%，单文件约 8–12%。这是混合配置汇总，不能当成逐配置匹配的最优比较，也不能单凭它证明瓶颈因果。
- worker 复用已在日志中观察到。控制/数据连接、首块等待、manifest 等阶段是待测候选原因；不能写成某一瓶颈已证实。
- scheduler 上传受到 compression off 未完全生效的污染，当前不足以比较 scheduler 策略。
- 18 个 io_uring case 缺真实 liburing；资源耗尽影响 runtime/resume。它们未形成独立的功能验收结论。

以上数字需要用于论文或新路线决策时，由实验角色回到原始行验证口径。

## 当前路线和未解决契约

路线文件：`docs/DECISIONS/2026-09-17-retest-route-reset.md` 和 `docs/DECISIONS/2026-09-17-directory-data-plane-profiling.md`。

下一阶段顺序：修复实验环境和固定构建 → 明确 profiling 字段/计时/schema → 实现观测 → 小矩阵复验 → 根据证据选择优化。暂不开启新的 scheduler、compression、io_uring、QUIC 等扩展。

需要架构与质量先校正的文档歧义：

1. profiling 决策中 `first_payload <= payload_io` 把两个独立区间强行排序，没有一般成立的依据；应明确边界事件和各自非负性。
2. 旧文档“约 10M”带宽表述含混；新实验记录真实配置、单位和实测吞吐，不沿用这个值。
3. route-reset 写有 scheduler 三策略小矩阵，后续 profiling 写 scheduler off。当前先按 profiling 的原始传输基线准备，scheduler 另行验收；任务单须明确口径。
4. 阶段 0 的勾选表示已有实现，不能替代固定构建与完整验收。

详细设计按需读 `docs/DESIGN.md`、`docs/ENGINEERING.md`、`docs/DIRECTORY_TRANSFER.md`；历史研究入口是 `docs/RESEARCH_BASELINE.md`。存在冲突时报告证据，不把旧文档断言当作事实。
