# 02 实验与证据：首次接手回执

日期：2026-09-17（Asia/Shanghai）。角色：02 实验与证据。交付对象：00 总指挥。
状态：本地接手，等待任务单；EXP-01 未启动。本回执不是新实验结论或生产 readiness 证明。

## 本次范围与验收

- 范围：读取协作入口、本角色 prompt、研究基线、两份 2026-09-17 决策；检查本地证据索引，抽查 6 条 CSV 数据记录和 2 份对应 client summary；只写本回执。
- 非目标：不连接云端、不重跑或 dry-run 实验、不重新汇总全部原始行、不重算 payload hash、不改原始证据/源码/BOARD/ROSTER，不操作共享 index、不 git add/commit，不创建实现 worktree。
- 受影响文件：仅 `docs/coordination/receipts/02-experiments.md`（启动时不存在）。
- 验收：输入 HEAD/status 可追溯；路径存在性、抽样范围、已核实/待核实口径明确；候选矩阵/时钟与 schema 待确认项/阻塞齐备；执行本回执文档脚本门禁。无实现或测试改动，CMake/CTest 不属于本次接手验收，不声称其通过。

## 输入提交与工作树

- 启动实查 HEAD：`51820e7aed918fd9e6840ef834acd1705c3848de`，不是资料中的 `6dad8bf`；写入前复查相同。
- 启动与写入前 `git status --short`：仅 ` M docs/coordination/ROSTER.md`。这是既有改动，本角色未修改。
- 写入前 `git diff --cached --name-only` 无输出，暂存区为空。
- `3b0820d` 是资料记载的最近实现基线，不把当前文档 HEAD 或历史实验 HEAD 自动当作获准实验输入。后续需运维交付任务指定的固定 commit、archive SHA-256、binary SHA-256、构建参数及依赖清单。
- 已读：AGENTS、START_HERE、PROJECT_BRIEF、ENVIRONMENT、BOARD、本角色 prompt、RESEARCH_BASELINE、retest-route-reset 与 directory-data-plane-profiling 决策；按需查看当前 runner.py、schemas.py、dataset.py 的矩阵/结果/计时片段。

## 本地证据存在性

本次只验证下列路径存在和有限内容可读，未验证备份完整性、归档可解包性或全部证据 SHA-256。

| 路径 | 本次检查 |
|---|---|
| `D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results` | 存在；索引含 case_plan.json、environment.json、results.csv、summary.csv、dataset_manifest.json、command.jsonl、cases、command_logs 等 |
| `D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\RETEST_REPORT_ZH.md` | 存在，已读；报告与 results 同级 |
| `D:\Project\GridFlux Beta\_server_backups` | 目录存在；未展开、恢复 payload 或校验备份 |
| `D:\读研\干活\算力网项目——存储优化\汇报进度\实验数据汇报.pptx` | 文件存在；未打开或修改 |
| `D:\Project\CPNetFlux-evidence\<task-id>` | ENVIRONMENT 规定的未来回收路径，本次未创建、未认定为已有证据 |

下文 E 指上述 results 目录。旧原始标识 `gridflux` 与历史路径保持原样；新方案统一使用 CPNetFlux/cpnetflux。

## 已核实：有限抽样所支持的事实

1. E/case_plan.json 的 `case_count=218`，cases 索引长度也是 218；run_id 为 `20260916T114921Z`。dataset_manifest.json 记载 seed=`20260831`。
2. E/environment.json 的 local.git_revision 为 `16b377359494f19f386ba5d375d353449e45f7a0`，local.git_status_short 非空且有 78 行。本次未复核历史远端源码。旧实验必须表述为 **16b3773 加 dirty 修改**；旧报告“固定 commit 构建”不能证明仅凭 commit 复现。
3. 仅抽查以下 6 条 CSV 数据记录（序号按 csv.DictReader、不含表头；不是物理文本行号）。hash 相同指原始字段记载一致，本次未重新读取大 payload 做独立 SHA-256。

| 数据记录序号 | case_id（原始标识） | 观察 |
|---|---|---|
| 5 | `core_gridflux_local_to_remote_single_256MiB_fp1_n1_schedoff-fixed_ioposix-qd1-bs1_fresh_r0` | exit=0，pass，hash_match=true，源/目标 hash 相同；268435456 B / 26.967774 s，按十进制 Mbps 复算 79.631476 |
| 9 | `core_gridflux_local_to_remote_tree_dense_128MiB_fp1_n1_schedoff-fixed_ioposix-qd1-bs1_fresh_r0` | exit=0，旧 fail_correctness，源/目标 hash 相同；wire_bytes=0，错误为 wire accounting 与 verified_chunks evidence missing |
| 77 | `core_gridftp_local_to_remote_single_256MiB_fp1_n1_schedoff-fixed_ioposix-qd1-bs1_fresh_r0` | exit=0，pass，源/目标 hash 相同；268435456 B / 25.368142 s，复算约 84.652776 Mbps（与 CSV 值差为舍入量级）；wire 未提供 |
| 150 | `scheduler_gridflux_local_to_remote_tree_mixed_256MiB_fp4_n8_schedglobal-fixed_ioposix-qd1-bs1_fresh_r0` | compression=off，wire_bytes=4391958，logical_bytes=268435456；exit=0，源/目标 hash 相同，旧 fail_correctness |
| 182 | `io_gridflux_local_to_remote_single_1GiB_fp1_n8_schedoff-fixed_ioio_uring-qd1-bs1_fresh_r0` | blocked_io_uring，error 为 ldd 未检测到真实后端；无退出码/耗时/hash，不能判完整性失败或通过 |
| 205 | `io_gridflux_remote_to_local_single_1GiB_fp1_n8_schedoff-fixed_ioposix-qd8-bs8_fresh_r1` | fail_runtime，exit=1，error 明确含 Errno 28 / No space left on device；无 hash，不纳入有效吞吐 |

4. E/cases/<记录 9 的 case_id>/client_summary.json：128 files、worker、control_connect_count=1、reconnect=0，支持这一个 case 的控制复用已被观测；不能据此断言剩余瓶颈因果。客户端 elapsed=59.5436 s，CSV runner elapsed=65.099958 s，二者不是同一个计时边界。
5. E/cases/<记录 150 的 case_id>/client_summary.json：148 files、worker、control_connect_count=4，wire_bytes 同为 4391958。该数是 4.391958 MB 或约 4.188498 MiB；旧报告“约 4.39 MiB”不可直接沿用。此次没有展开 scheduler events 验证报告中的 4096 次 compression attempts。
6. 当前 runner 的 CPNetFlux tree 计时从准备目录前开始，到独立 hash、停止服务、回收日志后结束（runner.py:1793、1913）；GridFTP 路径也含生成数据与 hash（2102、2220）。这仅证明当前源码边界，历史 dirty runner 的精确版本/边界仍待核对。新实验不能把 runner elapsed、客户端 elapsed、纯 payload 时间混用。
7. 当前 schemas.py 含 integrity_status、evidence_status、wire_accounting_status、exit_code、result，但未见独立 transfer_status 列；字段映射/结果语义须在 SPEC-01 确认，不能只凭阶段 0 勾选就宣称四列契约已完整验收。

## 旧报告记载与待核实事项

- 216 行、124 pass、68 fail_correctness、18 blocked IO、6 runtime、2 个 GridFTP resume 缺失：本次已阅读 RETEST_REPORT_ZH.md 与 final_summary_zh.md，未逐行重计或做 case_id 缺失/重复集合审计。计划 218 已从索引实查；其余统计仍是旧报告记载。
- “68 项 hash 全部相同”仍属报告结论，本次仅抽查其中 2 条。不能将旧 fail_correctness 标签解释为 68 次数据损坏，也不能把 hash 相同自动升级为所有证据/协议语义都通过。
- 单文件约 8–12%、目录约 33–49% 是跨配置平均；本次未重算。下轮必须按方向、数据集、seed、并发/流数、backend、checksum、compression、计时口径匹配，并报告每组重复与离散度，不混合最优值或跨配置平均。旧源码/报告中的“约 10M”与 final_summary 的“100M”不一致，带宽配置、单位、实际吞吐仍待运维证据，不能自行选一个数。
- 记录 5 与 77 各自内部 hash 相等，但两系统的 source_tree_hash 字段值不同。当前源码可见 CPNetFlux single 使用 file_sha256，而 GridFTP 路径使用 tree hash；历史行的 hash 算法/路径归一化及相同输入字节仍需确认，不能直接把跨系统 hash 字符串相异判作不同 payload 或损坏。
- scheduler compression off 污染保留为旧证据边界；wire 较小本身不是压缩因果证明，不据此比较 scheduler 策略。io_uring/resume 无可靠专项验收结论。
- 旧报告/RESEARCH_BASELINE 的“根因”“瓶颈已经转移/集中”等措辞不能取代阶段观测。只保留候选解释和证据强度，不将相关性称作因果。
- 新固定提交矩阵、目录 instrumentation 未完成、全 CTest 尚无全绿证据是交接资料中的状态；本次没有运行测试或复核云端构建。token-auth/event-log 的 token 注入和 io_uring 环境跳过需质量/运维独立处理。

## 建议小矩阵（候选，未获批执行）

沿用后续 profiling 决策，先 raw/worker 基线；不执行 route-reset 中的 scheduler 三策略专项。这里 raw 指不做压缩/staging，仍使用 CPNetFlux 自定义数据通道，不等同于普通 FTP raw data stream。

| 数据集 | 明确数据规模 | (file_parallelism, per_file_connections) | 两方向 × 两系统 × 3 次 |
|---|---|---|---|
| single_256MiB | 1 × 256 MiB | (1,1)、(1,8) | 24 |
| tree_dense_128MiB | 128 × 1 MiB | (1,1)、(4,1)、(8,1) | 36 |
| tree_mixed_256MiB | 128 × 512 KiB + 16 × 4 MiB + 4 × 32 MiB | (1,1)、(2,2)、(4,2) | 36 |

- 共 8 个配置 × 2 方向 × 2 系统 × 3 次 = **96 个计划 case**，其中 CPNetFlux 48、真实 GridFTP 48；preflight/smoke 若任务单要求，另计并明确用途，不混入统计。
- Linux 运行端暂按深圳，local_to_remote=深圳→上海，remote_to_local=上海→深圳；local 不指 Windows 工作区。实际端点角色由任务单冻结。
- CPNetFlux 固定：POSIX、control_reuse=worker、scheduler=off、compression=off、checksum=none、fresh/no resume。checksum=none 不取消外部独立 file/tree SHA-256 验收。
- 两系统统一 dataset seed=`20260831`（建议沿用已查索引值）；记录生成器版本/代码 hash、完整文件清单、字节数、源 hash，统一 hash 算法与相对路径规则。3 次重复使用相同数据 seed、独立 case 目录、每次 fresh 目标。另记执行顺序 seed，建议同配置按重复分块交替系统，减少时间漂移。
- GridFTP 参数映射需质量核对真实命令、实际并发流数与认证配置；只比较可对齐的端到端指标，不伪造同名内部阶段或把 runner 的 worker/POSIX 标签当作 GridFTP 内部实现事实。
- 先冻结显式 case_plan，再启动；当前 runner core 默认包含更多组合，不能直接用 --stage all/core 冒充上述 96-case 计划，也不能用前 N 个 case 截断代替平衡抽样。本次不修改 runner 或生成执行命令。
- 按同配置列 3 次原始值、有效 n、median、min/max 与定义清楚的离散度（如 range/median）；3 次样本不支持强统计/因果结论。失败、blocked、missing、skipped 单独列，不能补成 pass 或零耗时。

## 待架构/质量确认：schema 与时钟边界

- 字段至少区分 run_id/case_id/repeat/attempt、schema_version、双方构建与环境、方向/端点角色、seed、配置、命令引用、退出码、timeout、transfer、integrity、evidence、wire accounting、失败原因；transfer 不因可选证据缺失改写，缺 hash 为 unknown，不直接等于 mismatch。
- file 级记录 file_id、worker_id、host/process/clock_id、阶段起止事件及秒数、logical bytes、wire bytes 定义/统计层级、reuse hit 和阶段适用性。未观测、未走到、不适用、真实零时长需可区分；缺失不填 0。保留事件/原始行后再汇总。
- scan_plan/tree_verify 为 run 级；control_acquire、control_prepare、data_connect、first_payload、payload_io、transfer_complete_wait、manifest_finalize 至少 file 级。上传与下载需各自明确“首/末有效 payload”取发送/接收/落盘哪个事件，控制确认与 finalize 的顺序/重叠由实现给证据。
- 单机同一单调时钟计算区间，跨主机单调时间戳不可直接相减；UTC 只用于关联环境事件。每个已观测区间非负，不接受一般性的 first_payload <= payload_io 约束。
- 同时保留 case 总耗时、客户端 transfer wall、独立 tree_verify 耗时及测量包含项；比较 CPNetFlux/GridFTP 前统一端点事件。吞吐使用明确边界的 logical_bytes × 8 / seconds / 10^6（Mbps），MiB=2^20 B。
- summary 分列 sum/median/p95/max、成功文件数、记录完整文件数、完整率分母；当前决策“至少 90%”需明确是逐 case 成功文件覆盖率，并列缺失原因，不能遮蔽尾部。并发阶段 sum 不等于 wall，不用 sum/wall 当互斥时间占比；p95 是文件分布指标，不是 3 次 run 的可靠尾部估计。
- compression off 的参数传播与实际零压缩工作须质量验证。wire_bytes 的采集层级、发送/接收、协议头/重传/压缩/空值语义须固定；不能把缺失的 0 当成真实网络零字节。

上述为交接建议，尚未向 01/04 完成确认，不代表 schema 已批准。

## 阻塞与下一步交接

1. ENV-01：资料显示上海 2026-09-17 可用 0、100% 满，深圳约 52G；本次未 SSH，不能宣称已恢复。运维须重新核实两端挂载点、CPU/内存、依赖、占用端口/PID，每处保留至少 10 GiB 加本批峰值 payload 预算。
2. SPEC-01：解决时间边界、四列状态、单位、schema 与 scheduler off 范围歧义；01/04 确认后才允许 IMPL-01 做 instrumentation。
3. VERIFY-01/IMPL-01：固定构建清单、质量门禁、阶段记录覆盖率及计时不改变传输结果的证据齐备，再派 EXP-01。旧阶段 0 勾选与定向 Python pass 不替代完整验收。
4. 获准运行时，同一环境只跑一个批次；运维提供隔离目录、端口和占用窗口。不改云端 dirty 历史树，不接触 /root/projects/CPSS(DCC)，SSH/scp 仅部署和回收。
5. 每 case 先保存命令、环境、退出码、独立 hash、manifest 诊断、summary、原始计时与必要失败日志，再清理本 case 自有可再生 payload；记录前后磁盘、PID、清理清单。回收至 `D:\Project\CPNetFlux-evidence\<task-id>`，核对 SHA-256 与归档可读后结束；不把大 payload 放回仓库。
6. 向 00 交付本回执，等待正式任务单。01/04 的 schema 确认和 05 的环境/构建交接仍待总指挥派发；本次没有跨聊天派发，也没有其他角色已执行的证据。

## 实际检查与回执门禁

- 已执行 `git rev-parse HEAD`、`git status --short`、`git diff --cached --name-only`；结果见输入提交段。
- 本地读取使用 `python -c` + pathlib/json/csv，只读检查上述路径和索引；CSV 筛选首个代表性 case，实际取到数据记录 5、9、77、150、182、205；只读两份对应 client_summary。没有运行实验 runner。
- PowerShell 中文输出最初出现编码乱码；设置 Console UTF-8 被 ConstrainedLanguage 拒绝，改用 Python UTF-8 标准输出后重新读取成功。这是本地读取限制，不是项目/实验失败。
- 文档门禁：写入后以只读 Python 命令读回 UTF-8，检查关键章节、6 个 case_id、96-case 计数、输入 HEAD 未变、暂存区为空，以及 BOARD/ROSTER 的写前 SHA-256 未变；失败时命令非零退出。交付前另执行 `git diff --check`、`git diff --no-index --check -- NUL docs/coordination/receipts/02-experiments.md` 和 HEAD/status 复查。
- 首次写入命令因 Windows 命令行引号解析触发 Python SyntaxError，未写入文件；改为 PowerShell UTF-8 字面文本写入后再校验。这不是实验或代码测试失败。
- BOARD 写前 SHA-256：`13cd81069390bee922d609ae3e333f3d6f4ee73cf814ecc920c2f8b56eacf786`。
- ROSTER 写前 SHA-256：`fc602e864825a3f328726dcf1739c10b6e9f187a520088b16ea24040676e70cb`。
- 回执门禁结果：UTF-8、关键章节、6 个 case 回指、矩阵计数、HEAD、空暂存区与 BOARD/ROSTER 保持检查通过。首次空白检查发现本回执 EOF 多余空行，已归一化并复查。
- 交付复查另见 START_HERE、00 prompt、DISPATCH、03/04 回执等并发工作区变化；本角色只观察 status，未修改或暂存这些文件。
- 输出证据：本回执及本会话只读命令输出。未创建额外证据文件、未修改原始记录、未提交。

## PERF-MATRIX-01：100 Mbps 匹配对照矩阵

日期：2026-09-23（Asia/Shanghai）。路线/任务：`R2026-09-23.1 / v1`。此节只交付矩阵设计，不代表已运行或获准开跑。

### 任务范围与输入状态

- 输入 HEAD 实查为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致；开始核对时工作树并非任务单所述干净：`M docs/coordination/BOARD.md`，以及 `?? docs/tasks/2026-09-23-env-01.md`、`?? docs/tasks/2026-09-23-perf-matrix-plan.md`、`?? docs/tasks/2026-09-23-perf-route.md`。这些均为既有并发改动，我未更改或暂存。
- 仅追加本回执；未 SSH、构建、运行/试跑/dry-run case、生成/清理 payload、改 runner/schema、历史证据、任务单、BOARD/ROSTER 或其他角色文件。未操作共享 index。
- 已按任务读取 BOARD、ENVIRONMENT、RESEARCH_BASELINE、两份 2026-09-17 决策、`docs/perf/README.md`、两个 `tools/perf/run_gridftp*_private_matrix.py`、真实对照 `tools/experiments/gridftp_compare/{runner.py,schemas.py,dataset.py,preflight.py}` 的相关段落。

### 对照工具核实

- **仓库存在真实 GridFTP 入口，不触发“无对照入口则停止”。** `tools/experiments/gridftp_compare/runner.py` 的 GridFTP 分支确实执行 `globus-url-copy`；`preflight.py` 检查 `globus-url-copy`、`globus-gridftp-server` 版本及指定控制端口，可按参数执行双向传输/hash smoke。case planner 将真实 GridFTP URL 与本地 `file://` URL 配对。
- `tools/perf/run_gridftp_private_matrix.py` 的 docstring 和 `docs/perf/README.md` 均说明它运行 `cpnetflux-gridftp-server` 加 CPNetFlux framed file client；tree 版本也启动 CPNetFlux server 与 tree clients。名称中的 `gridftp` 指 GridFTP 风格控制面，二者都不是外部 GridFTP 对照，不能放入 GridFTP 组。
- 真对照 runner 的递归 GridFTP 命令映射为 `-cc=file_parallelism` 和 `-p=per_file_connections`；单文件以 `-p=connections` 表示并行流。需保存完整实际命令并以 socket/服务端证据核对观察到的流数，参数计划值不能代替实测值。
- GSI `remote_to_local` 分支不传 `-p`；单文件 `connections>1` 还会改用多个 partial GET，再在本地拼接。该路径不是普通单文件并行流的等价映射。任务冻结时应先验证可双向、可设流数的真实服务/auth 组合；不能匹配的方向/格点标成 `unmatched_config` 或 `blocked_external_gridftp`，不得作为同配置性能差距。GridFTP GSI 的 `-dcpriv` 与 CPNetFlux 默认匿名/无数据 TLS 也可能安全级别不同，必须记录并由路线/质量冻结比较口径。
- 当前 `gridftp_compare` 的 core case 组合写死；没有显式 case-manifest/include 选择器，`--max-cases` 是列表截断，不能用于平衡抽样。`ExperimentCase.checksum` 默认为 `none`，case builder 没有 checksum 维度。故此处真实外部 GridFTP 入口已存在，但下列精确两阶段计划**不能由当前 CLI 直接表达**；在另一个获批实现任务提供显式 case 选择/顺序及 CPNetFlux 校验档后，才可形成可执行命令。不得用私网 GridFTP-like runner替代。
- 当前 runner 的汇总 `elapsed` 从数据准备前开始，包含准备/哈希等工作；不能直接拿它作统一传输 wall。`command.jsonl` 有单调时钟命令时长，但远端 CPNetFlux 命令外包 SSH，而本地 GridFTP 命令不包相同启动开销。执行规格须使用两边 native client 在同一语义边界的 transfer elapsed（CPNetFlux client 内部单调计时、GridFTP `globus-url-copy` 进程单调计时），并确认包括连接建立至服务端最终成功确认、不包括预检、数据生成、独立 hash、归档及清理。若无法取得该边界，就先标计时证据不足。`scan_plan`/CPNetFlux 文件阶段只作 CPNetFlux 内部诊断；不伪造 GridFTP 同名阶段，也不把并发阶段耗时和当 wall time。
- 另一个 runner 的 case cleanup 默认在保存 CSV 行后删除本地 source/dest payload 及该 case 的远端目录；将来任务仍须逐 case 检查清理结果、只保留命令/环境/log/hash/summary 等必要证据，不能开启 `--retain-payloads` 后积累整批 payload。

### 候选矩阵

固定 CPNetFlux 非校验条件：`control_reuse=worker`、`scheduler=off`、`compression=off`、POSIX、fresh/no-resume；chunk=1 MiB、network buffer=64 KiB（沿真实对照 runner 当前默认值），其余写入、预分配、队列参数显式冻结为基线且所有 case 相同。方向：`local_to_remote`（深圳→上海）与 `remote_to_local`（上海→深圳）；只有 Linux endpoint 运行数据面，Windows 仅编排/证据回收。

| 工作负载 | 精确数据集 | 筛选时配置（file_parallelism, per-file connections） |
|---|---|---|
| 单文件 | `single_256MiB` = 268,435,456 B | (1,1)、(1,8)；第一项中的 file parallelism 固定 1 |
| Dense 目录 | `tree_dense_128MiB` = 128 × 1,048,576 B = 134,217,728 B | (1,1)、(4,1)、(8,1) |
| Mixed 目录 | `tree_mixed_256MiB` = 128 × 524,288 + 16 × 4,194,304 + 4 × 33,554,432 B = 268,435,456 B | (1,1)、(2,2)、(4,2) |

分两阶段。一个 matched block 内 GridFTP reference 只运行一次，与两个 CPNetFlux 校验档共用相同输入和流/文件并行配置，不把它重复算成两个独立 GridFTP 样本。

1. **筛选：**上表共 8 个配置格；每格双方向、2 个重复 block。每 block 三个 arms：CPNetFlux `checksum=none`、CPNetFlux `checksum=crc32c`、真实 GridFTP 原生 transfer。计划 `8 × 2 × 2 × 3 = 96` case。每 block 三个 arms 用独立、可复现的顺序 seed 随机打乱；重复 block 间重排。按每个数据集/方向，以 crc32c arm 的 logical goodput 选一个候选并保持相同并行度，再连同 none arm 和 GridFTP 进入确认；若 CRC 档不稳定或不完整，不晋升该格、不看最快的偶然值。
2. **确认：**每个数据集/方向仅测筛选晋升的一个共同并行配置，5 个 block；每个 block 仍为上述三 arms。计划最多 `3 数据集 × 2 方向 × 5 重复 × 3 arms = 90` case。每对 CPNetFlux 校验档使用同一 block 的一次 GridFTP reference，原始 paired block 保留。最多总计 **186 个 transfer cases**；外部 GridFTP preflight smoke 单列，不计入性能 case 或统计。

数据内容固定 seed=`20260831`（与 `gridftp_compare/dataset.py` 当前默认一致），生成器版本和源 manifest/hash 一并冻结；单个 block 中两系统与两个校验档使用逐文件字节完全相同的源数据，但每次 transfer 使用独立 fresh 目标。随机化另用 `order_seed=20260923`，保存 seed、排列和 case id；不按系统整批先后跑。data seed 不用于顺序随机化。随机顺序降低时间漂移偏差，但不证明消除 OS cache 影响；记录每 case 顺序与主机/链路环境，不执行未经授权的全局 cache flush。

### 校验、计时和有效样本

- 两个 CPNetFlux 档定义为 `none` 与 `crc32c`（checksum backend 固定 `auto`，记录实际后端）；外部 GridFTP 不虚报 CPNetFlux CRC 能力，使用其 native transfer 配置。两个系统/档位对**每一个**成功 case 都在 transfer timer 外独立核对源/目标 SHA-256：单文件比较文件 SHA-256；目录按排序相对路径、文件长度、每文件 SHA-256 形成 canonical tree hash，并同时比较文件数/总字节数。
- 为贯彻“可不做完整末尾重读”，拟将性能时间排除外部 SHA/hash 扫描。CRC 档要求 `verified_chunks` 有效且确认所有目标 chunk 覆盖；不允许悄悄回退 full reread。`none` 档须有明确的无完整末尾重读配置，再靠 timer 外独立 SHA 验收。现有文档规定 `verified_chunks` 在 checksum 关闭或 coverage 不完整时会 fallback `full`，真实对照 case 当前又没有校验档选择器；因此这两档的**精确 final-verify 可执行语义是前置实现/QA 门槛**。如果当前二进制只能对 none 档 full reread，应先把它明确列为不同验证工作负载并重新由 01/04 冻结，不能把数据说成 CRC 单变量成本。
- logical goodput 定义为 `logical_bytes × 8 / transfer_elapsed_seconds / 1,000,000`，单位十进制 Mbps；保存原始 byte 数及秒。external preflight 同步记录双向链路容量/RTT及端点 OS、CPU、文件系统、GridFTP/client 版本、auth/data channel 安全级别。没有测量/确认约 100 Mbps 的链路条件时，只能报告当时观察到的链路，不称固定 100 Mbps 矩阵；本任务不推断 100G readiness。
- 筛选 cell 仅在 2/2 block 完成、exit=0、文件数/总字节匹配、外部 hash 一致、有效流/配置与计划匹配、transfer elapsed 正数且所需环境/命令证据齐全时可用于选候选。n=2 的 range/median `>20%` 或任一 block 无效即标 `unstable/inconclusive`，不凭剩余单个值晋升。
- 确认结果逐 block 配对；至少 4/5 个完整有效 block 才报告该 cell 的 median/min/max/range/median 和有效 n。五次结果仍是筛选/描述统计，不做显著性、跨方向合并或因果结论。超时/非零退出/服务器错误列 `transfer_fail`；hash/文件数/长度不符列 `integrity_fail`；manifest、固定 commit/binary hash、命令或必要时间证据缺失列 `evidence_partial`；auth、服务、端口、磁盘/内存预算不满足列 `blocked_*`；计划外跳过/缺行列 `missing/skipped`；实际流数/安全级别/数据集不匹配列 `unmatched_config`。这些不得改写成 pass、零耗时或 correctness fail。wire accounting 独立记 `pass/fail/not_applicable/unknown`；GridFTP 没有等价字段时填 `not_applicable`，不能假设 wire bytes=logical bytes。

### case 与资源预算

- 筛选输入 payload 的配置和为 `2×256 + 3×128 + 3×256 = 1,664 MiB`；计双方向、2 block、每 block 3 arms，传输 logical payload 累计约 **19.5 GiB**。确认阶段为 `2×5×3×(256+128+256) MiB` = **18.75 GiB**。全矩阵上限约 **38.25 GiB logical network payload**；100 Mbps 理想线速下仅此部分下限约 54.8 分钟，实际需加协议/启动/目录开销和离散度，不能把该下限当预计 wall。
- 一次只运行一个 case，任务专属隔离目录下 source/dest/临时文件在证据保存后逐 case 清除。最大单 case 256 MiB；按临时/目标副本留每端 **1 GiB 峰值 payload allowance**，日志/summary/诊断/回收留 **2 GiB allowance**，归档/build 用运维实测大小 `B_host` 单列。故每个受影响挂载点开跑前最低可用预算为 **10 GiB 保护余量 + 1 GiB payload + 2 GiB 证据 + B_host（加测量误差余量）**；任一主机/挂载点达不到预算即阻塞。不能假设上海已恢复。本任务未分配目录/端口，也未核对实时空间、PID 或依赖。
- 未来获批运行只接受明确固定 commit 的源码归档和两端 binary SHA-256；results 落独立 task 目录，原始 CSV/命令/环境/日志/hash/summary 在清理前持久化并回收 SHA-256 验证。Git 只留小型报告/schema/索引，不回收 payload 到工作区。

### 本次实际验证与未决项

- 只读命令：`git rev-parse HEAD`=`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`、`git status --short`、`git diff --cached --name-only`、`rg` 静态定位及 Python UTF-8 只读输出 runner/schema 片段。未运行实验工具、外部 GridFTP preflight 或 case。
- 验收命令：`git diff --check` 与 `git diff --check -- docs/coordination/receipts/02-experiments.md` 均 exit 0（Git 仅报告工作区 LF→CRLF 提示，无 whitespace error）；Python 只读门禁检查必需字段、两阶段 case 数、payload GiB/线速下限、仅追加于 HEAD 回执、HEAD 未变和暂存区为空，exit 0。门禁输出：`DESIGN_GATE_PASS cases=186 payload_GiB=38.25 wire_floor_min=54.76`；回执 SHA-256=`87358f0751bd62b8fe894d742ae7073b8c0baaeb189b91846d9f39980f9294a3`。
- 写入范围复查：`git diff --name-only` 包含本回执，以及并发的 `BOARD.md` 和 `receipts/05-operations.md`；任务文件仍为 untracked。BOARD 与 05 回执未由本角色修改；HEAD 未变；`git diff --cached --name-only` 为空。仅本回执新增 PERF-MATRIX-01 专节。
- 真 GridFTP 入口已确认，但短矩阵当前不能直接由 runner 生成：需单独派单扩展窄范围 explicit case plan/可复现随机顺序，并支持 CPNetFlux none/CRC 两种有效 final-verify 语义；禁止以 `--max-cases` 列表截断冒充筛选矩阵。01/04 还需冻结 auth/data-TLS 是否可比、GSI reverse 多流口径、无末尾重读语义与 timer event 边界；05 需在后续运行任务中复核两端资源及服务占用。
- 下一步：交 00 验收此设计；待 PERF-ROUTE-01、质量审查和环境门槛冻结后，另发实现/运行任务。本次没有向其他角色派单，也不把旧 dirty 跨云数据写成新基线。

## PERF-TOOL-PLAN-01 v1：100 Mbps runner 与证据工具文件级规格

日期：2026-09-23（Asia/Shanghai）。路线/任务版本：`R2026-09-23.1 / v1`。这是工具改造规格，不是代码修改授权或实验批准。

### 范围与当前状态

- 输入 HEAD 实查为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致；路线仍为 `R2026-09-23.1`，BOARD 中本任务为 `in_progress`，未见 superseded 标记。
- 工作区已有并发文档修改：BOARD、ROSTER、02/03/04/05 回执为 modified，路线计划及多份任务单为 untracked；未覆盖、暂存或修改这些文件。本节是本任务唯一改动。
- PERF-QA-01 的实查 blocker 保持有效：case builder 无法生成配对的 96/90 manifest；现有 elapsed 混入准备/hash/收尾；GridFTP 单文件 hash 与 CPNetFlux 类型不同；CRC 档 effective verify 未统一；GSI remote-to-local 并流不匹配；cleanup 不核验返回码且发生在 CSV/summary 持久化前。
- 05 ENV-01 最新快照记录上海 `/`、`/tmp` 可用 0 GiB、清理授权 0 GiB；深圳空间快照也不能替代正式运行前重查。本任务不 SSH、不做 preflight/dry-run/build/transfer，不分配目录/端口，不生成或清理 payload。

### 文件白名单及职责

以下是后续实现任务的候选白名单；除本回执外本轮没有修改其中任何文件。**新实现必须先等待 01 的契约、00 的正式实现任务和 04 的规格验收。**

| 文件 | 计划职责与主要验收点 |
| --- | --- |
| `tools/experiments/gridftp_compare/runner.py` | **[02 可决定]** 加载并全量验证显式 case manifest；依 manifest 顺序执行而非硬编码组合/`--max-cases` 截断；逐 case 保存实际命令、退出/超时、端点、requested/effective 配置、timer 引用、hash 和清理状态；只消费有效 timer 字段计算 goodput；证据核验前禁止 cleanup，cleanup 失败后阻止下一 case。 |
| `tools/experiments/gridftp_compare/schemas.py` | **[02 可决定；依赖 04]** 版本化 manifest/result 字段与验证；分开 transfer、integrity、evidence、wire accounting、config match、timer、cleanup 状态，避免一个 `result` 覆盖多种事实；增加缺失值/不适用值契约。 |
| `tools/experiments/gridftp_compare/dataset.py` | **[02 可决定]** 冻结数据 profile、seed/generator 元数据、逐文件清单和独立 hash；提供两系统共用的单文件 SHA 与目录 canonical tree hash，检查缺失/额外 payload 文件并明确控制文件排除清单。 |
| `tools/experiments/gridftp_compare/preflight.py` | **[02 可决定；依赖 01/04]** 保留真实 `globus-url-copy`/`globus-gridftp-server` 检查；记录版本、服务端点/端口、auth/data-channel 请求设置及双向可用性证据。它不能仅凭 CLI 参数宣称并流等价。 |
| `tools/experiments/gridftp_compare/analyze.py` | **[02 可决定；依赖 04]** 只对 eligibility=true 的样本计算 paired 比较；保留失败、blocked、unmatched、missing、evidence partial 计数，不将无效耗时或空组补零，不把两个 checksum arm 与 GridFTP reference 重复计为独立 reference。 |
| `tools/experiments/gridftp_compare/test_gridftp_compare.py` | **[02 可决定；依赖 04]** 覆盖 manifest/配对/顺序/hash/命令映射/timer 消费/多维分类/持久化与 cleanup gate/paired summary。既有 test 模块可承载纯 Python 测试；不需要为工具规格预先新增模块或修改 CMake。 |

`tools/perf/run_gridftp_private_matrix.py`、tree private runner 和其结果不得成为 GridFTP arm 的实现入口；真实外部对照入口是上述 runner 调用 `globus-url-copy`。若实现拆出新 manifest/evidence 模块，需在后续正式任务中单独列入白名单并由 04 审查，本规格不预先授权新增文件。

### Manifest、配对与顺序

- **[02 可决定]** 使用一个 UTF-8、带 `schema_version` 的显式 JSON `case_manifest.json`，在任何建目录、生成数据、SSH、端口探测或进程启动之前完成 schema/关系校验。顶层最小字段：`schema_version`、`route_version`、`run_id`、`phase`、完整 `input_commit`、`generator_version`、`data_seed=20260831`、`order_seed=20260923`、`order_algorithm`、`expected_block_count`、`expected_case_count`、`endpoint_roles`、`created_utc`、`cases`。运行时实际构建/环境/链路证据写到逐 case evidence，不以计划字段代替。
- **[02 可决定]** 两个方向固定表示为 `local_to_remote`（深圳 → 上海）和 `remote_to_local`（上海 → 深圳）；Linux 两端运行数据面，Windows 仅编排/证据回收。每次运行仍须把实际 host identity 映射写入环境 evidence。
- **[02 可决定]** 每个 case 至少包含 `case_id`、`block_id`、`workload`、`direction`、`repeat_index`、`arm`、`system`、`dataset_profile`、精确 `logical_bytes`/`file_count`/逐文件相对路径与字节数、`dataset_manifest_sha256`、`file_parallelism`、`per_file_connections`、`checksum_requested`、基线固定项（POSIX、worker reuse、scheduler/compression、chunk/buffer、fresh/no-resume）、`auth_mode_requested`、`data_channel_mode_requested`、`expected_gridftp_mapping`、`expected_streams`、`requested_config`、`order_index`、`order_key`、预期对照关系及独立 case 输出目录标识。auth/data-channel/mapping/expected streams 任何一项缺失时 manifest invalid，不允许等运行后补填；这些 requested 字段不证明 effective match。所有 bytes 是整数；MiB 用 2^20，goodput 单位使用十进制 Mbps。
- **[02 可决定]** `block_id` 表示同一 workload、方向、并行配置和 repeat 的一个匹配 block；该 block 恰有 3 个 arms：CPNetFlux `none`、CPNetFlux `crc32c`、真实 GridFTP native transfer。三者共享完全相同的已物化 source manifest/hash；每个 case 使用 fresh、独立目标目录。GridFTP case 的 `reference_for_case_ids` 明确关联同 block 的两个 CPNetFlux arms，避免将一次 reference 伪计为两次独立样本。
- **[02 可决定]** 顺序算法定为对每个 block 的三个 arm 计算 `SHA256(UTF8(order_seed + NUL + block_id + NUL + arm_id))`，按十六进制 `order_key`、再按 `arm_id` 排序；manifest 同时保存 seed、算法版本、key 和最终完整排列。data seed 与 order seed 永不复用。确认阶段只接受单独生成并验过的 manifest，记录筛选选择依据/输入 manifest hash；不允许 runner 运行中挑最快 case 或自动改写确认矩阵。
- **[02 可决定]** 验证必须拒绝重复/缺失 case id、未知 arm、非连续/重复 `order_index`、block arms 不完整、reference 配置不相同、字节数或文件清单不符、目录逃逸/重复目标路径、配置字段缺失/未知、确认格无筛选来源、预期 case 数与实际不符。case list 顺序是唯一执行顺序，不按 system 排序。未来可提供纯验证命令 `python -B -m tools.experiments.gridftp_compare.runner --validate-case-manifest <manifest>`；本轮不运行或实现该命令。
- **[依赖 01/04]** manifest 的 `checksum_requested` 只保存计划值。`checksum_effective`、算法后端、chunk coverage、final-verify requested/effective、manifest flush/commit 策略及何时可比较必须按 01 决策建模、由 03 暴露，并由 04 确认。若无法有效表达两档，validator 应拒绝性能矩阵，不得退化为“参数已请求所以匹配”。

### 矩阵精确计数

| 阶段 | 数据集与字节数 | 配置格 | block/case 算式 | logical payload 上限 |
| --- | --- | --- | --- | --- |
| **[02 可决定] Screening** | `single_256MiB`: 268,435,456 B；`tree_dense_128MiB`: 134,217,728 B（128 × 1,048,576）；`tree_mixed_256MiB`: 268,435,456 B（128 × 524,288 + 16 × 4,194,304 + 4 × 33,554,432） | single `(1,1),(1,8)`；dense `(1,1),(4,1),(8,1)`；mixed `(1,1),(2,2),(4,2)`，tuple 为 `(file_parallelism, per_file_connections)` | 8 格 × 2 方向 × 2 repeats = 32 blocks；每 block 3 arms；64 CPNetFlux + 32 GridFTP = **96 cases** | 19.50 GiB |
| **[02 可决定；晋升门槛依赖 04] Confirmation** | 上述三个 profile；seed 与对应 screen 候选同源 | 每个 workload × direction 一个筛选晋升配置；总计至多 6 个配置格 | 3 workload × 2 方向 × 5 blocks × 3 arms = **90 cases**；60 CPNetFlux + 30 GridFTP |
| **[02 可决定] Total cap** | 同上 | 每方向/workload 保持单个候选格 | 62 blocks、**186 transfer cases**；GridFTP reference 每 block 一次 | 38.25 GiB（筛选 19.50 + 确认 18.75） |

- **[02 可决定]** 每个 arm 是一个 transfer case；preflight 和独立 GridFTP smoke 另记，不进入 186、不进入 paired summary。筛选门槛沿 PERF-MATRIX-01：每 cell 2/2 block 全有效方可晋升，n=2 的 range/median >20% 或任一 block 无效则 inconclusive；确认至少 4/5 个有效配对 block 才出描述统计。保留每个重复值、有效 n、median/min/max/range/median；不得将其称为显著性或因果结论。
- **[02 可决定]** 资源估算沿 PERF-MATRIX-01：logical network payload 总上限 38.25 GiB，100 Mbps 理想线速约 54.76 分钟仅作 payload 下限。每个受影响挂载点最低要求 `10 GiB 保留 + 1 GiB payload + 2 GiB 证据 + B_host(build/archive 实测) + 误差余量`；`/` 与 `/tmp` 同盘时只计一次，不得加总伪造容量。
- **[依赖 05]** 上海当前 0 GiB 可用且没有清理授权；深圳也须在未来运行批次开始前重新核实每个挂载点、进程/端口归属和整批峰值预算。上表不是当前资源许可或预计 wall。

### GridFTP requested/effective 配置与流数证据

- **[02 可决定]** 每个 GridFTP case 的 evidence 至少保存 `globus-url-copy` 与 `globus-gridftp-server` 的完整版本输出、服务 host/control port/passive port range、方向、源/目标 URL 的脱敏形式、真实 argv（保存参数与 flags，移除 userinfo/token/credential）、argv hash、进程 PID/开始与结束记录/退出码、stdout/stderr、环境变量白名单、服务端相关日志、命令对应 case/block id。不得只写“GridFTP parallelism=8”。
- **[02 可决定]** requested mapping 明示：single 的 `-p=per_file_connections`；tree 的 `-r -cc=file_parallelism -p=per_file_connections`；auth/data-channel flag 和方向作为配置字段。保留 socket sampler 原始逐时记录，并写其 host、monotonic clock、采样间隔、观测窗口、端口过滤、服务 PID；另保存服务器侧连接/流观测来源与时窗。计划流数、`-p/-cc` 参数、max observed streams 分列，不互相替代。
- **[依赖 01/03/04]** 实际 auth/data-TLS 安全级别和期望等价配置由 01 冻结；真实并发语义及服务端证据由 03/05 可提供的工具/环境能力决定，04 验收证据是否足够。当前 GSI `remote_to_local` 不传 `-p`，single 多连接时走多个 partial GET + 本地拼接；这类格点默认 `unmatched_config`/`blocked_external_gridftp`，除非新契约批准等价映射并有实际流证据。不得将 partial GET 拼接自动宣称普通并行流。
- **[02 可决定]** 只有 planned/effective auth、安全级别、file parallelism、per-file parallelism、source manifest、方向和实际流证据均达到已冻结匹配规则时，`config_match_status=matched`。缺证据为 `unknown/evidence_partial`；已证明参数或实际流数不一致为 `unmatched_config`；服务不可用/认证失败按原因记录为 `blocked_external_gridftp` 或 `blocked_auth`，不得给 goodput 补零。

### Hash、计时与四维结果

- **[02 可决定]** `dataset.py` 提供所有 system 共用的独立完整性接口：single 记录源/目标 `file_size_bytes` 和文件 SHA-256；directory 记录文件清单、file count、total bytes 与 canonical tree SHA-256。tree canonical 输入按 POSIX 相对路径字节序排序，每项是 `relative_path UTF-8 + NUL + decimal_size ASCII + NUL + lowercase_file_sha256 ASCII + NUL`，对连接后的流做 SHA-256。源清单是预期 payload 文件白名单；目标缺项、额外 payload 项、大小/计数不同单独报出，不可只 hash 现存文件后误判匹配。控制 manifest/临时 partial 等排除项必须按显式 pattern 列表登记并单独归档，不能静默忽略。
- **[02 可决定]** `hash_algorithm=sha256`、`hash_kind=file|canonical_tree`、hash 输入文件数/总 bytes、source/destination hash、逐文件 mismatch 清单、hash 开始/结束 UTC 和耗时均留证；source hash 可在 arm 执行前对同 block 计算一次，但每个 transfer 仍须独立核对 destination。独立 hash、数据生成/拷贝、服务启动/停止、数据目录准备、日志/manifest 回收与 evidence hashing、cleanup 均在 transfer 分母外。
- **[依赖 01/03]** timer 事件含义不由 02 冻结。schema 必须预留 `timer_contract_id`、`timer_source`、`timer_host`、`monotonic_clock_id`、`start_event_id`、`end_event_id`、`start_monotonic_ns`、`end_monotonic_ns`、`transfer_elapsed_seconds`、事件适用/缺失原因及 `runner_wall_seconds`。01 决定两系统同义起止事件及连接/最终成功确认如何计入；03 为 CPNetFlux native transfer wall/effective event 提供值；GridFTP 保留本地进程单调时钟边界证据。禁止跨机器 monotonic 相减，以 UTC 只做关联；字段缺失、负值、时钟不一致、timer contract 不一致即 timer invalid/evidence partial，goodput 留空。CPNetFlux 的 scan/tree/file 内部阶段只作诊断，不映射/伪造 GridFTP 内部阶段；并发阶段 duration sum 不当 wall。
- **[02 可决定]** schema 与输出分别保存 `transfer_status`、`integrity_status`、`evidence_status`、`wire_accounting_status`，另列 `config_match_status`、`timer_status`、`cleanup_status`、`performance_eligible`。transfer 可为 pass/fail/blocked/skipped/unknown；integrity 为 pass/mismatch/unknown/not_applicable；evidence 为 complete/partial/blocked；wire 为 valid/invalid/not_applicable/unknown。仅有可独立证明的 hash 不一致记 integrity mismatch；未取得任一 hash 记 unknown，不升格成损坏。`performance_eligible=true` 需 transfer pass、完整性 pass、必要 evidence complete、config matched、有效且契约一致的 timer，并符合 02 的 cell 有效样本规则。
- **[02 可决定]** 超时/非零退出/客户端或服务端传输错误记 `transfer_fail`；已证明 hash/文件数/bytes 不符记 `integrity_fail`；缺 commit/archive/binary hash/命令/环境/hash/timer 证据记 `evidence_partial`；资源/认证/服务不可用记有原因的 `blocked_*`；未计划 skip/缺行记 `missing_or_skipped`；effective 配置/实际流不匹配记 `unmatched_config`。status 可并存，互不覆盖。GridFTP wire accounting 为 `not_applicable`，不能假设 wire bytes=logical bytes；CPNetFlux wire 异常只影响 wire 维度，不反向改写 transfer/integrity。
- **[02 可决定；依赖 04 验收]** 兼容旧 `result` 字段时将其标为展示汇总，不用它单独决定纳入分析；analyzer 以显式 `performance_eligible` 和 paired block 关系筛样。无有效值时 median/goodput 为缺失，不填 0。

### Evidence durable 与 cleanup gate

- **[02 可决定]** 每个 case 在 run 隔离根下有 manifest 指定的唯一路径与 ownership marker。transfer 结束后先收集命令/环境/退出码、native timer/events、独立 hash、file/tree manifest、服务/客户端必要日志、socket samples、requested/effective config 和 case result；写入 case evidence index，记录每个 artifact 相对路径、size、SHA-256、采集状态。先写临时文件、flush/fsync、原子 rename，再读回并重新 hash；evidence index 自身完成后持久化并验证。日志敏感字段必须脱敏，凭据不进 JSON/CSV。
- **[02 可决定]** 只有所需 artifacts 全部存在、可读、长度/hash 一致且 `evidence_status=complete`，才能执行 cleanup。cleanup 只能删除 manifest 列明的本 case source/destination/regenerable payload 和有 run/case ownership marker 的确切远端根；先 canonicalize/验证目标位于 task 专属根内，不准 broad `rm -rf`、通配 task 之外目录或触碰历史目录。逐个保存本地/远端 cleanup argv（脱敏）、返回码、stdout/stderr、删除清单与事后存在性探针。
- **[02 可决定]** 任一 cleanup 返回非零、超时、所有权不明或残留检查失败时写 `cleanup_status=failed|blocked`，保留已持久化证据和必要诊断，停止同批次后续 case，通知 05 处理。不能把 transfer/hash 状态改写为 pass/fail；已取得的有效 timer 与 cleanup 状态分栏。下一 case 必须等运维确认隔离资源安全并获得正式恢复指令。
- **[依赖 04/05]** 04 验收 evidence 包的最小内容/耐久性和 cleanup failure 注入测试；05 冻结归档落点、实际挂载点预算、清理归属及未来回收 SHA-256 复核。ENV-01 未恢复之前此门禁只作设计，不启动。

### 回归入口与拒绝条件

- **[02 可决定]** 后续 Python 回归的明确入口为 `python -B -m unittest tools.experiments.gridftp_compare.test_gridftp_compare`；本轮没有运行。新增用例应验证：screen 96/confirm 90/block 与 arm 关联；固定 seed 可重复生成同排列、不同 seed/不同 block 有各自排列；manifest 拒绝重复/缺失 case 和不匹配配置；真实 GridFTP plan 含 `globus-url-copy`，私有 CPNetFlux runner 不可满足 GridFTP arm；single SHA 和 canonical tree hash 的已知向量、排序稳定、额外文件/partial 行为；GSI reverse 与 partial GET 未批准时输出 unmatched；invalid/missing timer 不产出 goodput；hash/evidence 缺失与 mismatch 分类不同；wire 错误不改 transfer/integrity；artifact 未持久化/验证时 cleanup mock 不得调用；cleanup 非零保留证据并阻止下一 case；paired analyzer 排除 blocked/missing/unmatched/evidence partial 且 n/median 不补零。
- **[依赖 01/03/04]** timer event 对应、effective verify/coverage/flush、认证和实际流的端到端 smoke 内容要等 01/03 冻结并由 04 独立验收；Python mock 不能替代真实固定构建的 upload/download/hash/resume smoke。未来完整测试还需 Python 回归、获批 CMake/CTest 及实际证据门禁；均不是本规格任务的执行命令。
- **[02 可决定]** manifest 校验不通过、计数非 96/90、hash 类型不符、GridFTP 入口非真实外部工具、计时字段不完整、matched 条件无法证实或 evidence 无法持久化时，拒绝标记 run ready；不得以 preflight/dry-run pass 取代传输 case 有效性。

### 执行回执

- 实际输入/输出 commit：输入 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本节只追加在工作树，未提交，预期 HEAD 不变。
- 实际改动：仅本文件新增 PERF-TOOL-PLAN-01 v1 小节；未修改 task/PLAN/BOARD/ROSTER、其他角色回执、runner/schema/test、原始历史证据或云端文件。
- 实际命令、退出码与证据：本节追加前 `git rev-parse HEAD`（0，匹配）；`git status --short --branch`（0，记录现有并发 modified/untracked 文件）；`git diff --cached --name-only`（0，空）；定向 `rg`/PowerShell 只读代码段检查（0）。追加后待执行最终 HEAD/status、`git diff --check`、仅追加和白名单核对。未运行 Python 测试、CMake/CTest、runner/preflight/dry-run/传输、SSH 或性能 case。
- 文件白名单：未来候选为 `runner.py`、`schemas.py`、`dataset.py`、`preflight.py`、`analyze.py`、`test_gridftp_compare.py`；本轮唯一实际改动是 `docs/coordination/receipts/02-experiments.md`。
- 由 02 决定与必须等待：manifest/配对/seed/order/计数、canonical hash 输入清单、证据文件索引、multi-status 分类、持久化与 cleanup gate 属 02 工具规格；timer event 语义、auth/data-TLS、GSI reverse 多流和 none/CRC effective verify 属 01，native 字段属 03，schema/回归门禁独立验收属 04；资源恢复/归档落点属 05。
- 失败/阻塞：现有 runner 不满足 manifest、timer、hash、cleanup contract；01 决策未交付；上海 0 GiB 可用且清理授权为 0。本任务不运行实验，矩阵保持 blocked/未批准。
- 下一角色可直接执行的下一步：00/04 审查本规格；01 冻结 timer/auth/data-channel/verify 契约，03 确认 native 字段；之后由 00 另发限定上述文件的 runner 实现任务。实现与 QA 通过、05 固定构建和环境预算重新验收前不运行 matrix。

### 验收与路线变化

- 规格沿用 PERF-MATRIX-01 的 96 screening + 最多 90 confirmation；没有替代路线版本或批准执行。若 01 新决策与本节候选冲突，以新冻结契约为准，由 00 更新任务/路线后再派实现；冲突项停止，不按本规格自行适配语义。

## PERF-TOOL-PLAN-02 v2：按 PERF-TOOL-QA-01 修订规格

日期：2026-09-23（Asia/Shanghai）。路线/任务：`R2026-09-23.1 / v2`。本节只补充并收窄 PERF-TOOL-PLAN-01 v1；v1 原文保留为历史审查对象。本规格不授权 runner 实现或启动实验。

### 范围与输入状态

- **[02 可决定]** 输入 HEAD 实查为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致；`docs/PLAN_100M_PERFORMANCE.md` 与 BOARD 仍列路线 `R2026-09-23.1`、PERF-TOOL-PLAN-02 in_progress，未见替代路线。工作区既有 BOARD、ROSTER、02/03/04/05 回执 modified 和任务/计划文档 untracked；均予保留，不暂存。
- **[02 可决定]** 本轮唯一改动为在本回执追加本节。未修改代码、runner、schema、dataset、analyzer、tests、计划、BOARD、ROSTER 或其他回执；不运行 Python/CMake/CTest、preflight、SSH、build、transfer、case 或 cleanup。
- **[依赖 01/03/04/05]** 01 timer/auth/data-TLS/GSI/effective verify 决策、03 native/effective telemetry、04 本规格复审、05 归档根/资源/服务端证据仍未交付。ENV-01 记录上海空间为 0 GiB、清理授权为 0，保持 blocked。

### QA blocker 对应修订

| PERF-TOOL-QA-01 v1 blocker | v2 规格修订 | 责任边界 |
| --- | --- | --- |
| B1 confirmation 固定 90 与部分晋升冲突 | confirmation 的 selected-cell 数 `K` 从筛选后不可变 selection manifest 读取；期望 blocks=`5K`、cases=`15K`，`K∈[0,6]`；`K=0` 不生成 confirmation case manifest。96 screening 固定不变。 | **02 可决定**；selection 门槛由 04 复审 |
| B2 失败后的停止规则不完整 | 定义副作用前 hard-stop、case fail-fast、pending case 的 not-run 状态和不伪造数据规则，见“批次状态机”。 | **02 可决定**；异常注入由 04 验收，环境 gate 由 05 提供 |
| B3 wire 与 performance eligibility 关系不明 | wire 不属于 logical-goodput required-evidence 集合；wire unknown/invalid 不单独改变 eligibility；禁止 analyzer legacy fallback。compression-off 观测另为必需基线证据。 | **02 可决定**；04 复审 required-evidence 与 analyzer |
| B4 attempt/retry 缺失 | 主矩阵固定每 case 单 attempt、禁自动重试；任何诊断 retry 使用独立 attempt/run 记录且不计入主矩阵 n。 | **02 可决定**；04 验收失败/重试分类 |
| B5 effective baseline/compression-off 证据不足 | 分列 requested/effective/observed；以覆盖完整且相关实际计数全零证明 off，缺字段为 unknown/ineligible；不得从命令行、raw/reject 事件或 wire 比例推断。 | **02 定义证据要求**；字段由 03 提供，04 验收 |
| B6 auth/流数配置晚到或缺失 | manifest 在运行前强制写 auth mode、data-channel mode、方向、预期 GridFTP 映射/流数；缺值拒绝 manifest。实际 GSI/流等价仍不自行断言。 | 字段 **02 可决定**；语义/映射依赖 01，server evidence 依赖 03/05，04 验收 |
| B7 cleanup 前没有 run-level durable ledger | 每 case cleanup 前先提交并回读验证 append-only run ledger 中的完整 case result/evidence-index hash；summary 只从 ledger 重建。ledger 不可写/验则禁止 cleanup 并停止。 | **02 可决定**；持久化存储/归档根依赖 05，恢复测试依赖 04 |
| B8 规范化向量及 timer finite/zero 规则未固定 | 给 order key、脱敏 argv hash 固定输入/输出向量；finite/zero/空 payload 数值验证明确，但 timer event 含义仍交 01。 | 序列化 **02 可决定**；事件语义依赖 01，向量测试依赖 04 |

### Selection 与不可变 confirmation manifest

- **[02 可决定]** Screening manifest 仍是固定的 8 格 × 2 方向 × 2 blocks × 3 arms = **96 cases**。筛选完成后由独立、只读的 `selection_manifest.json` 记录 `schema_version`、screen manifest SHA-256、screen run ledger SHA-256、selection policy id/version、`selected_cells`、每个候选格的 2 个 block/3 arms eligibility、CRC32C arm 两个原始 goodput/范围、未晋升原因、tie-break 规则、`selected_cell_count=K`、`expected_block_count=5K`、`expected_case_count=15K`、confirmation seed/order seed。
- **[02 可决定；门槛依赖 04]** 一个 workload×direction 最多晋升一个配置格。只从该格两个 screening block 均完整、eligible、matched 的 `crc32c` logical goodput 中取 median 最高者；仍须满足既定 `range/median ≤20%`。不得跨 workload/direction 挑选。完全同值时按 `(file_parallelism, per_file_connections)` 数值升序作稳定 tie-break。缺任一必要 arm/block、超出离散度门槛、unmatched 或无有效 CRC 观测则该 workload×direction 不晋升；不以另一个 arm 的最快值替代。
- **[02 可决定]** 若 `K=0`，selection manifest 可记录空选择及原因，但不得生成/运行 confirmation case manifest，也不得把 confirmation 计数记为零成功样本。若 `K>0`，每个 selected cell 生成 5 个新 block，每 block 三 arms，因此 `expected_block_count=5K`、`expected_case_count=15K`，`0<K≤6` 时范围为 15 至 90 cases；总计划数为 `96+15K`，最大 186。confirmation logical payload 上限按实际 selected workload bytes 求和：`5 × 3 × Σ(selected_cell.logical_bytes)`，不以 18.75 GiB 固定值覆盖部分晋升情况。
- **[02 可决定]** 每个 confirmation case 必须引用 `selection_manifest_sha256`、来源 screen cell/block ids、screen ledger hash、选择规则版本和 selected tuple；每个 block 恰有 CPNetFlux `none`、CPNetFlux `crc32c`、真实 GridFTP 三 arms，GridFTP reference 一次且显式关联两个 CPNetFlux case。seed、数据清单、方向和配置按 PERF-MATRIX-01 固定，同一个 block 同源数据、各 arm fresh 独立 destination。
- **[02 可决定]** 先完整生成 selection manifest，再生成 confirmation case manifest；两者 JSON UTF-8 无 BOM、canonical JSON 字节、写完后记录 SHA-256。启动前以 selection hash 和 case-manifest hash 核验副本；manifest 生成后不可就地修改、补 case、删 case、改 order 或 expected count。内容变化必须创建新 manifest/run id 并重新审查；运行期间 hash 不符则 `STOPPED_MANIFEST_INVALID`，不得自我修复。
- **[02 可决定]** validator 要复算 K、5K、15K、case/block 唯一性、每 block 三 arms、reference 关系、selection 来源、logical bytes/payload sum、顺序连续性和 manifest hash。Screen 只接受精确 96；confirmation 接受且只接受 `15K`（`K>0`）与该 selection 对应的 cell 集合；K=0 拒绝任何 confirmation transfer manifest。禁止 `--max-cases` 截断生成合法矩阵。

### Attempt 与 retry 语义

- **[02 可决定]** 主矩阵 manifest 固定 `attempt_policy="single_attempt_no_auto_retry"`、`max_attempts=1`。逻辑 `case_id` 属于唯一 planned arm；其实际运行记录必须含 `attempt_id`（首个为 `${case_id}.a001`）、`attempt_index=1`、`retry_of_attempt_id=null`、`retry_reason=null`。进程/SSH/客户端重试不得隐藏在 wrapper、SDK 或 runner 中；遇失败即保存原始退出/timeout，不重跑该 case。
- **[02 可决定]** 每个实际 attempt 是 append-only ledger/result 中的一条独立记录，保存自己的命令 argv/hash、开始/结束、输出目录、返回码、source/destination/hash/timer/status。失败记录不得被后续结果覆盖。主矩阵每个 case 只允许一个 attempt；analyzer 以 `case_id` 和 `attempt_index=1` 聚合，不能把 attempt 数当 repeat/block 数。
- **[02 可决定]** 获准的故障诊断重试必须另建诊断 `run_id`/不可变 manifest，带新 `attempt_id`、`retry_of_attempt_id`、`parent_run_id`、原因、授权任务 id、独立 order index、独立输出目录和新的 cleanup/evidence 引用。诊断 retry 不进入原 screen/confirmation selection、有效 n、中位数、成功率分母或 186 上限；不得通过“保留 retry 中成功的一次”替换原失败 attempt。
- **[依赖 04]** 单次 attempt、超时/中断和诊断 retry 的独立分类/summary 单测由 04 复审；本轮不运行。

### 字段独立判定与 wire policy

- **[02 可决定；依赖 04 复审]** 每个 attempt 分别输出 `transfer_status`、`integrity_status`、`evidence_status`、`wire_accounting_status`、`config_match_status`、`timer_status`、`compression_observation_status`、`cleanup_status`、`attempt_status` 和 `performance_eligible`。批次另有 `batch_status`。状态不能彼此覆盖：退出成功不能替代 hash；缺 hash 是 unknown，只有源/目标完整 hash 或清单明确不等才是 mismatch；wire 不能决定 transfer/integrity；cleanup 不重写已测 transfer。
- **[02 可决定] required evidence 集合**：immutable run/case manifest 与 hash；固定输入 commit/archive/binary SHA 和环境身份；实际 command/version/exit/timeout；成功 transfer 的 source+destination 独立 hash、file/byte counts；已冻结 timer contract 下有效 native/外部 timer 字段；requested/effective config；CPNetFlux baseline 的 compression-off 完整观测；真实 GridFTP arm 的 auth/data-channel/流匹配证据；case evidence index；cleanup 前已验证的 run ledger case commit。每项有 artifact 引用/hash/缺失原因。`wire_accounting` 明确不在此集合中。
- **[02 可决定]** `wire_accounting_status` 可取 `valid`、`invalid`、`unknown`、`not_applicable`；bytes 必须 nullable 非负整数，0 只在计数器来源/方向/scope/观测覆盖明确且真实报告为零时是观测值。缺测写 null+unknown，不能转成 0。至少记录 `wire_counter_source`、host/方向/层级、计数区间、是否含协议头/重传/压缩、覆盖文件/attempt、有效单位。GridFTP native wire 计数未提供时为 not_applicable。负数、非整数、解析错误或无效覆盖为 invalid。off 时 wire 与 logical 不等本身不证明压缩；禁止用 wire/logical 比值推断 compression。
- **[02 可决定]** `performance_eligible` 是**logical goodput 单 case**资格，只在以下条件全部成立时为 true：唯一首发 attempt、transfer pass、integrity pass、required evidence complete、config matched、timer valid 且 finite/正数、逻辑 payload bytes>0、requested/effective 基线配置匹配；CPNetFlux arms 还必须有 `compression_observation_status=verified_off`，真实 GridFTP arm 的 CPNetFlux compression 观测为 not_applicable 且其 native 配置满足已冻结映射。`wire_accounting_status` 和 `cleanup_status` 均不在该布尔公式内；wire unknown/invalid 仅使 wire 结论不完整，不自动排除已有可靠 logical goodput，也绝不自动使其入选。
- **[02 可决定；依赖 04]** paired block/cell 是更高一级资格：一个 block 仅当三 arms 各有且只有一个 eligible 首发 attempt、source manifest 相同、config matched、block id/reference links 正确，才算完整有效 block；confirmation 需 ≥4/5 有效 block，screening 晋升需 2/2。wire 仍独立呈现。analyzer/summary 必须只消费明确 `performance_eligible=true` 的 case 并按 block/arm 配对；旧 `summarize_rows()` 的“有 elapsed/旧 result 即统计”行为禁止作为 fallback。没有有效值时 n=0、goodput/median null，不填 0。
- **[02 可决定]** `cleanup_status` 在 performance bool 之外单列：仅对已完整持久化的结果做 cleanup；cleanup 失败将 stop batch，但不会改写前一行 transfer/integrity/wire/eligibility。该 case 仍可保持单 case eligible；由于其 block 其他 arm 可能未运行，block-level eligibility 仍为 false。失败或 unmatched case 的 payload/诊断先保留，须由批准的后续清理流程处置。

### Compression off 的 requested/effective/observed 证据

- **[02 可决定]** 矩阵的 CPNetFlux arm 在 manifest 固定 `compression_requested="off"`、`scheduler_requested="off"`；GridFTP native compression 字段用 not_applicable/实际值，不伪称实现相同。case evidence 分别记录 manifest 请求值、runner argv/config、client/server/every participating worker 的 `compression_effective`、`scheduler_effective`、`control_reuse_effective`、`file_io_backend_effective`、chunk/buffer effective value、参与进程/worker ids 和报告来源。命令行请求值不能代替 runtime effective value。
- **[02 可决定；字段依赖 03]** 对实际适用的每个 case/attempt 记录全覆盖的压缩观测摘要和原始记录引用：预期/观测 file ids、参与端点/进程/worker scope、生命周期覆盖数；`compression_sampling_attempts`、`compression_attempts`、`compression_dispatches`、`compressed_frames`、`compressed_payload_bytes`（实现若使用不同同义字段，需 03 明确字段映射）。所有计数为非负整数并带 source/coverage。只有请求 off、所有参与路径 effective off、观测覆盖完整、上述动作计数逐项为 0，才能 `compression_observation_status=verified_off`。任一有效正计数标 `observed_active`；effective 不同标 `effective_mismatch`；缺计数/参与者/coverage 标 `unknown`，都是 baseline required-evidence 缺失或 mismatch，case 不 eligible。
- **[02 可决定]** 单独的 `raw`、`sample_unavailable`、`compression_reject` 事件既不说明压缩已发生，也不充分证明零压缩；需由覆盖摘要证明本应观测的 worker/file/attempt 完整且实际动作计数全零。不能用 wire_bytes 等于/小于 logical_bytes 来替代 compression counters。
- **[依赖 03/04]** 03 提供 upload/download、单文件/目录、scheduler off、worker reuse、并发/失败重试路径可关联的 effective config 和完整压缩观察字段；04 验收覆盖与零值语义。当前性能矩阵只声称其实际执行的 `scheduler=off` 配置有证据。若要声称 compression off 在 `global fixed/global adaptive` 或 resume/retry 所有热路径均有效，需先在授权固定构建的回归矩阵覆盖 upload/download × off/fixed/adaptive × 并发及中断/重试，不可从本矩阵外推；不会由本任务运行。

### 批次状态机与 stop policy

**[02 可决定]** runner 以 immutable manifest 顺序、block-major、每 block 内保存的 arm order 执行。默认 fail-fast：会改变样本定义、导致证据不可恢复或危及隔离资源的异常停止本批次；不自动跳过/补跑或重新随机排序。case-level transfer/hash 失败停止当前批次，保证余下 arms 不在已知失败后继续形成偏斜 block。剩余 case 记录为 not-started 状态，不伪造 transfer 行、0 秒、pass 或 skipped-success。

| 触发条件 | 当前 case/批次结果与动作 | 未启动 cases / cleanup |
| --- | --- | --- |
| manifest/selection/hash/count/order/config-required 字段错误或运行前 hash 不同 | `STOPPED_MANIFEST_INVALID`；不做数据生成、网络动作或 transfer | 全部 `not_started_manifest_invalid`；无 payload cleanup |
| preflight/auth/service/port/effective expected mapping 失败 | `STOPPED_PREFLIGHT`；preflight 记 blocked 原因，不启动第一 case | 全部 `not_started_preflight_blocked`；无 payload cleanup |
| 初始或逐 case 资源预算失败/空间跌破 gate | `STOPPED_RESOURCE`；未启动 case 为 blocked_resource；进行中 case 保留已知 transfer 结果并标资源中断/未完成 | 全部 pending `not_started_resource_blocked`；保留必要证据，失败 payload 不自动删除 |
| transfer nonzero/timeout/server error 或 integrity mismatch | 当前 case 分别 `transfer_fail`/`integrity_mismatch`，保存 stdout/stderr/hash/manifest；`STOPPED_CASE_FAILURE` | pending cases `not_started_after_case_failure`；失败 payload 与诊断保留，等待批准的清理/诊断流程 |
| 实际 auth/stream/config 与 manifest 不匹配 | 当前 case `unmatched_config`，`STOPPED_CONFIG_MISMATCH`；不得当匹配样本 | pending `not_started_config_mismatch`；不 cleanup 未核实归属 payload |
| 必需 timer/effective config/compression/hash/env/build evidence 缺失或与 contract 冲突 | 当前 `evidence_partial` 或 baseline/timer invalid，`STOPPED_REQUIRED_EVIDENCE` | pending `not_started_required_evidence`; 若完整 evidence 包无法验证，不 cleanup |
| optional wire accounting unknown/invalid，且 compression observation 和所有 other required evidence 有效 | 仅 wire 子状态 `unknown`/`invalid`；不 stop，允许按原 manifest 顺序执行 | 按正常流程；wire 不改 eligibility |
| case evidence 或 run ledger append/fsync/readback/hash/chain 验证失败 | `STOPPED_EVIDENCE_IO` 或 `STOPPED_LEDGER`; 若不能落盘 stop event，进程非零退出并在 stderr 报最后可信 seq/hash | 绝不 cleanup；保留 source/destination/partial payload；不得启动下一 case |
| cleanup 权限/所有权不明、非零、timeout 或事后残留 | 先保留已提交 case evidence，记录 `cleanup_status=failed|blocked`，`STOPPED_CLEANUP` | pending `not_started_cleanup_blocked`；保留当前 residuals，等待 05/授权操作 |
| 全部 manifest cases 都完成且 run ledger chain 完整 | `COMPLETED`；若出现 wire unknown/invalid，仍可 completed，但报 wire incomplete counts；出现 case fail 不可能走到此态 | summary 从 ledger 派生并 hash |

- **[02 可决定]** 单纯 optional wire unknown/invalid 是唯一明确继续项；transfer/integrity failure、config mismatch、required evidence/timer/compression baseline missing、证据/ledger I/O 和 cleanup failure 均 fail-fast。preflight/resource 失败阻止首个/后续 case。每次停止写 run-level `stop_event`，带原因、触发 case/attempt、最后完成 seq、全部未启动 `case_id` 列表；每个未启动 case 的 elapsed/hash 为 null，`transfer_status=not_run`、其他状态 `not_evaluated`，eligible=false。
- **[依赖 01/03]** timer/baseline 合同不匹配的具体字段由 01/03 决定；本 stop policy 只规定其缺失或违约后拒绝继续，不定义起止事件、auth/TLS/GSI/verify 语义。
- **[依赖 05]** 实际多挂载点资源门限、GridFTP 服务端连接证据来源/观测窗口、run evidence 持久化目的地、残留清理授权由 05 冻结。上海未恢复时不得启动。

### Run-level durable ledger

- **[02 可决定]** 每个 run 建不可变 `run_manifest.json` 与 append-only `run_ledger.jsonl`；ledger 是可恢复的事实源，case CSV/summary/绘图均为可重建派生物，不是唯一记录。每个 JSONL event 至少有 `schema_version`、连续 `seq`、`run_id`、manifest SHA、`event_type`、`batch_status`、case/attempt/block ids（run 级事件可 null）、UTC、host/clock id、前序 `prev_event_sha256`、event payload 和 `event_sha256`。同 run 单 writer；case result event 含所有独立状态、logical bytes、null/non-null timer、hash 与 index 引用、eligibility 和 cleanup 状态。
- **[02 可决定]** ledger canonical bytes 定为 UTF-8 无 BOM、JSON object key 按 Unicode code point 排序、紧凑 separators `,`/`:`、数组顺序保持、`ensure_ascii=false`、拒绝 NaN/Infinity；每条 hash 对除 `event_sha256` 外的 canonical object 做 SHA-256，前一 hash 纳入下一 event。行尾单个 LF。append 后 `flush` + `fsync`；每个 cleanup 前从头回读本轮 ledger，验证 seq 无 gap/duplicate、run/manifest 同一、prev/hash chain、最后一行字节及 case evidence-index SHA，重新 SHA-256 并与待 cleanup gate 比较。
- **[02 可决定]** Case artifact 先各自临时写入、flush/fsync、原子 rename 并回读 hash；case evidence index 再列相对路径、byte size、SHA-256、required/optional、status/reason。完成后追加 `case_evidence_committed` ledger event，payload 含 result row/全部 evidence-index SHA、case manifest SHA、case terminal state、`cleanup_pending=true`；fsync 后执行完整 ledger re-read/hash-chain 验证。只有该 gate 通过才能删除 manifest 明确拥有的成功 case regenerable payload。失败/unmatched case 保留诊断 payload。
- **[02 可决定]** cleanup 完成或失败后，必须另 append `cleanup_terminal` event，包含 cleanup argv 脱敏 hash、每个目标/返回码/timeout/stdout-stderr evidence hash/事后 probe、`cleanup_status` 和 batch state；fsync 并重读验证 chain 后才可启动下一 case。创建文件时也要创建并持久化 run manifest/hash 与 initial ledger event。最终停止/完成事件和派生 summary 都引用最终 ledger SHA；summary 可删除后从 ledger+manifest 重建，不得回写/重排 ledger。
- **[02 可决定]** Ledger 创建/append/fsync/readback/hash 失败时绝不 cleanup、不转下一个 case。进程被杀/断电导致最后状态是 started、cleanup_pending 或无 terminal event 时，新进程不能自动 resume/retry；将 run 标为 `INTERRUPTED_NEEDS_RECONCILIATION`，核对最后可信 ledger hash 与 payload，再由正式新任务决定后续。payload 已清理但 cleanup_terminal 缺失时同样人工 reconcile，不能推定清理成功。
- **[依赖 05/04]** 05 指定 run root 所在文件系统/受控回收路径和真实 durability 支持，含 archive 与传输后 hash 回收；04 验收原子写/追加中断/hash chain/cleanup failure/rebuild summary 测试。当前不创建 ledger 文件。

### 规范化向量与边界检查

- **[02 可决定] order-key fixture**：输入 UTF-8 字节串 `20260923 NUL screen-single-l2r-fp1-c1-r1 NUL cpnetflux-none`（NUL 是单个 `0x00` byte，不是两个字符 `\\0`）；SHA-256 期望 `63f64f123bb5f10461f3675cb8863ab195de09be1054562edf2a0533d187c990`。排序按小写 hex 字节序，随后 arm id ASCII 升序。
- **[02 可决定] argv-hash fixture**：先按固定规则将 credential/userinfo/token/password 参数值替换为 literal `<REDACTED>`，再对 argv array 使用上述 UTF-8 compact JSON canonicalization。输入 JSON 精确为 `["globus-url-copy","-p","1","<REDACTED>","file:///dst"]`，SHA-256 期望 `06352042ce06ddcbb47219a59ef0f08e7dc59eb393a00029a6d63a9f92af7b59`。同时要求含非 ASCII argument 的 UTF-8 golden fixture、参数顺序保持、redaction 前不得输出 secret；哈希对象是脱敏后的 argv，原命令仅由执行层短暂持有。
- **[02 可决定；timer 语义依赖 01]** 纯数值校验：success 且 logical bytes>0 时 elapsed 必须为有限 JSON number 且 `>0`；负数、`0`、NaN、±Infinity、数字字符串、null 均为 invalid/no goodput。零 payload/empty 或所有文件都未传输的 case 记 `timer_status=not_applicable`、无 goodput、performance_eligible=false，不以 0 秒制造样本。事件缺失/阶段名称/多 stream 聚合含义完全由 01 冻结；此向量不定义 timer start/end。
- **[依赖 04]** Python 回归需锁住以上 digest、UTF-8/non-ASCII canonicalization、secret redaction、空值与 non-finite 拒绝向量。实现后的明确纯 Python 入口候选为 `python -B -m unittest tools.experiments.gridftp_compare.test_gridftp_compare`；本轮未运行。

### 仍待角色交付与执行回执

- **[依赖 01]** 冻结 timer event pair/方向/确认/连接纳入方式、auth/data-TLS、GSI reverse 可比流映射、none/CRC requested/effective final-verify/fallback。v2 不替 01 决定，契约冲突时保留本回执并停止相应实现。
- **[依赖 03]** 提供 CPNetFlux file/tree upload/download 的 native timer/effective config、checksum coverage/flush/commit、compression/scheduler/worker counters 和覆盖率字段；不得以 runner 参数代替，也不由 02 伪造字段值。
- **[依赖 04]** 逐条独立复审 B1–B8、eligible/block analyzer policy、known vector 与 failure-injection 方案；当前没有任何实现或测试通过结论。
- **[依赖 05]** 指定持久证据 run root、archive/retrieval hash 流程、所有 mount resource gate、GridFTP server-side connection evidence source/window、PID/port 归属与失败后清理审批；上海 0 GiB 且授权清理 0，当前不得 build/run。
- **[依赖 00]** 04 复审和 01/03/05 输入满足后，另发仅含获准文件的实现任务。PERF-TOOL-PLAN-02 本身不允许实现或实验。
- 实际输入/输出 commit：输入和最终复核 HEAD 均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本节仅追加在工作树，未提交。
- 实际改动：仅 `docs/coordination/receipts/02-experiments.md` 新增本 v2 小节；保留既有 v1、并发文件和暂存区。
- 实际命令、退出码与证据：执行前 `git rev-parse HEAD`（0）、`git status --short --branch`（0）、`git diff --cached --name-only`（0，空）；定向读取本任务、v1 任务、PERF-TOOL-QA-01 v1、PLAN、BOARD 和回执（0）；PowerShell/.NET 只读计算 order-key/argv-hash fixture（0；结果见上）。追加后 `git rev-parse HEAD`、`git status --short --branch`、`git diff --check`、staged-path 检查、改动路径检查及 v2 字段/段落 `rg` 检查均退出码 0；HEAD 未变、暂存区为空，diff check 只有既有 LF→CRLF 提示。工作区 `git diff --name-only` 同时列出其他角色先前文档改动，本轮只编辑本回执。未运行 Python、CMake/CTest、preflight、SSH、build、transfer、case 或 cleanup。
- QA blocker 对应修订：B1 partial selection count；B2 逐类 stop policy；B3 wire 与 logical-goodput eligibility 解耦并禁 legacy fallback；B4 单 attempt/诊断 retry 不计 n；B5 requested/effective/observed compression 证明；B6 auth/data-channel/方向/expected streams 预先必填；B7 ledger cleanup 前 commit/re-read/hash；B8 golden serialization/timer numeric boundary。原始 QA 8 条全部有映射。
- 失败/阻塞及其原因：无本地文档门禁失败；外部 01/03/04/05 依赖未完成，上海资源受阻。这里是规格修订完成，不代表 QA 已批准实现或实验。
- 下一角色可直接执行的下一步：04 对本节 B1–B8 复审；若通过，由 00 依据 01/03/05 状态另发白名单 implementation task。具体测试门禁仅在其授权任务内执行。

### 验收与路线变化

- 修订沿用 `R2026-09-23.1`，没有改写 PERF-TOOL-PLAN-01 v1，也没有将任务或路线标为 superseded。后续若 01 decision 改变 timer/auth/verify，冲突范围停止并等待 00 更新规格；不得静默改变实验口径。

## PERF-TOOL-PLAN-03 v3：按 QA-02 收窄 B5–B8 工具规格

日期：2026-09-23（Asia/Shanghai）。路线/任务版本：`R2026-09-23.1 / v3`。本节只追加规格层修订；不改写 v1/v2，不表示代码、测试、构建或实验已通过。

### 范围、输入与 QA-02 结论

- **[02 可决定]** 实时 HEAD=`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，匹配任务输入；`docs/PLAN_100M_PERFORMANCE.md`、BOARD 仍为 `R2026-09-23.1`，PERF-TOOL-PLAN-03 未被 supersede。工作区已有共享文档 modified/untracked；全部保留。本节是本任务唯一编辑。
- **[02 可决定]** PERF-TOOL-QA-02 v1 对 B5、B6、B7、B8 均判 `partial`。本 v3 只闭合 02 可独立规定的 validator 输入、状态规则与纯规格测试向量；未满足外部字段/合同/持久化依赖时仍按 partial、unknown 或 blocked 处理，不宣称当前运行条件已通过。
- **[依赖 01/03/05]** 当前 PLAN 仍记 A 阶段因 01 active-writer 未交付；03 effective/native 字段映射另由 PERF-IMPL-CONTRACT-02 交付；05 没有批准清理，上海空间 0 GiB。不得据本规格执行实现、构建、运行或清理。

### B5：压缩观测适配层与机械判定

- **QA-02 B5（partial）原边界：** v2 的零计数判据可读，但没有 03 正式字段映射，也没有 upload/download、file/tree、worker 并发及失败路径覆盖定义；当前不能机械得到 `verified_off`。
- **[02 可决定] normalized validator input：** 在工具内部定义版本化、与 native 源字段名无关的 `CompressionObservationV1`。03 或其正式 adapter 将实现字段转换成该结构；validator 只消费此结构，不猜字段、不从日志自由文本/CLI argv/wire ratio 推断。最小字段为：
  - `adapter_contract_id`、`case_id`、`attempt_id`、`direction`、`transfer_shape`（file/tree）、`outcome_scope`、原始 artifact references（path/id、byte size、SHA-256、source component）；
  - `requested`：`compression`、`scheduler`；`effective_by_participant`：每个 participant id 对应 compression/scheduler 的规范值、source ref 与观测状态；
  - `participants_expected` 与 `participants_observed` 的完整集合及差集；expected 集合必须来自启动/服务/worker roster 或冻结后的 route inventory，不能从观测到的记录反推，以免漏掉未报告 worker；
  - `coverage`：预期/观测 case、attempt、file id、participant id、计数器记录数、生命周期阶段与未覆盖原因；
  - `counters_by_participant`：规范键 `sampling_attempts`、`compression_attempts`、`compression_dispatches`、`compressed_frames`、`compressed_payload_bytes`。每键保存 `present`、整数值或 null、单位、scope、source ref；原始实现字段名只放在 adapter mapping/version 中，不作为 validator contract。
- **[02 可决定] 机械状态函数：** `verified_off` 当且仅当 requested compression/scheduler 都为 off；参与者 expected 集合非空且 observed 集合与 expected 完全相等；每个适用 participant 的 effective compression/scheduler 都有可解析值且为 off；本 case/attempt 的 file、direction、transfer shape、相关生命周期覆盖完整且无 gap；五种 counter 对每个 participant 均存在、是非负整数且均为 0；所有 raw artifacts/index refs 可读且 SHA-256 匹配。任何字段缺失、null/负数/非整数计数、participant/file/path coverage 不完整、未知 effective 值、adapter 未登记或 evidence ref 失效，状态只能是 `unknown`（已有明证矛盾可为 `effective_mismatch`/`observed_active`），case `performance_eligible=false`。不得将缺测按 0 处理。
- **[02 可决定] coverage 范围：** 逐 case 证明只覆盖该 case 实际运行的方向、file/tree 类型、files、参与 client/server/worker/scheduler participant 和整个该次 attempt 生命周期；tree case 要覆盖 manifest 中每个预期文件及其 worker 归属，不允许只报告总计却不提供 participant/file 覆盖。失败/中断 attempt 的已有记录仍须覆盖至其 terminal/stop event；未执行路径记 not-applicable 并说明，不可借此声称其它路径的 off 已验收。全产品路径的 `scheduler=off/global fixed/global adaptive`、resume/retry、故障恢复覆盖是单独的资格验证 suite，不由 scheduler=off 性能 case 外推。
- **[依赖 03/04]** 03 需交付 raw→normalized 的版本化映射、expected participant 来源、字段单位和 file/worker coverage 能力，覆盖 upload/download × file/tree 以及适用并发/失败路径；若 03 尚未交付，adapter 缺失就使所有 CPNetFlux compression verdict 为 `unknown`、性能样本 ineligible。04 需复核 adapter 映射、全覆盖和机械状态函数；这些目前未测试。

### B6：GridFTP match validator 规则

- **QA-02 B6（partial）原边界：** requested auth/data-channel/direction/expected streams 已要求预填，但没有实际证据引用和 validator 比较条件；01/03/05 尚未提供有效映射、服务端来源/窗口，不能标 `matched`。
- **[02 可决定] pre-run manifest 必填：** 每个 GridFTP arm 必须在不可变 case manifest 中有非空 `direction`、`auth_profile_id`、`requested_auth_mode`、`data_channel_profile_id`、`requested_data_channel_mode`、`mapping_contract_id`、`mapping_contract_version`、`expected_gridftp_mapping`（工具、传输策略、parallelism 参数映射）、`expected_file_parallelism`、`expected_per_file_connections`、`expected_streams_per_file`、`expected_total_streams`、`stream_count_method_id`。单文件也显式写 `file_parallelism=1`。字段缺失、null、占位字符串或 expected 数不与 manifest tuple 一致时，在 side effect 前拒绝为 `manifest_invalid`；contract id/version 有记录但未获 01 冻结或 04 接受时标 `blocked_contract_unfrozen`，不得开始 case；不得等命令结束再补字段。
- **[02 可决定] actual evidence reference：** case 结果必须引用并保存 artifact id/path、size、SHA-256、producer/version、case/block/attempt id、host、UTC/monotonic 观测窗口：脱敏实际 argv/command audit 与工具版本；认证结果和 data-channel negotiation/安全属性；client socket stream sample；server-side connection/stream log 或适配后的观察摘要；control/data port 与服务 PID/归属。凭据不得进入 argv evidence，hash 前先 redaction。Evidence reference 的存在不代表其语义可比，需由 validator 实际解析并核对对应 case/window/hash。
- **[02 可决定] match 状态：** `config_match_status=matched` 仅在 `mapping_contract_id/version` 已由 01 冻结且 04 接受、manifest requested fields 完整、required evidence references 完整/hash 正确、observed auth/data-channel/方向符合该 contract，且 observed per-file/total streams 与 contract 预期映射一致时允许。reference 缺失、窗口不覆盖 transfer、观测无法关联 case、01 mapping 未冻结或 03/05 未提供 server-side/effective evidence 时为 `unknown` 并附原因，performance ineligible；观测已明确与冻结 mapping/expected streams 不符则为 `unmatched_config` 并 fail-fast；认证/服务启动不可用则按原因 `blocked_auth`/`blocked_external_gridftp`。validator 不能自行把不同 auth/TLS 级别、`-p/-cc` 数值或 segmented partial GET 解释为等价流。
- **[依赖 01/03/04/05]** 01 决定 auth/data-TLS/GSI 双向流语义及可比映射版本；03 确认客户端/协议侧实际配置可观测字段；05 冻结服务端连接 evidence source、端口/PID 归属与采样窗口；04 验收 validator 只在有冻结 contract 和真实证据时输出 matched。未完成前所有相关 GridFTP cell 均不得由该规格声称 matched 或可运行。

### B7：ledger 逻辑完整性、evidence index 与 durability 分层

- **QA-02 B7（partial）原边界：** v2 的 hash-chain/fsync/readback/cleanup 顺序清楚，但真实 run root/archive durability 未由 05 指定，杀进程/掉电/损坏恢复未由 04 验收；逻辑 hash 正确不能证明介质耐久。
- **[02 可决定] 三层状态分离：**
  - `ledger_structure_status`：`valid|invalid|incomplete`，只判断 schema、seq 连续唯一、状态迁移合法、`prev_event_sha256/event_sha256` hash chain 和 manifest hash 是否一致；
  - `evidence_index_status`：`complete|partial|invalid`，只判断 ledger 引用的 case/run evidence index 是否存在、引用 artifacts 的路径、size、SHA-256、required/optional 与缺失原因是否可逐项回读核对；
  - `storage_durability_status`：`verified|unverified|failed`，只由 05 批准的持久化位置及 durability/retrieval 核验证据设置，02 的 fsync、当前进程读回或同一 OS cache 内 hash 不能将它升级为 verified。
- **[02 可决定] 状态迁移及引用一致性：** run manifest hash 固定；合法逻辑序列为 run_created→preflight_terminal→case_started→case_terminal→case_evidence_committed(cleanup_pending=true)→cleanup_terminal→下一 case，或按 v2 stop 状态写 stop_event/run_terminal。case evidence event 必须引用精确 `case_id/attempt_id/case_manifest_sha256/evidence_index_sha256/result_sha256`；cleanup terminal 必须引用同一 case/attempt 和前序 commit seq；summary 引用最后 ledger SHA。重复 terminal、跳 seq、case/attempt 不匹配、引用 hash 错误、cleanup 无已提交 case evidence、未知状态迁移均为 invalid，不可依赖 summary CSV 修补。
- **[02 可决定] cleanup/run-evidence gate：** `run_evidence_gate=complete` 当且仅当 ledger structure valid、required evidence index complete 且所有 artifact hash 回读相等、`storage_durability_status=verified`，并由 05 的批准规则验证该 run root/回收副本。只有该 gate complete 且当前 case cleanup 所有权范围核对通过，才可 cleanup regenerable source/destination payload；否则 gate 是 blocked/unverified，严禁删除 payload。持久性失败为 failed；尚无 05 证据则 unverified，二者都不准称 evidence durable/pass。
- **[依赖 05]** 05 必须指定允许的 run root/挂载点、实际 archive/retrieval 或等价持久化验证方式、回收 SHA-256 对账和环境预算，并提供 root identity 与核验 artifact。没有这些真实 evidence 时只记录逻辑 `valid`/索引 `complete`（若可证明），durability=`unverified`、run evidence gate blocked，`evidence_status` 不得为 complete、`performance_eligible=false`，不 cleanup、不将结果写成完整性能结论。
- **[依赖 04]** 04 需执行 sequence/hash tamper、manifest hash mismatch、case-index dangling/错误 SHA、cleanup 中断、ledger append/fsync/readback 失败、summary 从 ledger 重建和进程中断恢复测试；这些都不在本次执行。

### B8：规范化已知向量与需要 QA 执行的断言

- **[02 可决定] canonical bytes**：UTF-8 无 BOM；JSON object key 按 Unicode code point 升序；紧凑分隔符 `,`/`:`；数组顺序保持；Unicode 原样 UTF-8（不转 `\\uXXXX`）；拒绝 NaN/±Infinity；文件行尾恰一个 LF，event hash 不包含自身 `event_sha256` 字段。
- **[02 可决定] order key 已知向量**：UTF-8 输入字段 `20260923`、`screen-single-l2r-fp1-c1-r1`、`cpnetflux-none` 以单字节 `0x00` 连接（完整原始串为 `20260923\0screen-single-l2r-fp1-c1-r1\0cpnetflux-none`）；SHA-256=`63f64f123bb5f10461f3675cb8863ab195de09be1054562edf2a0533d187c990`。
- **[02 可决定] non-ASCII argv 向量**：canonical argv JSON 精确为 `["globus-url-copy","-dest","数据/测点.bin"]`，UTF-8 SHA-256=`babcf714d8a8b3a81ac6c2dd12c4204e1cf36c1a3de4a1c0095692a37ac8003c`。必须验证非 ASCII 字符未经 ASCII escape 且 UTF-8 byte hash 相同。
- **[02 可决定] secret redaction 向量**：原始测试 argv `["globus-url-copy","--access-token","test-secret-123","file:///dst"]` 进入任何日志/ledger 前按 token option 规则转换为 canonical JSON `["globus-url-copy","--access-token","<REDACTED>","file:///dst"]`，SHA-256=`85f1558763bd9a9ef131858c8f7f70b40e0bcab800e4c03d0b67a195eb1e528e`；assert secret 字节既不出现在 argv evidence，也不出现在 stdout/stderr/ledger。canonicalization 后改变 argv 参数顺序应改变 digest；不得先 hash/log 原始 secret 再标记 redact。
- **[02 可决定] manifest corruption 向量**：canonical manifest bytes `{"route_version":"R2026-09-23.1","schema_version":1}` 的 SHA-256=`586489807f37deb641de8188315ca679da6735aeccaf7f35bcf0f8a835cd8228`。运行前单字节改写 route_version 而保留声明 hash，validator 必须在任何数据生成/网络副作用前拒绝 `manifest_hash_mismatch`。
- **[02 可决定] ledger chain 向量**：在同一 manifest hash 下，event 1 canonical bytes 为 `{"batch_status":"RUNNING","event_type":"run_started","manifest_sha256":"586489807f37deb641de8188315ca679da6735aeccaf7f35bcf0f8a835cd8228","payload":{},"prev_event_sha256":null,"run_id":"r1","schema_version":1,"seq":1}`，event hash=`62ffd72b138318eedfefa1370ee503ac9bae1959077548f127726e2f3c569166`。event 2 canonical bytes 为 `{"batch_status":"RUNNING","event_type":"case_started","manifest_sha256":"586489807f37deb641de8188315ca679da6735aeccaf7f35bcf0f8a835cd8228","payload":{"case_id":"c1"},"prev_event_sha256":"62ffd72b138318eedfefa1370ee503ac9bae1959077548f127726e2f3c569166","run_id":"r1","schema_version":1,"seq":2}`，event hash=`7da800efb8159be1396513c0526bae2033ec8c07c988bc785c03ceca3d6f6ff7`。必须验证该两行通过；event 2 payload 单字节改变但保留旧 hash 时在 seq 2 拒绝；event 2 自 hash 重算但 prev 指向非 event 1 hash 时仍因 chain mismatch 拒绝；删除/重复 seq 同样拒绝。
- **[02 可决定；timer 事件语义依赖 01] numeric boundary**：非空 payload 且 timer contract 事件齐全时，JSON number `1.25`/`1` 是有效的 finite positive elapsed；`0`、`-0.0`、负数、NaN、±Infinity、数字字符串、空字符串和 null 都是 invalid/no goodput。`logical_payload_bytes=0` 的 empty/no-transfer case 无论 elapsed 为 null 还是 0 均是 `not_applicable`、goodput=null、eligible=false，不得做除零或以 0 秒计样本。JSON decoder/schema 必须先拒绝非标准 NaN/Infinity。此向量只校验数值和 eligibility，不定义 01 timer event 名称或起止边界。
- **[02 可决定；依赖 04] 必需纯函数/故障注入断言**：锁定上列 manifest/order/argv/ledger golden digests；UTF-8/non-ASCII canonical bytes、JSON key 排序、array order 保持、redaction-before-log/hash、修改一个 manifest byte 的 pre-side-effect rejection；event hash 自身 tamper、prev hash tamper、seq gap/duplicate、dangling evidence-index ref/错误 artifact SHA 的拒绝；正数/零/负/null/string/NaN/Infinity 计时分类；case_evidence_committed 之后、cleanup_terminal 之前模拟进程中断时结果为 `INTERRUPTED_NEEDS_RECONCILIATION`，不得假定 cleanup 成功、自动 resume/retry 或删除 residual payload；cleanup nonzero/timeout 后保持 stop、证据索引保留、pending cases not-started。真实 storage durability/retrieval test 由 05 提供运行环境、04 独立判断，不以 mock 代替。
- **[依赖 04]** 上述是 QA 后续应执行的断言清单，不是已经存在/通过的测试。待获准实现任务后，由 04 复审测试和实现；本轮不运行 Python、CMake/CTest 或故障注入。

### 依赖边界与执行回执

- **[依赖 01]** 继续冻结 timer event pair、auth/data-channel/GSI 方向等价、effective verify/fallback；v3 只要求 presence/ID/version 和拒绝未知 mapping，不定义其协议/计量含义。
- **[依赖 03]** 提供 B5 normalized compression observations 的正式 raw→normalized adapter、participants roster、计数单位和 file/worker coverage；提供 B6 当前 client/native effective config evidence 字段。缺少时 validator 必须 `unknown/ineligible`，不捏造任何字段和值。
- **[依赖 05]** 提供 B6 服务端实际连接证据来源/窗口、B7 被批准的 durable run root/archive/retrieval 验收和挂载点预算；没有真实 durability evidence，`run_evidence_gate` 不通过、禁止 cleanup。上海当前 0 GiB 可用且清理批准为 0。
- **[依赖 04]** 逐项复审 QA-02 B5–B8 修订；执行上列纯函数、失败注入和恢复断言的授权测试任务。当前不声称测试通过。
- **实际输入/输出 commit：** 输入与最终复核 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；只追加本节，未提交。
- **实际改动：** 仅追加 `docs/coordination/receipts/02-experiments.md` 中 PERF-TOOL-PLAN-03 v3；没有修改其他文件、代码或共享 index。
- **实际命令与退出码：** 执行前 `git rev-parse HEAD`、`git status --short --branch`、`git diff --cached --name-only` 均 0；读取本任务、QA-02、v2、PLAN、BOARD 和 01/03/05 现有回执的定向 `Get-Content`/`rg` 均 0；PowerShell/.NET 只读生成本文 non-ASCII argv、redacted argv、manifest、两条 ledger event 的 SHA-256 golden values 为 0。追加后 `git diff --check`、`git rev-parse HEAD`、`git status --short --branch`、暂存区路径、改动路径和 v3 段落/向量 `rg` 均退出码 0；HEAD 未变、暂存区为空，diff check 仅有 LF→CRLF 提示。工作区 diff 仍包含其他角色已有文档改动；本轮仅编辑本回执。未运行 Python/项目测试、CMake/CTest、preflight、SSH、构建、实验、传输或清理。
- **QA-02 partial 对应修订：** B5 固定字段名无关的 normalized adapter schema、coverage 集合和 `verified_off` 机械谓词；B6 固定 manifest 必填、evidence ref 验证与 matched/unknown/unmatched/blocked 分支，但保留 01/03/05 语义依赖；B7 将 ledger chain、evidence-index completeness、05 storage durability 分层，未验证 durability 则 block cleanup/证据通过；B8 新增 UTF-8/redaction/order/manifest/ledger 具体 digest 和 corruption/interruption/timer boundary 断言清单。
- **失败/阻塞：** 本地规格记录不替代 03 adapter、01 mapping/timer、05 durable root，也不替 04 执行测试；相关状态继续 partial/unknown/blocked。当前没有 QA-02 复审结论。
- **下一步：** 04 独立复审 v3 B5–B8；有冲突则保留历史章节并按 01/03/05 决策停止相关实现，不在本任务内继续扩展。

### 验收与路线变化

- 沿用 `R2026-09-23.1`，仅为 QA-02 的有限规格修订；未声明 B5–B8 质量 gate 已通过，未批准实现或实验。若路线/HEAD 后续变化，停止依赖本节的执行并由 00 更新任务口径。
