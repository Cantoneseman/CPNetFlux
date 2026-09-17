CPNetFlux｜01 架构与需求首次接手回执

日期：2026-09-17（Asia/Shanghai）

工作区：`D:\Project\CPNetFlux`

交付对象：00 总指挥

唯一回执路径：`docs/coordination/receipts/01-architecture.md`

**1. 接手范围与证据来源**

已读到原始角色 prompt，以及本轮总控转交的 AGENTS、PROJECT_BRIEF、ENVIRONMENT、BOARD、ROSTER、DISPATCH、本角色 prompt、profiling 决策全文和 04 质量回执。以下仓库、代码和环境事实均来自这些交接材料，并非本人执行命令所得。START_HERE、DESIGN、RESEARCH_BASELINE、route-reset 原文及源码尚未直接读取，不声称完整核验。

本轮仅形成架构接手判断及 SPEC-01 建议；唯一产物为本回执正文，由启动 CLI 的 `--output-last-message` 保存，保存结果待总控核验。未改路线、源码或任务板，未运行 SSH、实验、测试及 Git 写操作。

**2. 输入提交与工作树**

总控提供的实时 HEAD 为 `51820e7aed918fd9e6840ef834acd1705c3848de`；ROSTER 登记本会话 cwd 为上述工作区。二者均未由本人运行命令复核。

输入 status 有六项修改：BOARD、ENVIRONMENT、ROSTER、SETUP_STATUS、START_HERE、`prompts/00-commander.md`；五项未跟踪：DISPATCH 及 02、03、04、05 回执。均不属于本角色修改范围。

04 回执确认最近实现为 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`，至当前 HEAD 的差异仅为文档。旧 `6dad8bf` 不作实时输入；云端 `16b3773` 加 dirty 修改不能重建为同名固定提交实验。

**3. 职责、路线及环境边界**

本角色负责需求边界、接口与测量契约、验收和风险，向总指挥交付规格，不批准新研究方向。按 `R2026-09-17.1`，先治理环境与证据、固定构建，再确认 profiling 规格、实现观测、小矩阵复验，最后依据证据选择优化；SPEC-01 仍待派发。

CPNetFlux 是 Linux/C++20 可靠传输研究，采用 epoll TCP 多流、POSIX IO、framed data、manifest/checksum/resume 和 worker 控制连接复用，不是完整 GridFTP 实现，也无生产或 50G/100G readiness 证明。

本地管理设计、源码和 Git；云端只运行隔离的已提交构建。运维交接称上海仍零可用、深圳约 51.07 GiB，实验前须重验并满足各相关挂载点 10 GiB 余量及峰值 payload 预算。保护两端 dirty 历史目录，不触碰 `/root/projects/CPSS(DCC)`，不读取或转存凭据，不恢复大 payload。

**4. 关键契约矛盾与判断**

以下是本角色依据交接提出的判断，待总指挥收口：

- `first_payload=t_first-t_ready`、`payload_io=t_last-t_first` 是独立区间；分别验证事件顺序和非负性，撤销二者大小不等式。慢首块、快传输可合法违背旧断言。多连接须先定义各流事件及文件聚合方式；跨主机时钟不能直接相减。
- 并发阶段 sum 不是 wall time。04 定位的 runner wall 包含准备、验证和收尾，客户端 elapsed 边界不同；应分别命名，禁止强求相等，并明确吞吐分母及 resume 总量／本轮传输量。
- 空文件、单次 payload、全部 resume 跳过、首块前失败需独立规则；不适用、未完成和缺失不能统一填零。四维状态应独立，缺 hash 不能直接判为完整性失败。
- profiling 的 scheduler off 基线与三策略专项分开；“约 10M”改为实际配置、单位和实测值。目录差距 33–49%、单文件差距 8–12% 是混合配置观察；复用日志不能独自证明收益或剩余瓶颈因果。
- 04 定位的 wire 分类及清理后证据保留风险尚待复验；阶段 0 已实现不等于验收完成。

**5. 建议 SPEC-01**

目标：形成可供实现和实验直接执行的版本化时间边界、JSON/CSV schema、状态映射及矩阵。明确 run/case/file/attempt/worker 标识、端点角色、单调时钟、单位、事件、缺失原因、覆盖率分母和固定 p95 算法。

非目标：不优化协议，不改变 checksum/resume 或默认后端，不扩展 scheduler、compression、io_uring、QUIC。获派前仅影响本回执；后续规格及决策修改文件由任务单列明。

建议验收：

1. 示例覆盖慢首块、空文件、单块、重试、失败、并发重叠及复用；保留原始区间、分布统计和独立 wall。
2. 成功文件完整阶段覆盖率至少 90%，适用性规则单列；证据缺失不改写 transfer/integrity。必要 commit、hash、命令、环境或清理证据缺失的 case 不进入性能结论。
3. 基线固定 worker、scheduler off、compression off、checksum none、POSIX；沿用双向 single/dense/mixed 组合，各三次、同 seed、独立目录及真实 GridFTP 对照，不伪造其内部阶段。
4. 后续固定构建执行 CMake/CTest、tree/resume smoke 和阶段 0 Python 门禁；记录失败、blocked/skipped，不能以跳过代替通过。本轮均未运行。

**6. 尚缺证据与下一步**

仍缺上述未提供原文、源码事件边界核验、固定归档及两端二进制 hash、完整 CTest 日志、off 热路径运行证据、清理后诊断可读证明和新矩阵。68 项旧错误标签对应 hash 相等来自 04 的 CSV 核对，不是本角色重算 payload。

当前结论：可接手，初始化资料学习不构成产品验收。请总控核验本正文落盘；随后等待带版本的 SPEC-01 任务单，与 04 收敛契约，再交总指挥确认并派发实现。今天不启动任何后续任务。
