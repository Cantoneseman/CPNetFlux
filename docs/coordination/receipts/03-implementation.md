# 03 核心实现：增量执行回执（V3-LINKSET-01）

日期：2026-10-18。角色：03 核心实现；交付对象：00 总指挥。
状态：本次 LinkSet 能力广告切片已完成，控制面承载和真实多链路传输仍待后续任务。

- 输入提交：`4e2900842b7608f757d8c95c24ad62e6011000c0`；工作树：`/root/projects/CPNetFlux-worktrees/v3-linkset-01`。
- 修改文件：`include/cpnetflux/core/scheduler/link_profile.h`、`include/cpnetflux/core/scheduler/link_set.h`、`src/core/scheduler/link_set.cpp`、`tests/unit/scheduler_link_set_test.cpp`，以及本任务、BOARD、回执文档。
- 实现：能力广告稳定编码/严格解码、LinkSet 汇总能力、协商结果固定摘要；不改变现有 wire、默认传输策略或 8 通道限制。
- 验收命令：`cmake --build /tmp/cpnetflux-runs/V3-LINKSET-01-20261018-build --parallel 8`；`cpnetflux_unit_tests --gtest_filter=SchedulerLinkSetTest.*`；`cpnetflux_unit_tests`；`ctest --test-dir /tmp/cpnetflux-runs/V3-LINKSET-01-20261018-build --output-on-failure -j 8`。
- 输出证据：构建成功；定向 8/8、单元 329/329；CTest 370 项 368 通过，2 项 token-auth smoke 因 `CPNETFLUX_TEST_TOKEN` 未设置失败。
- 下一步交接：将摘要适配 `XCPNETFLUX V2` 的兼容控制字段，增加旧端行为测试，再进行深圳↔上海显式多链路实测；不在本切片中宣称真实多链路已验收。

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

## 2026-10-18 V3-WORK-UNIFY-05 完成交接

- 任务：V3-WORK-UNIFY-05；输入 3525a1638a653864c9abdb9cbb30badd603247cb；实现分支 codex/V3-WORK-UNIFY-05。
- 实际解决：static/per-file/global/pipeline 兼容编排共用 WorkExecutor/queue owner；terminal failure 释放 lease/credit；stop claims、静态通道保序和取消/join 边界收口；逐文件失败继续其它文件的语义保留。
- 验收：22 个定向单测、tree data reuse smoke、统一 executor smoke、362/362 CTest 均通过；最终版深圳↔上海双向 static persistent tree 互传 SHA-256 一致；1 次 reset 的故障证据收敛为 1 completed/1 failed。
- 证据：/tmp/cpnetflux-runs/V3-WORK-UNIFY-05-20261017；结果 docs/tasks/2026-10-17-v3-work-unify-05-result.md 和 docs/tasks/2026-10-18-v3-work-unify-05-cloud-summary.json。
- 下一步：V3-LINKSET-01 先实现 LinkSet 模型、能力协商、选择/记账接口和单测；不接入默认传输、不做十链路性能实验。


## 2026-10-18 V3-LINKSET-01 首个实现切片

- 独立分支 codex/V3-LINKSET-01，输入 4e2900842b7608f757d8c95c24ad62e6011000c0。
- 新增 LinkSet 模型、每链路运行态、能力协商、确定性负载选择和 reserve/release 记账；不接入默认传输。
- 5/5 LinkSet 单测、326/326 完整单元测试、367/367 CTest 通过；证据 /tmp/cpnetflux-runs/V3-LINKSET-01-20261018-ctest-final.log。
- 后续需解决控制面字段兼容、8 条通道限制、安全协商降级和多链路故障 reclaim/drain/cut。


## 2026-10-18 V3-LINKSET-01 GlobalScheduler 兼容接入

- GlobalScheduler 增加单链路 LinkSet 兼容视图，保留既有 LinkManager 和默认调度行为。
- LinkSet 定向测试 6/6，完整单元测试 327/327，CTest 368/368 通过。
- 证据：/tmp/cpnetflux-runs/V3-LINKSET-01-20261018-ctest-adapter.log。
- 下一步：定义控制面能力协商摘要和安全上限策略，再接入真实协商；当前不改变默认传输上限。


# 03 核心实现：V3-LINKSET-01 控制面承载切片回执

日期：2026-10-18。角色：03 核心实现；交付对象：00 总指挥。
状态：控制面切片及深圳↔上海双向显式协商传输已完成验证；真实多物理链路数据面仍待后续任务。

- 输入提交：19f0deb4e3706909c08c0b51ebe2afd5651e1064；工作树：/root/projects/CPNetFlux-worktrees/v3-linkset-01；分支：codex/V3-LINKSET-01。
- 实际解决：把 LinkSet 能力从内部摘要接入现有 XCPNETFLUX V2 persistent-tree 控制请求。显式请求携带 LINKS=<N> LINK_CAPS=<bits>，服务端按配置、现有 8 条 persistent 通道安全上限和能力位做协商；不支持时返回 504，客户端不静默降级。未带字段的旧请求继续使用默认单链路。
- 修改文件：include/cpnetflux/config/tree_transfer_options.h、include/cpnetflux/core/scheduler/link_set.h、include/cpnetflux/protocol/control/control_options.h、src/config/tree_transfer_options.cpp、src/core/io/tree_transfer_client.cpp、src/core/scheduler/link_set.cpp、src/protocol/control/control_options.cpp、src/protocol/control/control_server.cpp、tests/unit/control_options_test.cpp、tests/unit/scheduler_link_set_test.cpp、tests/unit/tree_transfer_options_test.cpp，以及任务/BOARD/本回执。
- TDD 证据：新增控制字段行为测试先运行并按预期 linker red，记录于 /tmp/cpnetflux-runs/V3-LINKSET-01-20261018-control-red.log；实现后目标构建成功，记录于 ...-control-green-build.log。
- 验收命令：cmake --build /tmp/cpnetflux-runs/V3-LINKSET-01-20261018-build --parallel 8 --target cpnetflux_unit_tests cpnetflux-gridftp-server cpnetflux-tree-upload-client cpnetflux-tree-download-client；cpnetflux_unit_tests --gtest_filter=SchedulerLinkSetTest.*:TreeTransferOptionsTest.ParsesExplicitLinkSetNegotiationOptions:ControlOptionsTest.ParsesExplicitLinkSetCapabilities；cpnetflux_unit_tests；CPNETFLUX_TEST_TOKEN=V3_W05_TEST_TOKEN ctest --test-dir /tmp/cpnetflux-runs/V3-LINKSET-01-20261018-build --output-on-failure -j 8。
- 输出证据：定向 12/12、完整单元 333/333、CTest 374/374（0 failed）；日志分别为 /tmp/cpnetflux-runs/V3-LINKSET-01-20261018-control-targeted.log、...-control-unit.log、...-control-ctest.log。
- 实际协议验证：loopback 显式接受、默认兼容与拒绝均通过。深圳→上海 2 文件/134,971 bytes、上海→深圳 2 文件/102,265 bytes 的源和目的 SHA-256 逐项一致；跨机默认能力服务端对 links=2/caps=1 明确拒绝且无目标文件。两端证据位于 /tmp/cpnetflux-runs/V3-LINKSET-01-20261018-cross；反向初次使用 25300 被网络过滤，改用空闲 24300 后通过，未触碰既有 GridFTP 服务。
- 下一步交接：实现 V3-MULTILINK-RANGE-01 的真实物理链路建立、range/WorkLease 到 LinkSet 映射、进度和 drain/cut；先补旧协议端拒绝测试，再重复深圳↔上海双向哈希验收。当前跨机测试只证明控制面协商及既有数据面的端到端完整性，不证明 LINKS=2 对应两个物理数据链路。

- GitHub 备份记录：控制面代码提交 `c92d101486e6defefea2104c773093b2f45ae2c0` 已推送；当时 `git ls-remote origin refs/heads/codex/V3-LINKSET-01` 回读同 SHA。加入双向云端实验结果后的文档提交 `512a2da2bee6962b037320af0cdd72a31abe3e98` 也已推送并回读同 SHA；当前 HEAD/远端状态以最新回执提交及最终核验为准。
## 2026-10-18 V3-MULTILINK-RANGE-01 真实数据面切片回执

日期：2026-10-18。角色：03 核心实现。状态：done。

- 输入提交：49eb3eea84c4455d4c407f07fe5ec1070e764825（V3-LINKSET-01，已推送）；实现分支 `codex/V3-MULTILINK-RANGE-01`；worktree `/root/projects/CPNetFlux-worktrees/v3-multilink-range-01`。
- 本次实际解决：V3-LINKSET-01 已经能协商 LINKS/LINK_CAPS，但 persistent tree 仍按 fileParallelism 建数据会话，range 没有稳定 link_id，LINKS=2 可能只有一个数据连接。本轮把请求链路数接入 persistent data plane，建立至少一个独立 TCP session/lane，给 worker 和 WorkLease 稳定 link ID，按 range/整文件做 lane affinity，并在首选 lane 不再 accepting 时允许健康 worker 接管 ready/retry item；摘要增加 observed data connection 和逐 lane range completion 计数。
- 修改文件：`include/cpnetflux/core/io/dynamic_file_queue.h`、`src/core/io/dynamic_file_queue.cpp`、`include/cpnetflux/core/io/persistent_tree_transfer.h`、`src/core/io/persistent_tree_transfer.cpp`、`src/core/io/tree_transfer_client.cpp`、`tests/unit/dynamic_file_queue_test.cpp`，以及本任务单、BOARD 和本回执。
- TDD/门禁：先以新 queue 行为测试得到预期编译失败，证据 `/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-red.log`；实现后全量构建通过。最终单元测试 334 项中 333 通过、1 项因 io_uring 条件不可用跳过；CTest 375/375，0 failed，1 skipped。最终日志：`/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-build-final-verification.log`、`/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-unit-final-verification.log`、`/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-ctest-final-verification.log`。
- 云端实测：资源复核显示深圳 / 可用 66 GiB、可用内存 2.5 GiB，上海 / 可用 81 GiB、可用内存 2.8 GiB，均为 2 CPU；任务端口在启动前确认空闲。深圳→上海和上海→深圳各传 80 MiB，均 `result=pass`、`data_connect_count=2`、accepted links=2、lane IDs=[link0,link1]、range completions=[2,2]。两方向源/目的 SHA-256 分别为 `b7e162d1c71e675c00834092a22150b1506a7d70837ec7e3d2b88ea8cbe8cd79` 与 `ac20c938f665a81df00c80b06d3169125ce07efa2694139024e425df2e20f5b5`；摘要位于两端 `/tmp/cpnetflux-runs/V3-MULTILINK-RANGE-01-cross/logs/upload-{shenzhen,shanghai}.json`，manifest 均为 `completed_ranges=0-83886080`。
- 证据边界：当前证明的是两个独立 TCP data session、逻辑 lane/range 亲和和端到端完整性，不是不同物理 NIC/交换路径或性能提升。首次反向尝试使用被过滤端口超时，改用已验证可达的任务端口后通过；临时服务和端口已回收。
- 下一步：V3-FLOWCUT-01 / 剩余 V3-MULTILINK-RANGE，解决 endpoint/local-bind/path identity、global scheduler 的 LinkSet adapter、weighted runtime selection、lane drain/cut、失败 lane reclaim、stale/duplicate epoch 证据，并运行 1/2/4/8/10-link 双向矩阵后再评估稳定性和性能。

## 2026-10-18 V3-FLOWCUT-01 GlobalScheduler/LinkSet 接入回执

状态：done。输入 46b3bb70bb5acbeeec4f04bd757b15b89d9f2b64；实现分支 codex/V3-FLOWCUT-01，worktree /root/projects/CPNetFlux-worktrees/v3-multilink-range-01。

- 实际解决：persistent tree 原先拒绝 global scheduler + LINKS>1；现在 GlobalScheduler 可创建 N 条逻辑 lane，DynamicFileQueue 由 LinkSet 按健康/负载和范围亲和选择 lane；drain、disconnect、recover 与 connection epoch 更新同步到 LinkSet。
- 另补等负载范围稳定破平局，避免所有 claim 都落在 link0。单链路仍为默认兼容。
- 验收：全量 CMake 构建成功；337 个单元测试 336 通过、1 个 io_uring 条件跳过；CTest 378/378，0 失败、1 跳过。日志位于 /tmp/cpnetflux-runs/V3-FLOWCUT-01-build-final.log、V3-FLOWCUT-01-unit-final.log、V3-FLOWCUT-01-ctest-final.log。
- 深圳→上海与上海→深圳各完成 80 MiB persistent_tree/global/2-link 传输；双方摘要均 LINKS requested/accepted=2/2、逐 lane range=[2,2]，源/目的 SHA-256 一致。云端日志 JSON 位于两端 /tmp/cpnetflux-runs/V3-FLOWCUT-01-cross/logs/。
- 临时 CPNetFlux 服务、进程及测试端口在两台服务器已停止并核实为空闲。
- 边界：这是逻辑 lane/调度的端到端验证，不证明不同物理 NIC/路径或物理吞吐提升。
- 下一步 V3-LANE-FAILOVER-01：补足单 lane 失效后 unfinished range 定向回收、健康 lane 接管、旧 epoch 迟到结果拒绝，以及恢复 lane 递增 epoch 重新加入；通过可控 lane 断连在云端双向复验。用户要求把物理网卡/不同物理链路配置验证留在代码侧工作完成后。

- GitHub 备份：实现提交 edf92bf4081347ea9ad60f196d741499f99b222e 已推送 origin/codex/V3-FLOWCUT-01；推送后 refs/heads/codex/V3-FLOWCUT-01 回读同 SHA。后续仅文档记录的提交另行推送。
