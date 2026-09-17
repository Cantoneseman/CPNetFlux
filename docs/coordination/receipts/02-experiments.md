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
