# 03 核心实现：首次接手回执

日期：2026-09-17（会话日期）。角色：03 核心实现；交付对象：00 总指挥。
状态：初始化接手，等待总指挥任务单；IMPL-01 未启动。本回执是源码定位与待确认事项，不是实现、测试通过或性能结论。

## 本次范围、非目标与验收

- 范围：核对 Git HEAD/status；阅读指定协作资料；静态定位目录阶段 instrumentation 的源码、指标出口与测试入口；登记建议边界及规格缺口。
- 唯一写入文件：`docs/coordination/receipts/03-implementation.md`。接手时该文件不存在。
- 非目标：不改源码、测试、决策、BOARD/ROSTER；不建 worktree/构建目录；不运行 CMake/CTest、传输 smoke 或性能实验；不 SSH、不清理云端、不复制凭据；不 git add/commit。
- 验收：回执包含输入提交、模块映射、建议修改边界、schema/时钟事件待确认项、测试缺口和交接；引用路径存在、UTF-8 可回读、无尾随空白；复核 HEAD/status、源码工作树差异及暂存状态。文档门禁不代表真实传输验证。

## 输入与已读资料

- 仓库：`D:\Project\CPNetFlux`。
- 初始及写入前 HEAD：`51820e7aed918fd9e6840ef834acd1705c3848de`。
- 初始分支/status：`## main...legacy-reference/main [ahead 10]`，唯一已有修改为 ` M docs/coordination/ROSTER.md`；该修改不属于本角色。
- `git diff --name-only 3b0820d HEAD` 的输出仅含 Markdown 文件；没有源码/测试差异。因此本次源码定位对应最近实现 `3b0820d`，不能以接手说明中的旧 HEAD `6dad8bf` 代替本次输入。
- 已读：根 AGENTS.md、README.md；coordination 的 START_HERE、PROJECT_BRIEF、ENVIRONMENT、BOARD、prompts/03-implementation、receipts/README；DESIGN、ENGINEERING、DIRECTORY_TRANSFER、RESEARCH_BASELINE；2026-09-17 的 route-reset 和 directory-data-plane-profiling 两份决策。源码按下表定向读取，未全读巨大历史状态或旧备份。
- 文档给出的历史边界：全套 CTest 没有全绿证据；token-auth/event-log smoke 缺测试 token；io_uring 有环境限制；压缩 off 全热路径未经全面证明；旧云端 dirty 实验和目录汇总差距不能证明瓶颈因果或生产 readiness。本次未重跑这些验证。
- 环境只采用已读快照：上海磁盘满、深圳约 52G 可用均未在本次重查；以后运行前由运维核实。两个旧 GridFlux-Beta 工作树及受保护 CPSS(DCC) 目录均不触碰。

## 模块映射（行号以输入 HEAD 为准）

| 阶段/职责 | 源码入口 | 静态核实与未来落点 |
| --- | --- | --- |
| tree 上传/下载、scan_plan | `src/core/io/tree_transfer_client.cpp:2379`、`src/core/io/tree_transfer_client.cpp:2433`；`src/core/tree/tree_scan.cpp` | runTreeUploadClient/runTreeDownloadClient 启动 steady_clock；上传本地扫描或加载 manifest，下载还有控制登录和 NLST/SIZE/MDTM 远端枚举。需明确这些控制操作属于 run planning 的哪部分。 |
| 控制建立与复用 | `src/core/io/tree_transfer_client.cpp:53`、`src/core/io/tree_transfer_client.cpp:475`、`src/core/io/tree_transfer_client.cpp:1151` | ControlClient、ensureControlReady、controlForFile；含 DNS/TCP、可选 TLS、greeting/login、parallelism 设置及 worker 命中分支。现有统计是连接次数，没有阶段区间。 |
| 逐文件控制/状态 | `src/core/io/tree_transfer_client.cpp:1307`、`src/core/io/tree_transfer_client.cpp:1479`、`src/core/io/tree_transfer_client.cpp:195` | processUploadFile/processDownloadFile 负责元数据检查、EPSV、REST、STOR/RETR、调用单文件引擎，再 waitTransferComplete 读取控制 226。成功/失败/跳过出口均需被规格覆盖。 |
| 上传数据连接、payload、COMPLETE | `src/core/io/file_transfer_client.cpp:390`、`src/core/io/file_transfer_client.cpp:229`、`src/core/io/file_transfer_client.cpp:368` | sendStream 连接后进行 SessionInit/ResumeResponse；sendChunkRange 分开发 header/payload；waitForFinalStatus 等待数据 COMPLETE。仅在 tree 外层包围 runFileTransferClient 无法拆出这些阶段。 |
| 下载数据连接、payload、提交 | `src/core/io/file_download_client.cpp:374`、`src/core/io/file_download_client.cpp:632`；`src/core/io/framed_data_socket.cpp` | receiveStream 建连、会话握手、接收 DATA 并校验/写入；流结束发送 COMPLETE，runFileDownloadClient 汇合后执行最终校验/rename/commit。下载端 COMPLETE 和文件落盘提交不是同一事件。 |
| 服务端对应路径 | `src/core/io/file_transfer_server.cpp:1148`、`src/core/io/file_download_sender.cpp:325`；`src/protocol/control/control_server.cpp` | STOR 接收/校验/提交/回 COMPLETE，RETR 发送并等接收端 COMPLETE，控制层给最终回复。需服务端细分时另明确白名单与证据关联方式；不能用客户端等待时间直接冒充服务端各阶段。 |
| manifest/finalize | `src/core/io/tree_transfer_client.cpp:470`、`src/core/io/tree_transfer_client.cpp:1068`、`src/core/io/tree_transfer_client.cpp:1081`；`src/core/tree/tree_manifest.cpp`；`src/core/session/transfer_session.cpp:214`、`src/core/session/download_session.cpp:191` | tree 状态更新持 mutex 保存 manifest；单文件 session 已有 ManifestFlush 操作计时。flush 横跨传输中及结尾，下载另设 mtime，不能把所有 manifest 活动视作单一尾部区间。 |
| 现有指标及汇总 | `include/cpnetflux/core/metrics/transfer_phase_stats.h`、`src/core/metrics/transfer_phase_stats.cpp`；`include/cpnetflux/core/io/transfer_runtime_metrics.h`；`src/core/io/tree_transfer_client.cpp:636`、`src/core/io/tree_transfer_client.cpp:907` | TransferPhaseStats 是原子累计操作时间/字节，含 recv/send/read/write/checksum/flush/precheck/verify/rename/overall；runtime metrics 无首末 payload 事件，也没有所需逐文件阶段分布。 |
| event log 与客户端 hash | `include/cpnetflux/core/metrics/event_log.h`、`src/core/metrics/event_log.cpp:14`；`src/core/io/tree_transfer_client.cpp:890`、`src/core/io/tree_transfer_client.cpp:757` | JSONL timestamp 用 system_clock；tree 事件目前 transferId 为空、elapsedSeconds 为 0。writeTreeJsonSummary 冻结 elapsed 后自行 computeTreeVerificationHash；这不是 runner 的独立 tree_verify。 |
| 实验侧计时/schema | `tools/experiments/gridftp_compare/runner.py:1913`、`tools/experiments/gridftp_compare/schemas.py`；`tools/test/tree_smoke_common.py:36` | runner 有独立 tree hash、命令审计及结果分列；该 CPNetFlux case 的 elapsed 在 hash、服务收尾及日志回收后取值，不能直接等同客户端 transfer wall。 |

## 建议修改边界（仅供任务单选择，不构成授权）

1. 最小实现候选集中在 tree 编排、file_transfer_client/file_download_client、独立 metrics 数据结构/汇总，以及必要的配置头和 CMake 测试注册。沿现有内部参数返回观测数据，不改 wire frame 或 manifest 格式。
2. 观测需同时覆盖 scheduler off、worker reuse、上传/下载和多连接，不依赖 global scheduler 专有分支。当前普通 runTreeScheduler 传入非空 runtime，而 transferMetrics 的回填有 global 条件，现有 run stats 合并也不等于完整阶段汇总；新设计须显式确认汇聚位置。
3. 客户端观测无法分离服务端内部 flush/verify/rename；如规格要求两端，任务单应明确增加 file_transfer_server/file_download_sender/control_server 的读取或修改边界。不得通过新增协议消息补测量。
4. 新 profiling 输出的写失败、缺记录和 partial evidence 应按确认后的证据契约处理。现有 emitTreeSummary 会返回 JSON 写入错误，不能无意扩大成修改既有 summary 错误语义；若要调整须单列授权。
5. 实验 runner/schema、QA 脚本与测试登记需总指挥指明责任及允许路径；不顺带改 scheduler/compression 策略、连接数默认值、IO backend、checksum、resume/重试或清理策略。运行时凭据不入记录。
6. 后续代码任务须固定输入 commit、任务 ID 和路径白名单，创建 `codex/<task-id>` 独立 worktree；质量角色审查后由总指挥整合。本次初始化不创建 worktree。

## 需要 01 架构与 04 质量确认的 schema/时钟事件

以下均为待决问题，不是已经采纳的 schema。

- 输出载体、schema_version、字段名/类型/秒单位/精度、兼容旧 summary 的方式；run/file/attempt/stream/worker 标识、相对路径与 transfer_id 关联、partial/failure/skipped 状态。重试与 resume 是否分 attempt，不能把同一文件不同尝试混为一条。
- 同进程持续时间用 steady_clock；UTC timestamp 只作事件关联，不用两台主机的 steady_clock 点相减。两端独立 duration 如何关联、是否只验客户端边界，需明确。
- control_acquire 的起止：是否包含认证、greeting、首次 OPTS；复用命中与重连如何记。control_prepare 如何容纳分散的 SIZE/MDTM/EPSV/REST/STOR/RETR 操作，及 resume 预检、队列等待归属。
- data_connect 的 ready 事件：TCP/TLS 完成，还是 SessionInit/ResumeResponse 完成；后者涉及 manifest 加载/恢复校验。多连接记录 per-stream 后如何聚合，不能用“最后一条连接 ready”去扣“最早一个 payload”而制造负时间。
- first_payload / payload_io：首字节、完整 DATA payload 接收/发送成功，还是校验/写入完成？上/下载须对称且可观测。应分别验证非负性与选定事件顺序；不存在通用 `first_payload <= payload_io` 持续时间不等式。单 payload、空文件、已完整 resume 的无 payload 情况需区分零值与不适用。
- transfer_complete_wait：最后 payload 到数据 COMPLETE、控制 226、还是本地最终提交？上传服务端 finalize 和下载本地 finalize 的位置不同；只包 waitTransferComplete 调用会漏掉单文件函数内部的等待。
- manifest_finalize：累计 flush/锁等待/持久化时间与尾部 finalize 区间是否分开；与 payload_io、complete_wait 的嵌套是否保留。不可将并发 worker/stream 或嵌套阶段相加当 run wall。
- run wall 的冻结位置、与现有 stdout/JSON 和应用事件总耗时的容差；客户端 summary 自算 hash、runner 独立 tree_verify、收尾/日志收集分别计时。logical bytes、实际本次传输 bytes、resume skipped 和吞吐分母保持清楚。
- run summary 的 sum/median/p95/max、分位数算法、文件计数/缺失计数、成功/失败/跳过统计集合；“至少 90% 成功文件完整”分母及空阶段例外须明确。记录缺失只能影响 evidence，不能伪造 0 或改变 transfer/integrity 结果。
- 观测开启方式、关闭时开销、并发写入策略及可接受开销/计时误差；凭据不写日志。GridFTP 只保留端到端参考，不制造同名内部阶段。

发现的文档/代码口径差异：路线文字称 worker 为默认策略，但 `include/cpnetflux/config/tree_transfer_options.h:47` 当前默认 `ControlReuseMode::Off`，`tests/unit/tree_transfer_options_test.cpp:107` 也断言 off。后续基线应显式指定 `--control-reuse worker`；是否改变默认值另立决策，不能夹带在 instrumentation 中。profiling 的 scheduler off 与 route-reset 的三策略矩阵也仍需由任务单消歧。

## 测试入口与缺口

- 单元注册：`CMakeLists.txt:287` 的 cpnetflux_unit_tests / gtest_discover_tests；相关现有入口为 `tests/unit/event_log_test.cpp`、`tests/unit/tree_transfer_options_test.cpp`、`tests/unit/tree_manifest_test.cpp`、`tests/unit/transfer_session_test.cpp`、`tests/unit/download_session_test.cpp`。现有操作计时不能替代阶段边界、分位数、缺记录和并发重叠测试。
- 真实链路候选：CTest 的 cpnetflux_tree_upload_smoke、download_smoke、parallel_smoke、control_reuse_smoke、resume_smoke、changed_file_smoke、edge_cases_smoke、manifest_corrupt_smoke，以及单文件 STOR/RETR/resume。脚本在 tools/test，对应注册见 `CMakeLists.txt:388`。已读 control_reuse smoke 只对上传 off/worker 验证连接计数与独立 hash；后续应补足下载阶段契约验证。
- 新阶段测试至少覆盖空文件/无 payload、单 payload、多文件并发与多连接、worker 首次建立/复用、resume 完整跳过和部分补传、建连/控制/数据/落盘失败、事件缺失、观测输出错误；应核实启用观测后原始结果及独立 hash 不变。mock/parser 只能证明局部契约，不能证明真实传输链路。
- `tools/test/run_gridftp_tree_data_final_status_timeout_smoke.py` 存在，但 CMakeLists 没有注册它；脚本向 tree CLI 传递 --data-final-status-timeout-seconds 并期待 recovery 字段/事件，定向搜索仅在单文件参数解析发现此选项，未在 tree 路径找到对应字段/事件。它只能作为待 QA 校核的入口，不能直接算可用或已通过的回归门禁；本次不修复。
- 阶段 0 Python 入口：`tools/experiments/gridftp_compare/test_gridftp_compare.py` 和 `tools/experiments/beta_matrix/test_beta_matrix.py`；包括分类/schema/参数传播证据，但不证明 compression off 全部热路径。
- `tools/test/run_gridftp_control_token_smoke.py:24` 在环境变量 CPNETFLUX_TEST_TOKEN 缺失时直接报错，event-log smoke 导入该模块；后续凭据须受控注入。`tools/test/run_gridftp_tree_scheduler_smoke.py:183` 上传显式 compression auto，不能当 off 基线验收。
- 本次 CMake/CTest、上述 Python 测试和 smoke 全部 NOT_RUN；原因是首次接手只读定位且仅写回执。没有新通过/失败/跳过的传输证据，也未验证本地完整 Linux 工具链。

后续可交给质量/运维的候选命令（本次未执行；须用任务单登记的隔离固定提交 build-dir）：

```text
cmake --build <approved-build-dir> --parallel <approved-jobs>
ctest --test-dir <approved-build-dir> --output-on-failure
python -B -m unittest tools.experiments.gridftp_compare.test_gridftp_compare tools.experiments.beta_matrix.test_beta_matrix
```

完整构建配置、目标二进制、CTest 范围、token 注入、依赖/跳过判定及新增 profiling gate 名称由任务单确认；记录真实命令、退出码和证据路径，不能将未运行写成通过。

## 本次命令与证据

- `git rev-parse HEAD`、`git status --short --branch`：读取成功，退出码 0，输出见“输入”及末尾复核记录。
- `git diff --name-only 3b0820d HEAD`：退出码 0，仅 Markdown 差异。
- 用 rg 检索并用 Python UTF-8 定向读取上述文档/源码/脚本；最初 PowerShell 中文显示有编码问题，已用 Python UTF-8 重读。一次宽泛定位引用了不存在的 src/transport、file_sender.cpp、file_receiver.cpp，rg 报路径不存在（命令组退出码 1）；随后依据 rg --files 转到实际的 file_download_sender/file_transfer_server。该情况是定位命令错误，不是测试失败。
- 产物仅为本回执；无构建、测试日志、云端证据或代码提交。文档门禁与最终 Git 复核结果由写入脚本在下方记录。

## 下一步交接

请 00 总指挥先取得 SPEC-01 的字段/事件/统计契约及质量审查，再派发 IMPL-01：任务 ID、固定输入 commit、允许修改路径、非目标、完整验收命令、观测开销门槛、隔离 Linux 构建和证据位置。环境快照过期及 token/liburing 限制交由 05 运维重新核实。

本角色当前等待任务单，不自动执行 BOARD 的其他任务。回执待总指挥读取/整合；本会话未向其他长期聊天派发消息，不声称已唤醒或已完成质量审查。

## 写入后脚本门禁与最终 Git 复核

- 文档脚本门禁：8 项必要章节、32 个引用路径、UTF-8 完整回读及尾随空白检查通过。
- git diff --check：退出码 0。
- git diff --exit-code -- src include tests tools CMakeLists.txt：退出码 0，无源码/测试/工具/CMake 工作树差异。
- git diff --cached --name-only：退出码 0，输出为空，未暂存。
- git rev-parse HEAD：退出码 0；51820e7aed918fd9e6840ef834acd1705c3848de，与输入一致。
- git status --short --branch：退出码 0，输出如下；ROSTER 属接手前已有修改。

```text
## main...legacy-reference/main [ahead 10]
 M docs/coordination/ROSTER.md
 M docs/coordination/START_HERE.md
 M docs/coordination/prompts/00-commander.md
?? docs/coordination/DISPATCH.md
?? docs/coordination/receipts/03-implementation.md
?? docs/coordination/receipts/04-quality.md
```

上述仅为本回执与仓库状态检查，不是 CMake/CTest 或真实传输验收。

## PERF-IMPL-PLAN-01 v1：单文件与目录优化实现拆解

日期：2026-09-23（Asia/Shanghai）。状态：只读方案交付，等待 00 验收；未开始代码实现。路线/任务版本 `R2026-09-23.1 / v1`。

### 输入、范围与证据等级

- 输入 HEAD 实查：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致。当前 `main...legacy-reference/main [ahead 11]`；已有并发修改为 `BOARD.md`、02/04/05 回执，新增多个任务单未跟踪。均未覆盖、暂存或修改。
- 只定向读取本任务、BOARD、02 的 PERF-MATRIX-01、04 的 PERF-QA-01、05 的 ENV-01、RESEARCH_BASELINE、既有 03 映射及相关单文件/tree 客户端、指标结构和 CMake 测试登记。
- 证据等级：**A（当前代码事实）**只说明已有实现/指标，不证明耗时重要；**B（历史间接证据）**只支持机制曾有收益，不能充当当前固定提交的 matched baseline；**C（待测假设）**无当前可比传输测量。此次没有任何候选达到实测因果或瓶颈证据等级。
- 本回执仅追加本节；不改源码、测试、runner、决策、任务单、BOARD/ROSTER，不创建 worktree，不运行 build/CTest/实验/SSH，不操作共享 index。

### 观测现状与缺口

| 范围 | 当前能力（A：源码核实） | 对性能归因的缺口 |
| --- | --- | --- |
| 单文件 STOR | `src/core/io/file_transfer_client.cpp:492` 在文件打开、大小/chunk 计划和 IO 校验后、启动 stream 前开始计时；elapsed 到各 stream 收到数据面 COMPLETE 后结束。`TransferRuntimeMetrics` 返回 elapsed、logical/wire bytes，以及 send/recv/read/write/checksum 累计秒数。 | 没有独立 connect/TLS、SessionInit、首/末有效 DATA、control EPSV/STOR 和 226 边界。多 stream 操作秒数为重叠累计，不能相加成 wall time；文件打开和部分准备在 elapsed 外。tree 调用者在此后才等 control 226，当前 file elapsed 不含该等待。 |
| 单文件 RETR | `src/core/io/file_download_client.cpp:632` 有相同操作类 phase 累计；输出请求及 effective final verify、manifest flush、rename/commit 信息。 | elapsed 在 stream 汇合、final verify、输出替换、rename、sync 和 commit 后结束，边界与 STOR 及外部进程计时不同。无连接/首 payload 区间。 |
| 目录 run | `src/core/io/tree_transfer_client.cpp:2379` / `:2433` 使用 steady_clock；可输出 run elapsed、logical/transferred bytes、连接和文件计数、状态与 JSON summary。下载起点在控制登录和远端 NLST/SIZE/MDTM 之前。 | 无逐文件阶段分布；run timer 混有 scan/manifest/远端枚举/调度，含义不同于 file client elapsed。JSON 内部会计算 tree hash，但其 elapsed 先冻结；不是 runner 的独立 tree_verify。普通 tree scheduler 路径未把 file runtime metrics 汇总为所需 per-file 记录；global scheduler 的 runtime metrics 用途也不是通用 schema。 |
| 目录复用/状态 | `controlForFile`/`ensureControlReadyWithStats` 有 worker 连接复用和连接计数；文件状态更新经 mutex 保存 tree manifest。 | 命中仅有计数，不含 acquire/prepare 时长；manifest 写/锁等待没有 tree file/run 阶段统计。worker 首次连接、复用、重连与并发重叠无法由当前 summary 分开。 |
| 工具/证据 | 02 确认真实 GridFTP `globus-url-copy` 入口与固定种子 dataset/hash 工具存在。 | 04 核实当前 runner 不能表达明确配对 case manifest/arm order；outer elapsed 包含准备/hash/日志/stop；单文件 GridFTP hash 口径不匹配；cleanup 与空间门禁不能证明证据保存后才删。当前 runner goodput 不可用于性能 arms 比较。 |

### 分开的候选方向

以下是后续候选扫描，不是本任务获准运行的参数变化，也不是改默认值建议。先建立可比较基线，保持 `control_reuse=worker`、`scheduler=off`、`compression=off`、POSIX、fresh/no-resume 等冻结设置；具体矩阵由 01/02/04 确认。

| 传输范围/候选 | 证据等级与收益机制 | 风险/混杂 | 能证伪它的测量 |
| --- | --- | --- | --- |
| 单文件：connections 1 对 8（02 短矩阵已有此配置格，未运行） | C。多流可能更充分覆盖 RTT/单流窗口或分摊传输，但 100 Mbps 链路未必受单流限制。 | 更多 socket/thread/CPU 和握手，可能无收益或变慢；真实 GridFTP 单文件 `connections>1` 当前可能使用 partial GET 拼接，不等于普通多流，未匹配时不可作同格比较。 | 同源 fresh 文件、双向随机配对、每格重复；确认实测流数与安全级别；用统一 native transfer wall、logical goodput 和离散度比较。若 8 流不稳定更快或差异落在噪声内，则否定该配置在该环境的收益主张。 |
| 单文件：buffer/chunk 维度分开做小范围扫描（基线目前 64 KiB / 1 MiB；不在 PERF-MATRIX-01 首批格内） | C。更大的 IO/socket 批次或 chunk 可能减少 syscall/调度频率，是否限制当前 100 Mbps 未测。 | 两个 knob 同时改会混淆归因；大 buffer 增加每流内存，大 chunk 影响恢复粒度/排队延迟。不能通过改协议或 resume 语义来“优化”。 | 固定并行度和另一参数，单因素 paired scan；记录 CPU、send/read/write 时间、goodput、内存与 resume 正确性。吞吐无可复现提升、CPU 未下降或可靠性/资源恶化即可证伪。完成 02 初始矩阵后另派小扫描，勿扩张 186 case。 |
| 目录：dense file-parallelism 1/4/8、per-file connections 固定 1（02 短矩阵已有此配置格，未运行） | C；旧研究只提供 dense/mixed 汇总差距的动机，不证明并发瓶颈。并行文件可能重叠每文件控制/data 建连开销。 | server/端口、磁盘/CPU、manifest mutex 竞争会随并发上升；run timer 含 scan 与控制枚举。必须看 file-level 分布及实测连接，不以各阶段和代替 wall。 | 同 seed、fresh 目标、每格配对重复；保留每文件控制/数据 wall、成功率、磁盘/CPU、真实 stream 与 run wall。若提高并发不改善 wall/goodput、长尾加剧或资源/失败率恶化，则否定收益。 |
| 目录：mixed `(file_parallelism, connections)` 的 `(1,1)/(2,2)/(4,2)` 配置组（02 短矩阵已有，未运行） | C；可能共同覆盖文件级与单文件流级并行。该组是组合扫描，不可单独归因一个 knob。 | 两个 knob 同时变动；matched GridFTP 的 `-cc/-p` 及双向 GSI 需实测确认。 | 先以 dense 固定 per-file connections 的单因素结果解释 file parallelism；mixed 只评估端到端组合，不将差值归给其中单一参数。若真实配置不匹配则标 `unmatched_config` 并排除。 |
| 目录：控制复用的后续诊断（当前保持 worker，不改默认/策略） | B（仅历史机制证据）：旧 Beta 记录过小文件场景复用有帮助；旧 dirty 跨云矩阵不是新基线，也不能解释当前 worker 模式下残差。当前控制连接计数可观测（A），阶段时延不可观测。 | 本轮矩阵固定 worker；切换 off 不属于获准矩阵。小文件旧收益不能外推至 100 Mbps 当前两向负载。 | 若未来单独批准 off/worker 配对，测控制连接数与 acquire/prepare wall，并固定其余配置；控制阶段占比低或 wall 无差异即可否定其为当前首要方向。 |

checksum `none`/`crc32c` 在 02 是待冻结的验证 workload 对照，不在此列作性能实现候选：不得借测量任务更改校验/resume 行为。01 须确认 effective final-verify 语义，04 须独立验收后才可纳入。manifest flush/finalize 也先补观测；不据单次高耗时直接降低 flush/改变可靠性语义。

### 100 Mbps 与 100G 的边界

- **100 Mbps 先行目标**：先解决匹配的 GridFTP/CPNetFlux 比较、timer、effective verify、case/hash 和资源证据，再用有限短矩阵找配置敏感性。它只代表指定两端和链路条件；方向、身份认证、数据通道安全、实际并流必须记录。不将公网 100 Mbps 结果写成高速能力结论。
- **100G 后续目标**：需另行批准高速专线环境、固定 build 和资源预算，重新评估 NIC/CPU/NUMA/socket/存储及 buffer/chunk/IO backend；100 Mbps 的排序与收益都不外推到 100G，也不因指标可采集就声称 readiness。

### PERF-QA-01 blockers 责任分类

| 责任 | blocker 与进入下一阶段的条件 |
| --- | --- |
| 02 runner/证据工具 | 加显式 case manifest、配对 block/order 与预期配置；runner 分离 prep/hash/log/cleanup 与统一 transfer timer；统一单文件 file SHA 与目录 canonical hash；保存 evidence/cleanup 结果，先持久化和核验后再删。工具变更须形成可复核小矩阵 dry plan，但此任务不运行。 |
| 03 核心客户端 | 在冻结契约后提供统一 native elapsed 及实际/effective 配置，覆盖 file 与 tree 上传/下载；tree 记录逐文件且失败/skip 可分类，并正确合并普通及并行路径。单文件现有 STOR/RETR elapsed 边界不同；tree 的 control commands、单文件引擎等待、控制 226 和本地下载 commit 需明确拆分。tree options 缺 final-verify policy，调用 file client 时没有贯通该策略；只有 01 决定语义后才考虑添加传递/输出，不改现行缺省行为。 |
| 01 契约 | 冻结两端同义 timer start/stop、hash/计时分母、auth/data TLS 可比性和 GSI reverse 流映射；冻结 none/CRC 的 requested/effective final verify、verified chunk coverage 与 flush/commit 证据。PERF-ROUTE-01 目前因 active writer blocked；未冻结前 03 不开代码任务。 |
| 04 回归验证 | 明确 schema、空文件/空阶段/失败/重试/resume/并发重叠测试及“缺观测不改 transfer/integrity 状态”；之后 review 02 工具和 03 客户端。既有 upload/download/parallel/control-reuse/resume/edge tests 是真实链路入口，但不是新契约验证。完整 CTest 还须正确注入 token；skip 不算 pass。 |
| 05 环境 | ENV-01 实时记录上海 `/`、`/tmp` 可用为 0（批准清理 0 GiB）；深圳约 48.88 GiB 可用且观察到外部写入/状态竞争迹象。禁止本任务内 SSH/build/实验；正式固定提交任务开始前重验资源、PID、端口、依赖和预算，上海未恢复则阻塞。 |

### 最小任务切分与顺序

1. **PERF-ROUTE-01（01，当前 blocked）**：先交联合路线与 timer/checksum/auth/data-channel 契约，明确 100 Mbps 验收边界和未来 100G 非目标。无此项不得派发性能行为改动。
2. **PERF-TOOL-01（02，04 审 spec）**：在允许路径内做显式、可重复 case manifest 与配对顺序、hash 类型、证据和 cleanup 门禁；与 03 协调 native elapsed 消费字段。工具不能先把现有不匹配 elapsed 当 go-goodput。
3. **PERF-FILE-OBS-01（03，04 review）**：单文件只补契约要求的 steady-clock 边界/effective 配置记录，修正统一 elapsed 出口并测得 metrics 可用；不改变 frame/默认值/校验/resume/IO 策略。边界覆盖 STOR data COMPLETE、RETR final verify/commit 的契约差异；必要的控制面等待由 tree caller 层单独归属。测试边界：单文件 options/phase 单元测试，加真实 STOR/RETR/hash smoke；路径由正式 task allowlist 指定。
4. **PERF-TREE-OBS-01（03，依赖 FILE-OBS 与 01 schema；04 review）**：tree upload/download 逐文件采集 control acquire/prepare、file transfer、complete/finalize 与 run wall，明确 scan_plan、远端目录枚举及独立 tree_verify；确保普通 scheduler 和 worker reuse 路径均记录、并发统计不冒充 wall。只有 01 批准后，才增加 tree final-verify 参数传递/effective 记录。测试边界：metrics 聚合单元测试；真实 upload/download、parallel、control reuse、resume、空文件与失败 smoke/hash，缺指标只能标 evidence partial。
5. **PERF-QA-02（04）→固定提交验收（05+04）→短矩阵（02+05）**：分别独立审查 runner/client；上海空间恢复且两端即时预算满足后才 build/CTest；凭实际命令、二进制 hash、有效 case 和证据门禁重新批准筛选阶段。若筛选没有稳健收益，先报告不晋级，不进入高成本确认或 100G 叙事。

路径边界目前是候选，不授权代码修改：单文件预估 `include/cpnetflux/core/io/transfer_runtime_metrics.h`、file upload/download clients 及对应 config/parser/unit/smoke/CMake；目录预估 `src/core/io/tree_transfer_client.cpp`、tree config/options、必要 metrics/schema 和 tree smoke；02 单独负责 `tools/experiments/gridftp_compare` 与其测试。最终每份正式任务须按 01 定稿进一步列完整路径白名单、输入 commit、命令和输出证据位置；不允许借此修改 BOARD 或默认 control reuse。

### 本次命令、验证与交接

- `git rev-parse HEAD`：退出码 0，`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- `git status --short --branch`、`git diff --name-only`、`git diff --cached --name-only`：执行时读到既有 BOARD/02/04/05 与 task 文档变化；暂存区为空。未更改或覆盖他人改动。
- 定向 `rg` 与 UTF-8 源码/回执读取：用于以上模块边界、runner blockers、环境快照。无编译执行、无传输数据或实验输出；不声称候选有效。
- `git diff --check`：执行前退出码 0；追加后的该任务验收需重跑，并回读确认仅新增本节、没有源码/测试差异。
- 未运行 CMake、CTest、runner、benchmark、SSH 或云端动作，符合任务禁止项。输出证据为本回执；没有新的 binary/hash/case 证据。
- 下一步：00 验收本候选拆解；01 完成 PERF-ROUTE-01 并冻结契约后，由 00 分别发 PERF-TOOL-01、PERF-FILE-OBS-01、PERF-TREE-OBS-01 正式任务，路径/验收不能从本建议自动推定授权。上海空间恢复和 05 重验是任何 Linux build/实验的硬前置。

### PERF-IMPL-PLAN-01 文档验收记录

- 输入检查：`git rev-parse HEAD` 退出码 0，仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`git status --short --branch` 退出码 0；任务开始前的 `git diff --check` 退出码 0。
- 追加后 `git diff --check` 和针对本回执的 `git diff --check -- docs/coordination/receipts/03-implementation.md` 均退出码 0（Git 仅提示 LF→CRLF，不报告 whitespace error）；`git diff --exit-code -- src include tests tools CMakeLists.txt` 退出码 0；`git diff --cached --name-only` 退出码 0、输出为空。
- Python UTF-8 内容门禁最终退出码 0：确认新节仅追加在 HEAD 版回执之后、输入 SHA/路线目标/候选/责任分工/依赖齐全、文件以换行结束且没有尾随空白。第一版校验器因错误要求英文 `single` 哨兵退出码 1；第二版空白谓词受 PowerShell/Python 转义影响误把 `t` 当空白也退出码 1。修正为中文字段检查及逐行 `rstrip()` 后通过；这两次是校验器误报，没有改动正文或代码。
- 本节之后的最终复核须确认唯一新增的本角色产物仍是该回执小节，HEAD 不变、共享暂存区为空；不运行 CMake/CTest/runner/实验/SSH。

## PERF-IMPL-CONTRACT-02 v1：源码级 effective telemetry 字段映射

日期：2026-09-23（Asia/Shanghai）。路线/任务版本 `R2026-09-23.1 / v1`。本节是只读源码映射候选，供 00 验收及 04 复审；不是已实现 telemetry 或已冻结 schema。

### 输入与核验边界

- `git rev-parse HEAD` 实查为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务输入一致；BOARD 与本任务仍列路线 `R2026-09-23.1`。若之后 HEAD/路线变化，本节结论需重核。
- 工作区既有 BOARD、ROSTER、02/04/05 回执修改及多个未跟踪任务文档；本角色仅在本回执追加本节，未覆盖或暂存这些改动。未创建 worktree，未触碰共享 index。
- 仅定向读取本任务、既有 PERF-IMPL-PLAN-01、02 PERF-TOOL-PLAN-02 v2、04 PERF-TOOL-QA-02，以及配置/运行指标、file/tree transfer、scheduler、file-IO 源码和相关测试入口。
- 不定义 timer/auth/GSI/final-verify/checksum 语义，不改代码、schema、默认值、协议、scheduler 或 IO backend。CMake、CTest、runner、传输、构建、SSH、profile、benchmark、实验及清理均未运行；本节提及的测试全部 `NOT_RUN`。

### 字段类别

| 类别 | 当前源码字段/来源 | 可作何种解释 |
| --- | --- | --- |
| 请求值 | 单文件 `FileTransferOptions`/`FileDownloadOptions` 的 `fileIo` 与 `hotPathCompression`；tree `TreeTransferOptions` 的 `controlReuseMode`、`schedulerMode`、`schedulerPolicy`、`compressionMode`、连接/缓冲参数。parser 在 `src/config/*_options.cpp` 中填值。 | 代表调用者配置。单文件压缩选项为内部结构；file client/server 输出没有压缩 requested 字段。tree JSON 输出 `scheduler_mode`、`scheduler_policy`、`control_reuse_mode` 等配置值，但不输出 `compression_mode`。runner argv 只能证明请求，不能替代参与进程的运行时记录。 |
| 运行时 effective | `storage::effectivePosixWriteStrategy()` 将 Auto 按 `bufferSize > 0` 解析为 Coalesced，否则 Direct；日志字段 `posix_write_strategy_effective` 由此生成。Global tree scheduler 按 dispatch 的 `targetConnections` 改写该文件的 `connections`，并按 compression disposition 设置上传 `hotPathCompression`。 | 前者是 POSIX 写策略解析值；若 backend 为 io_uring，该 POSIX 策略不参与实际写入。后者源码可追踪到当前派发值，但没有逐参与 worker/file 的 effective 配置输出。没有通用 `compression_effective`、`scheduler_mode_effective`、`control_reuse_effective` 或 `file_io_backend_effective` 字段。 |
| 实际 observed | 单文件 sender/receiver 的 per-call 计数、FileIoStats；tree `TreeRunStats` 和 Global scheduler CSV/JSONL。 | 计数只有在对应代码路径执行且结果到达导出点时才表示观察到的动作。计数零本身不表示该参与者已覆盖，也不等价于 off。 |

### 单文件路径

| 路径与位置 | 当前字段/计数及 scope | 失败、重试、并发覆盖 |
| --- | --- | --- |
| STOR 客户端 sender：`src/core/io/file_transfer_client.cpp:275`、`:573`、`:616`、`:635` | 每 DATA stream 的 `compressionAttempts`、`compressedFrames`、`rawFallbackFrames`、`compressionFailures`、`compressedLogicalBytes`、`compressedWireBytes`；所有 stream join 后聚合为一次 file-client call，写 `TransferRuntimeMetrics` 并打印 `file_client`。有效压缩动作只由 `enabled && candidate && !forceRaw` 触发；帧计数不等于启用状态。 | 并发 stream 在成功 join 后合计，未输出 stream ID。任一 stream status 失败会在汇总/导出前返回，失败调用可能没有这些计数。单次调用内 resume/resend 的动作随 stream 实际处理计数；跨调用 retry 需由上层明确关联。 |
| STOR 服务端 receiver：`src/core/io/file_transfer_server.cpp:591`、`:1096`、`:1109` | 每个接收 transfer 的 `compressedFrames`、`decompressionFailures`、压缩 logical/wire bytes，导出于 `file_server`；这是解码端观察，不含压缩尝试或 raw fallback。 | 按 transfer 汇总并行 stream。输出位于成功完成路径；提前错误路径不能以缺失/零值代表完整覆盖。与客户端可用 `transfer_id` 关联，但没有 worker/process coverage ledger。 |
| RETR 客户端 receiver：`src/core/io/file_download_client.cpp:493`、`:763`、`:796`、`:813` | per-stream `compressedFrames`、`decompressionFailures`、压缩 logical/wire bytes，stream 汇合后作为一次 `file_download_client` call 输出；没有 `compressionAttempts`/raw fallback，因为本端是接收者。 | counters 聚合仅在完成后续 stream、finalize/commit 路径后导出；接收解压失败虽在局部递增，调用提前返回时不会产生完整汇总。并发 stream 没有单独参与者 ID。 |
| RETR 服务端 sender：`src/core/io/file_download_sender.cpp:230`、`:480`、`:521`、`:536` | 每连接 sender 的压缩尝试、compressed/raw-fallback frames、失败及压缩 bytes；join 后为一次发送 call 汇总，输出 `file_download_sender` 与 `TransferRuntimeMetrics`。这是 RETR 压缩生产端证据。 | 任一发送 stream 失败会先返回，聚合日志/metrics 不产生。成功时并发连接合计，无 per-stream/worker ID。 |

上述 file 日志的 `file_io_backend`、buffer/queue/batch/advice、`posix_write_strategy` 是 `options.fileIo` 配置回显；只有 `posix_write_strategy_effective` 是明确解析字段。file download client、file download sender、file transfer server 和 STOR client 均调用 `appendFileIoStats`，但其日志也在各自完整成功输出路径。

### Tree upload/download、scheduler 与 control reuse

| 路径/位置 | 当前字段/计数及 scope | 覆盖边界 |
| --- | --- | --- |
| tree 通用 summary：`src/core/io/tree_transfer_client.cpp:636`、`:880`、`:907` | `TreeRunStats` 维护 run 级原子 compression counters、`controlConnectCount`、`controlReconnectCount`、`dataTransferCount`。stdout/JSON 记录 requested `control_reuse_mode`、connect/reconnect/data-transfer 计数；JSON 记录 scheduler mode/policy request。无 run-level compression counters 的常规 summary。 | 普通 tree scheduler 将同一个 `TreeWorkerRuntime` 交给每个 worker，但 copyStats 只复制连接/传输/文件数据统计，不复制 compression `TransferRuntimeMetrics`。逐文件 file-client 日志仍存在，但没有 worker ID 或完整参与者/失败覆盖集合。 |
| tree upload：`:1307`、`:1409`、`:1456`、`:1465` | 每文件调用 STOR client。只有 Global scheduler 分支把 `transferMetrics` 放入 runtime，再由 `completeGlobalDispatch` 聚合到 run stats；tree `compressionMode` 通过 `schedulerConfig.enableCompression` 控制 plan，dispatch candidate 才将 enabled/candidate/max payload 设置给上传 sender。 | 普通模式 runtime 非空但不进 Global 汇总，故 run summary 不含这些 compression 计数。失败的 file client 可在写回 metrics 前返回；Global executor failure event 可记录失败，但不补齐 sender 动作计数。压缩失败时强制 raw resume retry 会 merge retry metrics，并记录 retried work items/event；首个失败调用通常无完整 runtimeMetrics 可 merge，因此总计数并非失败全过程覆盖。 |
| tree download：`:1479`、`:1592`、`:1604`、`:1613` | 每文件调用 RETR client；本地接收端显式 `hotPathCompression = {}`，输出的是 compressed frames/decompression observed counters。只在 Global scheduler 下送入 Global 汇总。远端 RETR sender 是另一参与进程，其压缩计数在服务端 `file_download_sender` 日志，不由 tree client runtimeMetrics 代表。 | 普通模式不聚合 run compression counters。下载传输成功后 `dataTransferCount` 先递增，随后等待 control 完成；因此该计数不是文件最终成功数。local call/control/finalize 失败会使树级输出无法代表每个参与者的完整压缩观测。 |
| Global scheduler：`src/core/io/tree_transfer_client.cpp:2175`、`:2184`、`:2250`、`:1850`、`:2041`；record 定义 `include/cpnetflux/core/scheduler/metrics_writer.h` | 请求 policy、initial/max connections、compression enabled 来自 options；CSV summary 记录 policy、initial/current/target/max connections、ramp/pressure/retry counters。events/samples 有 task/file IDs、target connections、compression decision/dispatch/fallback/reject 等。summary 的 `compressionAttempts` 来自实际 attempts；但 `compressedWorkItems` 直接赋值 `compressedFrames`，`rawFallbackWorkItems` 赋值 `rawFallbackFrames`，`rawWorkItems` 来自计划数；`compressionRatioEffective` 是观测 bytes 比值，无 compressed bytes 时默认 1.0。 | summary 是 run/link 级状态而非逐进程/worker effective 记录；events 对 task/file 有粒度但不枚举每个未压缩动作/参与 worker。字段名 `compressedWorkItems` 与其 frame counter 来源不一致；ratio=1.0 或无 candidate/reject event 均不能证明 compression off。Global summary 可能在调度文件执行失败后写出，但失败调用的动作计数仍可缺失。 |
| control reuse：`:1141`、`:1151` | `control_reuse_mode` 是请求值；`ensureControlReadyWithStats` 只在建连成功后增加 run 级 `control_connect_count`。Worker 模式复用 `TreeWorkerRuntime.control`，Off 模式使用 file-local control。 | 无 reuse-hit、worker ID、每文件 acquisition 结果或有效复用率。`controlReconnectCount` 在当前源码只有声明、复制和打印，没有递增点，因此打印 0 不表示已证明“没有重连”。失败建连不计数；连接计数并发累加但只到 run scope。 |

### File IO 观测

- 请求配置在 `include/cpnetflux/storage/file_io.h` 的 `FileIoConfig`：backend、buffer size、queue depth、batch size、advice、POSIX write strategy。单文件结果输出同时回显这些参数；tree client 将 tree buffer 参数复制到逐文件 options，但 tree summary 不包含 file IO effective/observed 记录。
- `FileIoContext::backend()` 返回配置 backend；`validateAvailable()` 不可用时返回错误；`readAtAll`/`writeAtAll` 按该 backend 明确分支，没有自动 fallback。因此结果行 `file_io_backend` 与执行选择同源，但字段名是 options echo，不含单独 effective 标签或完整参与者覆盖证明。io_uring unavailable 会失败，不会静默改走 POSIX。
- `posix_write_strategy_effective` 在 `src/storage/file_io.cpp:313` 由配置解析；仅当 POSIX write path 实际执行时才是该次行为的 effective strategy。设置为 io_uring 时这个 POSIX 策略值虽仍被打印，但不表示 io_uring 写入采用该策略。
- `FileIoStats` / `appendFileIoStats` (`src/storage/file_io.cpp:543`) 提供 per-file-call 聚合：stage read/write calls/bytes、wait seconds、POSIX syscall/retry/short/zero counts，以及 io_uring submit/wait/completion/SQE/partial/retry 和 completion bytes/平均值。多个 stream 共享每次调用的 stats。`readAtAll`/`writeAtAll` 按调用传入 length 记录 bytes；该字段不能替代跨参与者成功覆盖状态。早退路径无最终结果行。
- 候选最小映射应分别保留 `file_io_backend_requested/effective`、调参 requested/effective、POSIX strategy requested/effective 和 observed backend operation counters；当前可沿用的字段只能标成配置回显或 per-call observed counter，不可把空值补零。

### B5 候选字段与不能证明的事项

候选记录按 case/attempt、方向、file ID、participant process/worker ID、发送/接收角色关联，并显式标注预期参与者数、实际记录数及 coverage complete/partial。每个参与进程各自记录 requested、源码解析出的 effective 值、report source 和 observed counters；失败/中止也要能产生带 coverage 状态的记录。它是供 QA-02 B5 映射使用的字段建议，不等于批准新 schema 或 wire 消息。

- Compression：requested/effective mode 与 scheduler decision 分开；sender 侧映射现有 `compression_attempts`、`compressed_frames`、`raw_fallback_frames`、`compression_failures`、压缩 logical/wire bytes；receiver 侧映射 `compressed_frames`、`decompression_failures`、解压逻辑/线缆 bytes。`compression_sampling_attempts` 无当前等价计数；`compression_dispatches` 只有 scheduler event 级候选，不能当每 DATA 的完成动作；未覆盖的 receiver/sender 或失败调用不能补 0。即使所有实际 attempts 为零，也需 effective 值与完整参与者覆盖才能按 QA 规则评为 verified_off。
- Scheduler：保留 requested `scheduler_mode/policy`；Global 可映射 run/link 的 current/target/max connections、ramp/queue/pressure/retry 和 task/file events。缺 per-worker applied policy/connections、普通 scheduler effective mode/participant 记录；request policy 或 dispatch event 不能替代全参与者 effective 配置。需修正/澄清 frame 数量与 `compressed_workitems` 名称对应关系后，方可作为严格字段映射。
- Control reuse：当前可映射 requested mode 和成功 connect count；缺每 worker control connection identity、per-file reused/acquired 状态、failed reconnect 计数及完整覆盖。connect 数与文件数的比值可作为 smoke 断言，但不是 `control_reuse_effective` 的全 worker telemetry。
- File IO：当前映射配置回显、POSIX strategy 解析值和 FileIoStats；缺明确 backend effective 标记、参与者/worker 覆盖和失败调用的最终 counters。不能从 runner argv、`wire_bytes/logical_bytes` 比率或成功日志缺席推导任何 compression/file-IO off 状态。

### 测试入口与边界（本任务均 NOT_RUN）

| 入口 | 可触及的源码行为 | 不能证明的内容 |
| --- | --- | --- |
| `tests/unit/hot_path_compression_test.cpp`（CTest 单元目标） | 本机 loopback 的 STOR/RETR 压缩、接收解码、fallback/损坏恢复相关数据路径及部分 runtime counters。 | 不是 tree run/全部 worker 覆盖测试；不证明失败调用 telemetry 完整、requested/effective 记录或真实云端性能。 |
| `tests/unit/file_io_test.cpp` | FileIo config 解析、POSIX effective write strategy、FileIoStats 调用/bytes、io_uring 可用性及 completion-loop 单元逻辑。 | 不检验单文件/tree 日志字段、每参与者 backend effective 覆盖或失败 transfer 汇总。 |
| `tests/unit/scheduler_metrics_writer_test.cpp`、scheduler advisor/work-item tests | Scheduler record 序列化及 advisor/plan 组件行为。 | writer fixture 不验证 `tree_transfer_client` 如何填入字段，也不能证明 Global scheduler 报告覆盖真实运行 worker。 |
| CTest `cpnetflux_tree_control_reuse_smoke` | upload Off/Worker 模式、tree JSON 的 mode/data transfer/connect count，检查连接数相对文件数的关系。 | 不测 download、per-worker reuse hit、连接失败/reconnect telemetry，也不检验 compression/IO coverage。 |
| CTest `cpnetflux_tree_scheduler_smoke` 与 file/tree transfer smoke | 能运行真实本机构造的 tree/file data path；scheduler smoke 会检查成功场景 summary/events 和部分 compression 计数。 | 不构成所有参与进程/worker 的 coverage ledger；没有覆盖所有 scheduler/mode/方向与失败/重试组合的 effective-off 证明。上述 CTest 和所有其他入口本次均未运行。 |

### 执行回执

- 输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；路线 `R2026-09-23.1`；执行前 `git status --short --branch` 为 `main...legacy-reference/main [ahead 11]`，状态含既有共享文档修改和任务单 untracked；不修改这些路径。
- 输出：仅追加本节至 `docs/coordination/receipts/03-implementation.md`。没有源码、测试、runner、CMake、schema、协议或 IO/scheduler 行为改动，没有 worktree、暂存、commit 或云端动作。
- 实际核验命令：`git rev-parse HEAD`、`git status --short --branch`、定向 `rg`/源码及测试入口读取；各退出码均为 0。`git diff --check` 在追加前退出码 0，追加后的复核结果记录于最终执行回执。CMake/CTest/runner/传输/构建/实验/profile/benchmark/SSH/清理：均 `NOT_RUN`。
- 后续交接：由 00 决定是否形成实现任务，再由 04 参考复审。本节不批准任何 telemetry 已实现或 `verified_off` 的结论，也未定义 01 所属 timer/auth/GSI/verify 契约。

### PERF-IMPL-CONTRACT-02 最终验收记录

- `git rev-parse HEAD`：退出码 0，`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`git status --short --branch`：退出码 0，包含共享工作区已有的其他文档改动及本回执修改。
- `git diff --check -- docs/coordination/receipts/03-implementation.md`：退出码 0，仅有 Git 的 LF→CRLF 提示；`git diff --exit-code -- src include tests tools CMakeLists.txt`：退出码 0，源码/测试/runner/CMake 无差异。
- `git diff --cached --name-only`：退出码 0、无输出；`git diff --numstat -- docs/coordination/receipts/03-implementation.md`：仅新增行、无删除（同一回执还含 PERF-IMPL-PLAN-01 的追加内容）。
- CMake、CTest、测试程序、runner、传输、构建、实验、profile、benchmark、SSH 和清理均未运行；本任务涉及的所有测试保持 `NOT_RUN`。

## PERF-IMPL-ADAPTER-01 v1：规范化 telemetry 适配与覆盖清单

日期：2026-09-23（Asia/Shanghai）。输入路线/任务：`R2026-09-23.1 / v1`；本节是源码映射和后续实现白名单建议，不是 adapter 或 telemetry 实现。

### 前置与规范输入

- `git rev-parse HEAD` 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务输入一致；`git diff --name-only -- src include tests tools CMakeLists.txt` 和相同路径的 staged 检查均无输出。HEAD/源码状态前置满足。
- 读取 02 `PERF-TOOL-PLAN-03 v3` 与 04 `PERF-TOOL-QA-03` 的 B5 段落。02 的 `CompressionObservationV1` 要求 adapter/version、case/attempt/direction/shape/scope、raw artifact refs + hash、requested compression/scheduler、逐 participant effective 值、expected/observed participant 集合、file/worker/lifecycle coverage，以及五种逐 participant counter：`sampling_attempts`、`compression_attempts`、`compression_dispatches`、`compressed_frames`、`compressed_payload_bytes`。
- v3 的 expected participants 必须来自 run/route/worker roster，不能从观测到的记录反推。expected 集合非空且与 observed 精确相等；每 participant 的 effective 值可解析且 off；每文件、方向、shape 和生命周期 coverage 完整；五类计数均 present、整数、非负且为 0；artifact/hash 完整，才可能为 `verified_off`。缺失、null 或覆盖差集必须为 `unknown/ineligible`，不可补零。
- v3 schema 没有 checksum 或 FileIo 字段；本节单独映射这些字段，不把它们扩写进 `CompressionObservationV1`。校验/verify 的比较语义仍依赖 01 冻结，04 的 QA-03 仍判 B5 partial；本节不宣称 B5/B6/B7/B8 或任何实现通过。

### Native 字段到规范计数候选

| v3 规范键 | 源码事实与单位 | 映射等级 / 缺口 |
| --- | --- | --- |
| `sampling_attempts` | Global scheduler 可写 `scheduler_samples.csv` 的 fileId/offset/length/sample ratio/decision/CPU/link fields：`src/core/scheduler/scheduler.cpp:143`、`:174`；writer schema：`include/cpnetflux/core/scheduler/metrics_writer.h:65`。每行是一次 file-range sample 结果，不是显式 sampling invocation counter。 | **missing**：无参与者 ID、attempt counter 或 complete coverage；不能把样本行数映射成每 participant 的尝试数。 |
| `compression_attempts` | STOR sender 在候选 DATA 上递增，成功后按 stream 汇总为 transfer call：`src/core/io/file_transfer_client.cpp:275`、`:573`、`:616`。RETR sender 同义计数：`src/core/io/file_download_sender.cpp:234`、`:480`、`:521`。单位为实际 compressor invocation / DATA payload，不是文件数。 | **partial**：仅 sender 有等价计数；aggregated stream total 无 stream/participant ID，只在成功导出点可见，失败早退可能无结果。 |
| `compression_dispatches` | Global tree scheduler 对 candidate FilePlan 写 `compression_dispatch` event：`src/core/io/tree_transfer_client.cpp:1705`、`:1710`，event 结构含 task/file/link/target connections：`include/cpnetflux/core/scheduler/metrics_writer.h:50`。单位是 candidate 文件计划 dispatch，不是已执行的 DATA 压缩帧。 | **partial**：是计划级事件，不是规范 counter 的 per-participant 实际动作；非 candidate/raw dispatch 没有同一事件，也无 worker/process ID。 |
| `compressed_frames` | STOR sender 在编码结果小于 raw payload 后递增：`src/core/io/file_transfer_client.cpp:295`；RETR sender：`src/core/io/file_download_sender.cpp:254`。接收端 STOR server/RETR client 在接受 compressed DATA 后递增：`src/core/io/file_transfer_server.cpp:619`、`src/core/io/file_download_client.cpp:526`。单位为发送/接收压缩 DATA frame；sender/receiver 分开记。 | **partial**：正数可以证明该 report source 观察到动作；零需要对应 endpoint、streams、文件和生命周期全覆盖。当前 per-call 聚合及成功出口不足以做到此覆盖。 |
| `compressed_payload_bytes` | sender 的 `compressedWireBytes` 累加编码 DATA payload `wirePayloadSize`：`src/core/io/file_transfer_client.cpp:297`、`src/core/io/file_download_sender.cpp:256`；receiver 的同义来源是 compressed DATA `header.payloadSize`：`src/core/io/file_transfer_server.cpp:621`、`src/core/io/file_download_client.cpp:528`。单位为 compressed DATA payload bytes，不含 frame header。 | **partial**：可作 raw adapter 的 byte source，须记录 sender/receiver source 和成功/失败覆盖；不能以 `wire_bytes/logical_bytes` 比率代替。 |

现有辅助字段不属于上述五键的替代品：`raw_fallback_frames`、`compression_failures`、`decompression_failures`、`compressedLogicalBytes` 可保留为额外诊断字段。`compressionRatioEffective` 是 bytes 比率；Global summary 的 `compressedWorkItems` 实际来自 frame 计数：`src/core/io/tree_transfer_client.cpp:2062`；没有 compressed bytes 时 ratio 默认 1.0：`:2069`。它们均不能证明 effective off。

### 路径、单位和生命周期

| 路径/模式 | 现有记录及边界 | 覆盖结论 |
| --- | --- | --- |
| Single upload / STOR | 客户端 `runFileTransferClient` 在 DATA sender stream 内采样压缩计数，join 后按 transfer call 汇总并输出 `file_client`：`src/core/io/file_transfer_client.cpp:275`、`:573`、`:635`。服务端 per-transfer 解压 counters 在 `file_transfer_server.cpp:591`、`:1096`、`:1109`。单次调用的 `transfer_id` 可关联两端，但两侧都没有 process/stream roster 输出。 | sender 的 attempts/dispatch 相关实际动作与 receiver 的 compressed frames 是不同来源；任一 stream 失败时调用会在完整汇总/最终输出前返回。并发只得到 call total；没有逐 stream coverage。 |
| Single download / RETR | 服务端 `runFramedFileSender` 的每 stream attempts/frames 在 `file_download_sender.cpp:230`、`:480`、`:536` 汇总。客户端 receiver 在 `file_download_client.cpp:493`、`:763`、`:796`、`:813` 汇总 compressed frames/decompression failures。 | sender/receiver 是两个 process participant，客户端 metrics 不能代表服务端 sender。计数导出在成功 finalize/commit 路径；失败调用/中断不能用日志缺失或零值表示 off。 |
| Tree upload / scheduler off | manifest 提供预期 file paths/status；每文件事件含 relative path：`tree_transfer_client.cpp:1318`、`:1474`。普通 `runTreeScheduler` 每 worker 持有 `TreeWorkerRuntime`：`:1109`、`:2342`；其默认 hot-path options 传入 file client，当前 summary 不汇总该路径的 compression metrics：`:2304` 后的 copyStats 只复制文件/连接/传输数据。 | static route 选 off 不等于运行时逐 participant effective record。文件覆盖部分可由 manifest/event 重建，但缺 file→worker、client/server roster、各 participant effective 值和 failure-complete counters。 |
| Tree download / scheduler off | 每文件调用 `runFileDownloadClient`，本端 `hotPathCompression = {}`，并生成 file lifecycle events：`tree_transfer_client.cpp:1479`、`:1581`、`:1592`、`:1630`。RETR sender 在远端 `file_download_sender`，不在 tree client runtime。 | tree 客户端接收计数不是 sender compression evidence；远端 participant 不随 client JSON/event roster 输出。文件 manifest 能列预期文件，但无 worker/source coverage 集合。 |
| Tree Global scheduler off/fixed/adaptive | requested mode/policy 在 options 和 JSON summary；effective configuration 通过 scheduler config/dispatch 构造：`:2175`、`:2184`、`:2250`。per-file candidate event 带 file ID 和 target connections；run/link summary 写 counters/connection state：`:2041`、`:2062`、`:2079`。 | 比普通模式多了 file-plan、sample 与 controller 候选记录，但仍缺 participant/process/worker identity、每 worker effective values 和 complete file lifecycle/counter coverage。`compression_dispatch` 是 file plan 单位；summary 总量不能反推每 worker。Off case 不外推 fixed/adaptive。 |
| Tree control reuse Off/Worker | `control_reuse_mode` 是请求值；Worker 模式连接存于 worker-local runtime，Off 模式用 file-local control：`tree_transfer_client.cpp:1109`、`:1151`。`control_connect_count` 在 ensure 成功时增加：`:1141`；tree run 打印连接总数：`:880`、`:982`。 | worker reuse hit 没有计数。`controlReconnectCount` 当前无递增点，仅声明/复制/打印；0 不能作为 no-reconnect 或 hit 证据。并发 connect count 是 run 聚合，无 worker/connection ID。 |
| Failure/retry/parallel | Tree event log 有 file_start/complete/failed/skipped/changed：`tree_transfer_client.cpp:1318`、`:1421`、`:1474`、`:1489`、`:1600`、`:1630`；Global scheduler 有 `executor_failed`：`:2031`。tree JSON 有 run `result/error_code/error`：`:994`。Global compression retry 会强制 raw 并记 retry event：`:1998`、`:2002`。 | tree event 不是所有早退返回的统一 terminal record；单文件失败时 runtime metrics/成功结果行可能缺失；进程中断无 terminal event。并发只按 file/run totals，无法证明每个 worker/path 均闭合。retry 首次失败和 raw retry 缺少统一 attempt record/覆盖链。 |

### 请求值、effective 值与相邻策略

- **Effective compression：partial。** `HotPathCompressionOptions` 可在 C++ 调用时控制 enabled/candidate/forceRaw；tree Global 从 compression disposition 派发这些值。没有 `compression_effective` 字段按 participant 输出；single file options 和 tree JSON 也没有对应 effective 值。scheduler Off/Global Off 的实现分支有静态源码事实，但不构成当次 runtime roster + counter coverage。
- **Scheduler effective：partial。** tree JSON 的 `scheduler_mode/policy` 是 options echo；Global summary 的 current/target/max connections 是 run/link controller 输出，不是逐 participant applied config。single file 无 tree scheduler option；不可仅凭字段缺席自动补成规范 `off`。
- **Checksum effective policy：partial，且不属于 `CompressionObservationV1`。** 单文件 client/server 在运行前 `resolveChecksumBackend`，成功行 `checksum_backend` 使用 resolved backend：`file_transfer_client.cpp:527`、`:632`；`file_download_client.cpp:634`、`:809`；sender/server 相应为 `file_download_sender.cpp:407`、`:533`、`file_transfer_server.cpp:405`、`:1105`。tree JSON 的 `checksum_algorithm/backend` 则直接写 options：`tree_transfer_client.cpp:986`。RETR client/STOR server 成功日志还输出 `final_verify_policy_effective`：`file_download_client.cpp:827`、`file_transfer_server.cpp:1125`。这些是不同字段和 scope；没有统一 checksum requested/effective/coverage record，且 final-verify 语义待 01 冻结。`verified_bytes`/`checksumSeconds` 不能自行解释为算法覆盖证明。
- **IO effective：partial。** `FileIoContext::backend()` 取配置值并按它选择 backend；Unavailable 返回错误，不自动 fallback。日志有 `file_io_backend` 配置回显、POSIX `posix_write_strategy_effective` 解析值及 per-call FileIoStats，但没有每 participant backend-effective + coverage 记录；失败早退通常无完整最终 counters。

### 覆盖状态清单

| 要求项 | 状态 | 源码证据与缺口 |
| --- | --- | --- |
| participant expected/observed roster | **missing** | 源码输出没有 participant/process/PID/worker ID 集合；全局 `rg` 对这些字段无匹配。scheduler event 的 task/file/link ID 不能当 participant roster。expected 集合必须另有启动/服务/worker roster 或冻结 route inventory。 |
| file coverage | **partial** | Tree manifest 与 file lifecycle path 可以给 expected files 和部分 outcome；single transfer ID/路径只在 transfer 结果可见。没有完整 expected↔observed 文件集合、与所有 endpoint/worker 的绑定及中断 gap 标记。 |
| worker/stream coverage | **missing** | file client/server 聚合并发 stream totals，无 stream/worker ID；tree 有配置的 fileParallelism/targetConnections，但不输出实际 worker roster 与每 file 的 worker assignment。 |
| lifecycle coverage / raw artifact refs | **partial** | tree 有若干 file/scheduler events、run result；single endpoint 多为成功 stdout summary。缺统一 start/dispatch/attempt/terminal coverage 和必需 source component、size、SHA refs；突然中止不能得到 terminal coverage。 |
| effective compression/scheduler per participant | **partial** | 有调用选项、源码派发分支和实际动作计数，但没有逐 participant requested/effective/report source/observation state。全五类 counter 也不完整。 |
| checksum effective policy | **partial** | resolved backend 与部分 receiver final-verify effective 值存在于成功输出；algorithm/options、coverage、端点与失败记录未统一关联，且不在 B5 normalized schema。 |
| control reuse hit | **missing** | mode echo + successful connect count；无 per-file reuse hit/connection ID/worker scope。Reconnect count 当前无更新点。 |
| 最终失败记录 | **partial** | tree 有部分 `file_failed`/`executor_failed` 和 run error summary；early returns、single file failed call、process interruption、retry attempt lineage 没有统一且必达的 terminal record。 |

依照 v3，即使单个成功 case 在 `scheduler=off` 且当前 sender counters 为 0，也因 expected participant、worker/file/lifecycle coverage、逐 participant effective 值及 `sampling_attempts`/`compression_dispatches` 缺口只能为 `unknown/ineligible`。不能创建 `verified_off`；需要另行覆盖 global fixed/adaptive、resume/retry、故障恢复的资格套件。

### 后续最小实现白名单建议

以下是待 00 另发正式任务后的建议路径，不是本任务授权或已有事实：

- **03 native records：** `include/cpnetflux/core/io/transfer_runtime_metrics.h`；`src/core/io/file_transfer_client.cpp`、`file_transfer_server.cpp`、`file_download_client.cpp`、`file_download_sender.cpp`；`src/core/io/tree_transfer_client.cpp`；必要时 `include/cpnetflux/core/scheduler/metrics_writer.h` 与 `src/core/scheduler/metrics_writer.cpp`、`include/cpnetflux/storage/file_io.h`/`src/storage/file_io.cpp`。目标是把 participant ID/role、file/worker/stream attribution、requested/effective/source、五计数的 present/value/unit/scope、coverage gap 和 terminal failure/retry 一起写入可关联的记录；错误和中断边界不能只靠 success stdout。
- **02 normalized adapter：** 由 02 自己维护 runner adapter，将上述 native report 显式映射到 `CompressionObservationV1`、raw artifact refs/hash 与 expected roster；只接受已登记映射/version，不猜字段，不从 argv、自由文本或 wire ratio 推导。
- **checksum/verify 边界：** 单独候选 native requested/effective algorithm/backend/policy/source/coverage 字段，待 01 冻结策略语义后再定白名单；不能塞入 B5 五 counter，也不能自行更改 checksum/resume/verify。
- **测试路径候选：** `tests/unit/hot_path_compression_test.cpp`、`tests/unit/file_io_test.cpp`、`tests/unit/scheduler_metrics_writer_test.cpp`、tree/file smoke 需加入 participant/file/lifecycle/failure coverage assertions；04 负责独立审查与授权测试。runner adapter 单元测试由 02/04 边界另行派单。

**B5 阻断关系：** 在 native roster/effective/coverage/五计数/terminal evidence 与 raw→normalized adapter 都可读可校验、并经 04 验收前，所有 CPNetFlux compression observation 只能 `unknown`，相关 case `performance_eligible=false`。此状态不构成 B6/B7/B8 验收，也不批准后续代码范围。

### 执行回执

- 输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；分支 `main...legacy-reference/main [ahead 11]`。源码、测试、runner、CMake 输入状态均无 unstaged/staged 路径差异；共享文档变化保持原样。
- 定向命令：`git rev-parse HEAD`、`git status --short --branch`、`git diff --name-only -- src include tests tools CMakeLists.txt`、`git diff --cached --name-only -- src include tests tools CMakeLists.txt`、源码/02 v3/04 QA-03 `Get-Content` 与 `rg`。HEAD/status/source checks 退出码 0；participant/process/worker identifier 搜索无匹配（`rg` 退出码 1）。一次复杂正则在 PowerShell quoting 阶段失败（退出码 1，未执行 `rg`），随后使用简单 event token 定向读取；不影响结论。
- 仅追加本节到本角色回执。写后 UTF-8、`git diff --check`、HEAD、暂存区结果在下方最终验收记录；不构建、不测试、不运行 runner/传输/实验、不 SSH、不改云端或清理。
- 本节所有项目测试、CMake、CTest、runner、profile、benchmark、实验均 `NOT_RUN`。下一步由 04/00 复审映射并另发白名单实现任务；本节没有改变路线或授予实现权限。

### PERF-IMPL-ADAPTER-01 最终验收记录

- `git rev-parse HEAD`：退出码 0，仍为任务输入 SHA。`git status --short --branch`：退出码 0；原有共享文档修改与任务单未跟踪状态保留。
- 严格 UTF-8 解码：退出码 0。全局 `git diff --check` 与目标回执 `git diff --check -- docs/coordination/receipts/03-implementation.md`：均退出码 0；只输出 Git LF→CRLF 提示。
- `git diff --exit-code -- src include tests tools CMakeLists.txt`：退出码 0；`git diff --cached --name-only`：退出码 0、无输出；目标回执 diff 为追加行、无删除。
- 无代码或测试执行；CMake/CTest/runner/传输/实验/SSH/清理维持 `NOT_RUN`。
