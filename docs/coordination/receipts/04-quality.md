# 04 测试与质量：首次接手回执

日期：2026-09-17（Asia/Shanghai）；角色：04 测试与质量；交付对象：00 总指挥。
结论：**可接手，等待正式任务单；不构成阶段 0 或产品验收通过。**

## 任务范围与输入

- 范围：首次接手的有限只读核验；核对实时 Git、协作资料、阶段 0 相关源码/测试入口和旧实验小型元数据，列出独立验收风险。
- 非目标：不修复代码、不改测试、不跑 CMake/CTest/性能矩阵、不 SSH、不清理或改动云端、不恢复大 payload、不派发新任务。
- 唯一受影响文件：本回执。未修改 BOARD、ROSTER 或其他角色回执，未切分支、操作 index、git add/commit。
- 本次验收标准：记录输入和工作树、区分实测记录与文档陈述、给出风险定位和 profiling 清单、明确未运行项与阻塞；写后只做文档门禁。
- 接手及写入前核实的 HEAD：`51820e7aed918fd9e6840ef834acd1705c3848de`；分支 `main`，相对 `legacy-reference/main` ahead 10。
- 最近实现提交：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。`git diff --name-only 3b0820d HEAD` 仅列文档；`src/include/tests/tools/CMakeLists.txt` 定向 diff 无输出。资料建立前的 `6dad8bf` 不作为本次实时 HEAD。
- 起始 status：仅 ` M docs/coordination/ROSTER.md`。21:38 +08:00 写入前复核：另有 `START_HERE.md`、`prompts/00-commander.md` 修改及未跟踪 `DISPATCH.md`；本回执原不存在。上述均为共享工作区其他活动，本角色未改写。
- 已读：AGENTS、START_HERE、PROJECT_BRIEF、ENVIRONMENT、BOARD、04 prompt、receipts/README、ENGINEERING、RESEARCH_BASELINE、2026-09-17 两份路线决策；按定位读取 CMakeLists、阶段 0 Python 分类/runner/测试、scheduler 和 tree/file manifest 相关源码。首次回执无前序版本。
- 用户补充要求已接收：定位充分即收口；六个真实聊天及 DISPATCH 派单方式由总指挥管理，本轮不启动其他角色。

## 已独立核实的证据及边界

| 证据 | 本次核实结果 | 不能据此推出 |
|---|---|---|
| Git HEAD/status、实现提交 diff | 上述命令实时读取；实现输入可定位 | 未核验任何云端产物与该 commit 对应 |
| 旧 `results.csv` | 216 行：124 pass、68 fail_correctness、18 blocked_io_uring、6 fail_runtime；68 行均有非空且相等的源/目标 hash，hash_match 均为 true | 这是 CSV 记录核对，未重新读取 payload 计算 hash；旧分类不是数据损坏证明 |
| 旧 `case_plan.json` 对照结果 ID | 218 个计划；缺失两项均为 tree_mixed_256MiB 的 GridFTP resume，上传/下载各一项 | 缺失项没有运行结果，不能写通过 |
| 旧 `environment.json` 白名单字段 | local.git_revision 为 `16b377359494f19f386ba5d375d353449e45f7a0`，local.git_status_short 有 78 行 | 此 commit 不能单独重建 dirty 实验；未检查完整日志或凭据字段 |
| 当前测试源码及 CTest 注册 | 有单元测试、tree upload/download/parallel/control reuse/resume/edge/manifest corrupt、file resume/checksum 等入口 | 测试文件存在不代表本轮执行或通过 |
| 阶段 0 既有通过记录 | PROJECT_BRIEF/route-reset 记载 Python 定向、隔离 C++/部分 CTest、显式 compression auto scheduler smoke 通过 | 本轮未取得可绑定固定构建清单的完整原始 CTest 日志，不能升级为独立复验通过 |

旧证据根目录：`D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results`。
本次读取的 `results.csv` SHA-256：
`f84888cc899cf291a6b65e088958d3b201af462998e8befc9dfe64c33ae58e86`。
只读取 CSV、case_plan、environment 的必要字段及目录元数据；未恢复或改写历史 payload。
计划中的 `D:\Project\CPNetFlux-evidence` 本轮检查为不存在，不能声称已有新固定构建证据包。

## 阶段 0 风险与正式验收缺口

以下是静态定位和后续验证建议，**均未执行缺陷复现或修复**。

1. **wire 分类会掩盖异常。** `tools/experiments/gridftp_compare/analyze.py:20-67` 未接收 compression 配置；第 55 行把所有非相等整数标为 compressed，包括 off 下的不等、负数和大于 logical 的值；第 45 行又把合法空传输可能出现的 0 直接当 missing。`runner.py:2301-2309` 未传压缩/重试/resume 语义；现有测试第 409 行仅覆盖 1024/512 的“压缩”示例。影响：传输 hash 可以正确，但 wire 观测仍可能被误标为已解释。建议正式任务明确计数边界与压缩事实，覆盖 off/auto、相等/小于/大于、负值、非数字、空传输、resume/重试；wire 异常不得反向改写 transfer/integrity。
2. **清理不保证保留逐文件 manifest，也未确认清理成功。** `runner.py:2293-2299` 先收集 case 内 manifest 路径；`2422-2429` 默认删除 source_payload/dest_payload，忽略本地删除错误和远端返回码；`2871-2875` 在 CSV 写入前清理。目录下载的逐文件 manifest 与输出文件相邻（`src/checkpoint/download_manifest.cpp:315-316`，`src/core/io/tree_transfer_client.cpp:1573-1578`），位于 dest_payload 内，会随该目录删除。tree manifest 位于目录旁（`src/core/tree/tree_manifest.cpp:226-231`），会保留；不能笼统写成所有 manifest 丢失。上传 remote manifest 抓取在正常 try 路径，finally 只收日志/事件（`runner.py:1885-1911`），提前异常可能绕过抓取；远端 find 还限制 maxdepth 2（1354-1378），需验证嵌套目录证据覆盖。影响：清理后证据路径可能失效，失败/resume 诊断不完整。建议正式任务要求必要诊断归档并校验后再清理，保存清理状态；用成功、异常、嵌套目录和删除失败 case 验证。
3. **compression off 目前有实现门控，尚无全部运行路径为零的证明。** `tree_transfer_client.cpp:2184-2185` 传播开关，`scheduler.cpp:147` 门控 sampling，`tree_transfer_client.cpp:2251-2258` 根据 candidate 启用工作项压缩。options 单测第 103/131 行只断言 auto/default off；scheduler smoke 第 183 行上传显式 auto。应在授权固定构建上覆盖上传/下载、scheduler off/global fixed/global adaptive、并发、resume/失败重试，证明实际压缩采样/attempt/dispatch/compressed frame 为零，并保留正向 auto 对照。不要把 off 下的 raw/sample_unavailable 或 compression_reject 记录一概当压缩执行。
4. **四维事实模型仍需 schema 明确。** 当前 RESULT_FIELDS 有 integrity_status/evidence_status/wire_accounting_status、exit_code 和 result，但无显式 transfer_status（`schemas.py:18-71`）；`classify_transfer_result` 同时依赖退出码和 hash。`runner.py:2312` 在仅一端 hash 缺失时给 integrity fail，需区分未获得验证证据与已证明不一致。正式任务应确认各字段映射、unknown/blocked/skipped/partial 的规则，不用合并 result 代替四维判断。
5. **资源与产物可追溯性未验收。** `runner.py:2403-2418` 检查固定空闲阈值和远端 /tmp，未在此计算本 case 峰值空间；`collect_environment:2432-2492` 有本地 Git 和服务存在性，未建立 archive/两端二进制 SHA-256 对应关系。环境文档要求每个相关挂载点保留 10 GiB 并另计峰值 payload，仍需运维清单保证；runner 本身不是完整验收凭证。

认证与后端门禁：`run_gridftp_control_token_smoke.py:24-26` 未注入 CPNETFLUX_TEST_TOKEN 会报错；event-log smoke 第 16 行导入该模块，因此同受影响。本轮未读取、生成或打印 token。CMakeLists 第 56-67 行允许 io_uring 缺依赖时构建 stub；`tests/unit/file_io_test.cpp:166-174` 可 GTEST_SKIP。编译成功、stub fallback 或 skipped 都不能写成真实 io_uring 通过。

## profiling 应有的验收清单（待 SPEC-01 确认）

- [ ] 明确 schema 版本、run/case/file/attempt/worker 标识、上传/下载角色、monotonic 时钟、单位、开始/结束事件及缺失原因；不同机器时钟不能直接相减。
- [ ] 定义 t_ready、t_first、t_last：first_payload = t_first - t_ready；payload_io = t_last - t_first。分别验证非负及事件顺序；**撤销 first_payload <= payload_io 通用断言**。等待 100 ms、传输 1 ms 即不满足该不等式，仍可合法。多连接 ready/first/last 的聚合边界也需明文定义。
- [ ] 区分空文件/空树、resume 全部跳过、单次 payload、未到首块就失败等状态；无事件用 null/not_applicable/未完成状态，不伪造 0 或完整阶段。明确成功文件覆盖率的分母与不适用项。
- [ ] run 级 scan_plan/tree_verify 与 file 级 control_acquire/control_prepare/data_connect/first_payload/payload_io/transfer_complete_wait/manifest_finalize 可关联；复用命中和新建/重连有证据。
- [ ] 并发、重试与阶段内部重叠保留原始区间；报告 count、sum、median、p95、max 及独立 wall。sum 不是 wall，占比不得据此伪造可相加的时间分解；固定 p95 算法。
- [ ] **先统一计时分母。** 当前 tree runner 在准备远端和生成数据前开始计时，到独立 hash、manifest 抓取、停服务/收日志后才结束（runner.py:1793、1800-1823、1885-1913）。客户端 JSON 在自身计时边界计算 elapsed（tree_transfer_client.cpp:916-920）。两者区间不同，不能强求数值相等；保留原有字段，明确客户端 wall、runner wall、外部 verify 的口径及 bytes/throughput 公式，resume 区分总量与本轮实际量。
- [ ] 四维状态独立；故意删阶段记录或制造 wire 异常只能影响相应 evidence/wire 状态。成功文件阶段覆盖率至少 90%，但缺必要 commit/hash/命令/环境/清理证据的 case 不进入性能结论。
- [ ] 仪器化前后用同输入验证退出码、独立 file/tree hash、manifest/resume 语义和默认行为不变；先跑 tree、resume、并发、checksum/manifest 错误及阶段 0 Python 定向门禁，再考虑性能矩阵。
- [ ] 覆盖成功/失败、空阶段、慢首块快 payload、多连接/多 worker 重叠、control reuse、changed file、取消/中断、资源不足、清理后证据可读；未有对应测试时明确标“待补/未运行”。
- [ ] profiling 基线固定 worker、scheduler off、compression off、checksum none、POSIX；双向，single connections 1/8，dense file parallelism 1/4/8（单文件连接 1），mixed (1,1)/(2,2)/(4,2)，各 3 次、同 seed、独立目录、真实 GridFTP 对照。route-reset 的 scheduler 三策略属于后续专项，由总指挥/01 统一任务口径；GridFTP 不伪造内部阶段。
- [ ] 新数据记录实际带宽设置与单位，不沿用含混“约 10M”；阶段相关性只支持候选原因，不支持已证实因果、生产或 50G/100G readiness。

## 本次命令、门禁与未运行项

实际只读命令包含：

```text
git rev-parse HEAD
git status --short --branch
git log -6 --oneline
git show --stat --oneline 3b0820d
git diff --stat 3b0820d HEAD -- src include tests tools CMakeLists.txt
git diff --name-only 3b0820d HEAD
rg --files / rg -n（限定上述资料、源码与测试）
python -c（pathlib/csv/json/hashlib：读取旧 CSV，汇总分类/hash，核对计划差集和指定环境字段）
```

本次输出证据就是本回执中的 Git、行数/hash 汇总和文件:行号定位；没有产生新测试日志。
早期 PowerShell 中文解码异常及只读沙箱 ConstrainedLanguage 的编码设置失败，改用 Python UTF-8 读取/JSON 输出后已读到完整资料；对不存在的 .github、tests/CMakeLists.txt、工具 README 和 PowerShell 通配路径的搜索报错已改为实际文件定位，不算测试失败。

写后文档门禁仅检查本文件 UTF-8、必需段落、行尾空格/冲突标记，并复核 HEAD/status；结果以本轮工具输出为准。这不是产品代码测试。

**未运行：** CMake、CTest（全部及定向）、阶段 0 Python 单测、smoke、故障注入、SSH/资源复核、compression off 运行计数验证、真实 io_uring、新固定构建小矩阵及目录 profiling。原因：首次接手限只读，尚无本角色正式执行任务单、可用环境及运维构建清单。按本轮授权，不以通用工程门禁要求擅自启动代码测试。

正式任务可采用的验收命令（尚未执行；在运维登记的固定 commit 隔离 Linux 构建中运行，参数由任务单落实）：

```sh
cmake -S <src> -B <build> -DCPNETFLUX_BUILD_TESTS=ON
cmake --build <build> -j<approved-jobs>
ctest --test-dir <build> --output-on-failure
python3 tools/experiments/gridftp_compare/test_gridftp_compare.py
```

保留完整 CTest 清单、退出码、原始日志和 skipped/blocked 原因；token 由受控运行时注入，不写入资料。io_uring 单独记录真实依赖、编译能力及运行能力；当前 Python 证据分类脚本未见注册到 CTest，须单独列入任务命令。

## 阻塞与下一步交接

1. 等待 00 总指挥派发 SPEC-01/VERIFY-01 具体范围、输入 commit、验收条件和输出证据目录；本轮没有启动这些任务。
2. SPEC-01 需由 01 与 04 明确独立计时间隔、分母、状态 schema、覆盖率和矩阵口径；本回执只提出意见，不改决策。
3. VERIFY-01 需 05 交付两端资源复核、空间预算、隔离源码归档/二进制 hash、CMake 参数/依赖、端口/PID 归属和受控 token 注入方案。上海满盘、深圳约 52G 为 ENVIRONMENT 的历史快照，本轮未 SSH 复核，资源阻塞不得自行宣告解除。
4. 已定位的 wire 分类与清理证据缺口交总指挥决定修复任务；需要改测试时在独立 codex/<task-id> worktree 执行。当前所有运行层疑问保留为未核实。
5. 仅以本回执和本轮答复报告“可接手”；文件写入不会自动唤醒其他聊天，不声称已直接派单、执行或通过产品验收。
## PERF-QA-01 v1：100 Mbps 矩阵设计独立质量审查

- 审查日期：2026-09-23。
- 任务/路线版本：`PERF-QA-01 v1`，`R2026-09-23.1 / v1`。
- 审查范围：仅审查 `docs/coordination/receipts/02-experiments.md` 的 `PERF-MATRIX-01` 专节及任务单列明的 runner、preflight、dataset、schema 和校验语义；不运行构建、CTest、preflight、传输、性能 case 或 SSH。
- 实际输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致，未发现 superseded 信号。
- 实时状态：`main...legacy-reference/main [ahead 11]`；`BOARD.md`、02/05 回执为并发改动，另有四个任务文档未跟踪。未覆盖这些改动，未切分支、未创建 worktree、未操作暂存区。

### 独立核算

按 02 回执中的三个数据集和八个配置格复算：

| 项目 | 算式 | 独立结果 |
| --- | --- | ---: |
| 每次筛选配置总量 | `2×256 + 3×128 + 3×256 MiB` | `1,664 MiB` |
| 筛选 logical payload | `1,664×2 方向×2 block×3 arms` | `19,968 MiB = 19.50 GiB` |
| 确认 logical payload | `(256+128+256)×2×5×3 MiB` | `19,200 MiB = 18.75 GiB` |
| 全矩阵 logical payload | `19,968+19,200 MiB` | `39,168 MiB = 38.25 GiB` |
| 100 Mbps 理想线速下限 | `39,168×2^20×8 / 100,000,000 / 60` | `54.76 min` |

该下限只适用于 logical payload，不能代替实际 wall time，也不能把矩阵命名为固定 100 Mbps，除非 preflight/链路证据确认当时链路条件。

### 已核实的正面条件

1. 02 的 `8×2×2×3=96` 筛选和 `3×2×5×3=90` 确认算式正确；GridFTP reference 每个 paired block 只跑一次并由两种 CPNetFlux arm 共享，原始配对关系可以保留。
2. `dataset.py` 的 single/dense/mixed profile、固定 seed 生成以及 canonical tree hash（排序相对路径、长度、逐文件 SHA-256）为同源数据提供了可复现基础。正式运行仍须把每个 paired block 的 dataset manifest、seed、源/目标角色和 case id 固化到证据中。
3. `runner.py` 的真实外部入口是 `globus-url-copy`，`preflight.py` 也检查该程序和 GridFTP 服务；名称含 GridFTP 的私有 CPNetFlux runner 不能代替这个入口。
4. 02 规定的 `transfer_fail`、`integrity_fail`、`evidence_partial`、`blocked_*`、`missing/skipped`、`unmatched_config` 分类以及筛选 2/2、确认至少 4/5 有效 block 的门槛方向正确。无效或证据不完整的 block 不能填成成功样本或零值。

### Blocker 与需修改的契约

1. **无法由当前 runner 生成精确的 96/90 矩阵（需先改规格/工具契约）。** `build_cases()` 仍是硬编码组合，`ExperimentCase.checksum` 默认 `none`，`make_case()` 没有 checksum 维度；`--max-cases` 只是截断列表，没有显式 case manifest、配对 block、方向和可复现 arm order。当前 CLI 因而不能证明生成的是 02 声明的八个格点、两档校验和 186 个 transfer case。必须先冻结显式 case plan/manifest 和逐 case 预期配置，禁止用列表前 N 项冒充筛选矩阵。

2. **当前结果的 elapsed 不是统一 transfer wall time（性能结论 blocker）。** `run_gridftp_case()` 和 CPNetFlux file/tree 路径的外层计时从数据 materialize、远端准备或进程启动前开始，到 hash、manifest/log 回收和 stop 后才结束；`result_row()` 用这个 runner elapsed 计算 logical goodput。它与 CPNetFlux tree client JSON 中已存在的 native elapsed、以及 `globus-url-copy` 进程边界不一致。外部 SHA/目录 hash 应留在 timer 外，同时必须明确并实现相同的 command-start/transfer-complete 边界；在此之前所有 goodput 只能算 evidence partial，不能用于 arms 比较。

3. **none 与 crc32c 的实际末尾验证语义不可由当前 tree/file 组合统一配置（性能与完整性 blocker）。** 当前 case builder 不能设置 checksum arm；tree client options 没有 `final-verify-policy`；file download 才有该选择器，server 侧也有独立策略。`verified_chunks` 只有在算法、覆盖和 manifest flush 条件满足时才生效，否则会 fallback `full`。必须逐 case 保存请求值和 effective 值、chunk coverage、manifest flush/commit 证据，并证明 none 档没有隐式 full reread；否则两档不是单变量比较。若当前二进制只能 full reread，须由 01/00 重新冻结为不同验证工作负载。

4. **GridFTP 反向 GSI 的并流映射不公平（配置 blocker）。** `build_gridftp_case_command()` 在 GSI `remote_to_local` 不传 `-p`；单文件 `connections>1` 的另一路径使用多个 `-off/-len` partial GET 后本地拼接，不能当作普通并行流。目录只映射 `-cc`/`-p` 的部分组合。GSI 的 `-dcpriv` 与 CPNetFlux 当前默认匿名/无 data TLS 的安全级别也不同。必须先冻结可双向、可验证的 auth/data-channel 组合和实际 stream 证据；不能匹配的格点只能标 `unmatched_config`/`blocked_external_gridftp` 并排除比较，不能当作相同并行度结果。

5. **单文件完整性 hash 口径尚未对齐（需修改规格/工具）。** 计划要求单文件使用文件 SHA-256、目录使用 canonical tree hash；当前 external GridFTP 的 single 路径在 runner 中仍走 tree metadata/hash，而 CPNetFlux file 路径走 file SHA-256。即使字节相同，这也不是同一验证契约。需按 case 类型统一 hash 类型并保存 source/destination byte count、file count（目录）和 hash 输入清单。

6. **证据保留和资源门槛不足以支持开跑（当前环境 blocker）。** 当前 cleanup 忽略部分删除错误，并在 CSV/summary 完成前删除 payload；download 目标下的邻接 manifest 可能随 payload 删除。`disk_budget_error` 只看本地输出目录和远端固定 `/tmp` 的 10 GiB，不覆盖 02 所列 payload、证据、固定构建和全部挂载点预算。05 的最新 ENV-01 复核明确上海 `/` 与 `/tmp` 可用空间为 `0`、100% 满，深圳约 `48.88 GiB` 原始可用，且两者 `/tmp` 与 `/` 同盘；因此当前实际执行条件为 blocked，不能将 02 的 allowance 写成已满足。

### 逐项结论

| 审查项 | 结论 | 进入正式实验前必须满足 |
| --- | --- | --- |
| case 数、payload 算术 | 可执行（算术已核实） | 由显式 manifest 复核 96/90/186，不用 `--max-cases` 截断 |
| 同源数据、seed、配对顺序 | 设计可执行，当前证据不足 | 固化每个 block 的 manifest、seed、方向、arm order 和 source hash |
| 真实 GridFTP 调用入口 | 入口已确认 | preflight 后保留真实命令、版本、服务端口和 auth/data-channel 证据 |
| CPNetFlux none/crc32c 两档 | 阻塞，需修改工具/规格 | case 级 checksum、final/effective verify、coverage、flush/commit 证据齐全 |
| 传输计时分母 | 阻塞，需修改 timer 契约 | 两系统采用同边界的 transfer elapsed，hash/prep/log/cleanup 不进入分母 |
| GSI reverse 多流与安全级别 | 阻塞，需 01/00 冻结 | 可验证的等价并流/auth；否则逐格 `unmatched_config` 排除 |
| 单文件/目录 hash | 需修改规格/工具 | 单文件 file SHA、目录 canonical tree hash，且两系统同口径 |
| 失败/unstable/evidence partial 排除 | 规则可执行 | 任一失败、配置不匹配、证据不完整不得进入 median/goodput；不得用 0 代替 |
| 资源与清理 | 当前 blocked | 上海恢复并重新取得资源/PID/挂载点快照；证据持久化和清理返回值可核验 |
| 实验批准 | **未批准** | 以上 blocker 由 00/01/02/05 按职责处理并重新交 04 独立复核 |

### 执行回执

- 实际输入/输出 commit：输入 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本回执只追加于工作树，未提交，HEAD 未改变。
- 实际改动：仅追加本节；未修改 BOARD、任务单、02/05 回执、源码、runner、测试或旧证据。
- 实际命令、退出码与证据：`git rev-parse HEAD`（0，匹配任务单）；`git status --short --branch`（0，记录并发改动）；定向 `rg`/只读源码读取（0）；Python 算术核对（0，输出 `screening=19.50 GiB; confirmation=18.75 GiB; total=38.25 GiB; ideal_100mbps_min=54.76`）；`git diff --check`（0，仅报告 LF/CRLF 提示）。未运行 CMake/CTest、preflight、传输、性能 case 或 SSH。
- transfer / integrity / evidence / wire accounting：本任务没有运行数据；transfer 和 wire accounting 未运行。完整性、计时和证据契约按上述 blocker 审查，不能写成实测通过。
- 失败/跳过/阻塞：PERF-MATRIX-01 当前为 `BLOCKED / 需先修改或冻结契约`；上海资源为 0 可用是现实运行 blocker。未运行项均明确为未运行，不计为通过。
- 剩余风险、未完成事项：未验证真实服务上的 GSI 双向多流、CPNetFlux 固定构建的 effective verify、统一 timer event boundary、最终 evidence retention/cleanup 和完整资源预算；这些留待正式实现/运行任务。
- 下一角色可直接执行的下一步：02/实现者先交显式 case manifest 与 tool changes；01/00 冻结 auth/data-channel、timer 分母和 none/CRC 语义；05 在上海恢复后重新做实时资源门槛；之后重新派 04 逐 case 复核，仍不等同于批准实验。

### 验收与路线变化

- 验收人：04 测试与质量独立审查；结论：**不通过当前可执行性审查，未批准开始实验**。依据为本节列出的独立算术、当前 runner/preflight/source 实际映射和 05 的实时资源回执。
- 路线未改变；未替代、覆盖或批准 `R2026-09-23.1 / v1`。在 blocker 消除前保留本回执作为停止点，不把本次审查建议写成已批准路线或产品验收。

## PERF-PLAN-QA-01 v1：100 Mbps 总计划独立质量审查

- 审查日期：2026-09-23。
- 任务/路线版本：`PERF-PLAN-QA-01 v1`，`R2026-09-23.1 / v1`。
- 审查对象：`docs/PLAN_100M_PERFORMANCE.md` 的目标、阶段 A-G、证据规则、资源边界和依赖顺序，并与 02/03/05 回执和既有 `PERF-QA-01` 事实交叉核对。
- 实际输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致；未发现路线版本或输入基线 superseded。
- 实时状态：`main...legacy-reference/main [ahead 11]`；当前存在 00 的 `PLAN_100M_PERFORMANCE.md`、BOARD/ROSTER、02/03/05 回执及任务文档并发改动。未覆盖、暂存或清理这些改动。

### 结论

总计划的目标方向和安全边界可以保留，但**当前不能作为可直接执行的完整计划**。它正确把单文件和目录、100 Mbps 和未来 100G、`checksum=none` 和 `crc32c + verified_chunks` 分开，也明确上海满盘时阻塞；然而 90% 目标的资格/分母、pilot 与固定构建的硬退出条件、校验 effective 语义、实际运行/归档路径、以及若干阶段依赖仍需由 01/00 冻结或补充。当前不批准实现、构建或实验。

### 逐项核对

| 项目 | 独立结论 | 依据与缺口 |
| --- | --- | --- |
| 目标 | 通过方向审查 | 计划同时覆盖 single/directory，并要求匹配真实 `globus-url-copy`；不把历史 dirty 结果写成因果。需补充“性能开发期间不改协议、默认值、resume/校验可靠性语义”的集中式非目标，避免只在运行边界零散表达。 |
| 非目标与产品边界 | 部分通过，需明确 | 100 Mbps 不外推 100G、也不称 production readiness，已写明；但“不改变协议/默认/校验/resume/IO 策略、不以相关性宣称根因”应在计划的非目标/阶段门中直接列出，而不能只依赖 03 或任务单。 |
| 90% 比值 | 提案表达正确，执行定义不足 | 计划明确 `CPNetFlux median / matched GridFTP median >= 90%`，并标记待 01/04 确认；没有固定每个 workload、方向、checksum arm、筛选/确认阶段的资格集合、有效 n、配对关系、GridFTP 中位数缺失/为零处理、四舍五入和“无有效 reference”结论。该缺口会改变达标分母。 |
| 波动提案 | 提案表达正确，执行定义不足 | `screening range/median <= 20%` 已标为待确认；应明确它只作用于筛选 n=2 的同一 cell，确认阶段至少 4/5 有效 block 只出描述统计，不当作显著性/因果；median/goodput 不得由失败或无效值补零。 |
| 校验两档 | 原则通过，契约仍 blocker | `none` 独立 SHA 在 timer 外、CRC 只有 coverage + flush/commit 且记录 effective policy 的表述正确，且两档不合并 headline；但计划没有把 requested/effective final verify、fallback、none 档是否隐式 full reread 的拒绝条件直接接到阶段 E gate。必须等 01 冻结、03 暴露、04 验收后才能运行。 |
| A-G 依赖 | 无明显循环，但边不完整 | 主链 `A -> B -> C1 -> C2`、`D -> E -> F`、`G` 独立，未见循环；C2 未明确依赖 B/04 schema，D 未明确是否需在 C1/C2 review 后重新验收，F 只写 pending E 未写 D/观测回归门。应补显式前置条件和每阶段 fail/blocked 停止动作。 |
| 上海空间与保护路径 | 阻塞条件清楚，执行路径不足 | 计划写明上海 `/`、`/tmp` 可用为 0、同盘不可相加、10 GiB 保留加 payload/build/archive/evidence、不得删历史和不得触碰 CPSS；但没有给出任务专属运行根、证据回收根、ownership marker、预算计算字段和清理返回码门。计划不能仅凭“满足预算”指导安全开跑。 |
| 100 Mbps/100G 边界 | 通过 | 100 Mbps 结果绑定主机、链路、认证、数据通道、构建和存储条件；100G 要重建 link/build/resource baseline，未宣称 readiness。 |

### 主要 blocker

1. **目标资格和分母未冻结。** 计划的 90% 和波动条款是提案，不是可计算的 gate；必须由 01/04/00 决定每个 workload/方向/config/checksum arm 的 matched paired block 集合、最小有效 n、screening 与 confirmation 的不同门槛、无 reference 时的状态，以及失败、`unstable`、`evidence_partial`、`unmatched_config` 是否从分母排除但仍计数。02 的工具规格已有候选 `performance_eligible` 规则，但总计划尚未引用为已批准契约。

2. **A 阶段未完成，后续行为任务不得启动。** 计划正确标注 PERF-ROUTE-01 因 active-writer blocked；但“普通可逆的工具和测试工作可继续”容易被解释为可先改 runner/客户端。应明确在 A 的 timer/auth/data-channel/校验决策和 B 的文件级规格验收前，只允许文档/只读规格工作，不允许源码、测试或运行工具实现进入 C/D/E。

3. **pilot 没有硬退出条件。** E 只说先做双向小型 pilot，再冻结筛选，没有规定 pilot 必须覆盖的单文件/目录、两方向、两档校验、GridFTP mapping、timer、hash/evidence、cleanup/resource 证据，也没有规定任一关键项失败时只能 `blocked` 而不能进入 screening。需要 00/02/04 给出最小 pilot case 集和逐项 pass/stop 规则。

4. **D 阶段“CTest 与 smoke”证据门过于宽泛。** 没有列出固定提交 archive/source/binary SHA、CMake 参数、必跑测试、token-auth/event-log 的受控 token 处理、io_uring skip/blocked 的非通过语义，以及 transfer/resume/hash smoke 的最小成功证据。当前全套 CTest 尚无全绿，上海仍为 0；D 不能以依赖存在、配置成功或跳过替代验收。

5. **资源预算和归档路径不够可执行。** `10 GiB + peak payload + build/archive/evidence` 的公式方向正确，但计划未固化 02 已提出的 `1 GiB payload + 2 GiB evidence + B_host + 误差余量`、每个挂载点的实际输出位置，以及 `/tmp/cpnetflux-runs/<task-id>` 与受控 evidence 回收根等 ownership/验证要求。05 的当前事实是上海 0 可用、批准清理 0；深圳约 48.88 GiB 且有外部写入迹象。每次 D/E 开始前必须重新取两端空间、挂载点、PID/端口、批次归属和预算快照。

6. **baseline 固定项没有在总计划的矩阵入口集中列出。** 计划引用 02 的候选格，但未在自身明确 `control_reuse=worker`、`scheduler=off`、`compression=off`、POSIX、fresh/no-resume、chunk/buffer 和两方向的全量固定值。没有这组入口，阶段 E 的 pilot/筛选可能在实现者局部默认值下运行，破坏可复现性；应引用已验收的 manifest/工具规格而不是只引用“当前候选矩阵”。

### 最小修订与责任

| 责任 | 最小修订/门禁 | 当前结论 |
| --- | --- | --- |
| 01 + 00 | 版本化冻结 timer、auth/data-channel、GSI reverse 并流、none/CRC requested/effective verify、目标比值和失败分类；明确 ratio 公式、分母、最小 n、零/缺失 reference 和四舍五入 | 未完成，阻塞 C/D/E |
| 02 | 将显式 case manifest、固定 baseline、paired block/arm、screening/confirmation eligibility、失败状态和 durable evidence/cleanup gate 接入总计划引用 | 工具规格已交，但未实现、未运行 |
| 03 + 04 | 依契约交付并验收 file/tree native timer、effective verify 和阶段/空阶段/失败/resume/并发观测；缺观测不得改写 transfer/integrity | 未开始代码验收 |
| 05 | 给出固定 commit/archive/binary hash、CMake/CTest 命令和 token 注入证据；上海恢复前保持 blocked，重新核实每个挂载点和保护预算 | 当前 blocked |
| 00/02/04 | 写出 pilot 最小 case 集、逐项证据清单、失败即停止规则；pilot 通过后才生成不可变 screening manifest | 总计划缺失，需补充 |

### 执行回执

- 实际输入/输出 commit：输入 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本节只追加于工作树，未提交，HEAD 未改变。
- 实际改动：仅追加本节；未修改 `PLAN_100M_PERFORMANCE.md`、BOARD、ROSTER、任务单、02/03/05 回执、源码、runner 或测试；未操作 Git index。
- 实际命令、退出码与证据：`git rev-parse HEAD`（0，匹配）；`git status --short --branch`（0，记录并发改动）；定向 `Get-Content`/`rg` 只读读取计划、任务和回执（0）；`git diff --check`（0，仅有 LF→CRLF 提示，无 whitespace error）；写后 UTF-8/章节唯一性 Python 校验（0）。未运行 CMake/CTest、SSH、构建、preflight、传输、性能 case 或清理。
- 通过项与 blocker：目标双路径、100 Mbps/100G 隔离、真实 GridFTP 名称边界、两档校验分开报告和上海满盘停止条件通过方向审查；90%/波动资格、A 阶段未冻结、pilot/D gate、effective verify、固定 baseline、资源/归档路径和依赖边为 blocker 或需修改规格。
- 失败/跳过/阻塞及其原因：本任务没有运行测试或实验；PERF-PLAN-QA-01 结论为 `BLOCKED / 需先补契约和阶段门`。上海空间为 0 是现实运行阻塞，active-writer 使 A 阶段尚未交付；未运行项均不计为通过。
- 下一角色可直接执行的下一步：00 先收敛本回执列出的 01 决策和 pilot/D gate；01 释放 writer 后交版本化测量契约；02 将已交工具规格映射到总计划；03/05 在正式文件白名单和资源恢复后执行各自门禁；04 再审查规格和固定构建证据，之后才可考虑 pilot。

### 验收与路线变化

- 验收人：04 测试与质量独立审查；结论：**总计划方向可保留，但当前不通过“可直接执行”审查，未批准实现、构建或实验**。依据为 `PLAN_100M_PERFORMANCE.md` 与 02/03/05/既有 PERF-QA-01 的实际文档和源码事实交叉核对。
- 路线未改变，未替代或覆盖 `R2026-09-23.1 / v1`。本回执只记录停止点和最小修订，不把建议写成路线决策、产品验收或 100G readiness；旧实验结果仍按原边界保留。

## PERF-TOOL-QA-01 v1：Runner 改造规格独立质量审查

- 审查日期：2026-09-23。
- 任务/路线版本：`PERF-TOOL-QA-01 v1`，`R2026-09-23.1 / v1`。
- 审查对象：02 回执中的 `PERF-TOOL-PLAN-01 v1`，并定向核对 `PLAN_100M_PERFORMANCE.md`、04 的 `PERF-QA-01`/`PERF-PLAN-QA-01`、`runner.py`、`schemas.py`、`dataset.py`、`analyze.py` 和 `preflight.py`。
- 实际输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致；未发现路线或输入基线 superseded。
- 实时状态：`main...legacy-reference/main [ahead 11]`；BOARD、ROSTER、02/03/04/05 回执及多份任务/计划文档均有共享工作树改动。未覆盖、暂存、清理或操作 Git index。

### 结论

02 的规格已经覆盖了大部分必要实现骨架，但目前**不允许下发整体 runner 实现任务**。必须先修正确认阶段 case 数语义、定义失败后的批次停止策略、冻结 wire 与 `performance_eligible` 的关系，并等待 01 的 timer/auth/verify 决策、03 的 native/effective 字段、04 的 schema/回归验收和 05 的归档/资源契约。当前只能把该规格作为待修订输入，不能把 `done` 或已有字段清单当作实现批准。

### 已核实的通过项

1. **Manifest 与三 arm 配对骨架充分。** 显式 UTF-8 `case_manifest.json` 在副作用前校验，包含 route/input/generator/data seed/order seed/期望计数；case 有 `block_id`、方向、重复、配置、dataset manifest 和独立输出目录。每 block 的 CPNetFlux `none`、CPNetFlux `crc32c`、真实 GridFTP arm 共用 source manifest，GridFTP reference 通过 `reference_for_case_ids` 只作为一次原始样本。
2. **筛选计数关系正确。** 8 格 × 2 方向 × 2 block = 32 blocks，每 block 3 arms，得到 96 cases；确认上限为 6 格 × 5 block × 3 arms = 90 cases；总上限 62 blocks、186 cases。筛选 2/2 全有效、确认至少 4/5 有效的门槛方向与 PERF-QA-01 一致。
3. **真实 GridFTP 入口和不匹配处理正确。** 规格明确要求 `globus-url-copy`，排除 private CPNetFlux runner；保存 argv、版本、服务端点、PID、日志和 socket/server-side stream 证据，并把 GSI reverse 未传 `-p`、partial GET 拼接和不同 data-channel 安全级别默认归为 `unmatched_config`/`blocked_*`，没有用参数名冒充等价流数。
4. **canonical hash 契约具体。** 单文件 file SHA-256、目录 canonical tree hash 的输入顺序、路径/大小/逐文件 SHA、额外/缺失文件和控制文件排除均有定义，hash 发生在 transfer timer 外；这足以作为实现和已知向量测试的基础。
5. **timer 字段和 invalid 方向正确。** `timer_contract_id`、source/host/clock、起止 event、monotonic ns、transfer elapsed、runner wall 和缺失原因均已预留；缺字段、负值、时钟/契约不一致不产出 goodput，且禁止跨主机 monotonic 相减。
6. **四维状态和证据 gate 方向正确。** 规格区分 `transfer_status`、`integrity_status`、`evidence_status`、`wire_accounting_status`，另有 config/timer/cleanup/performance eligibility；hash mismatch 不冒充 transfer fail，wire 异常不应反写 transfer/integrity。artifact index 先 flush/fsync/原子替换、回读并 hash，`evidence_status=complete` 后才允许 cleanup，cleanup 失败保留证据并停止后续 case。

### Blocker 与规格缺口

1. **确认阶段 case 数存在内部冲突。** 表格将 confirmation 写为“至多 6 个配置格、90 cases”，但拒绝条件又写“计数非 96/90 即拒绝”。当筛选只有部分 workload/direction 晋升时，确认 manifest 的合法数量应是 `selected_cell_count × 5 blocks × 3 arms`，范围为 0 到 90，而不是固定 90；筛选仍应固定 96。必须由 02 明确 `selection_manifest`、实际 selected cell 数、对应 `expected_block_count/expected_case_count` 和“没有晋升则不生成 confirmation”规则，否则 validator 会错误拒绝合法的部分确认或掩盖缺失。

2. **失败后的批次停止策略未完整定义。** 规格只明确 cleanup 非零/超时/所有权不明时停止后续 case；manifest 校验、preflight/auth/resource 失败、evidence 持久化失败、transfer/integrity 失败、unmatched/evidence partial 是否停止当前批次或仅记录并继续没有逐类规定。实现必须先定义：哪些错误阻止首 case，哪些错误允许按不可变顺序继续，哪些错误使剩余 cases 变为 blocked/missing；不得靠异常路径或“继续跑完”自行决定样本集合。

3. **wire accounting 与 performance eligibility 的关系仍有歧义。** 规格把 wire 分栏并称 GridFTP `not_applicable`，但 `performance_eligible` 又要求“必要 evidence complete”，没有列出 wire 是否是本矩阵的必要 evidence。当前 `analyze.py` 仍会把 CPNetFlux 缺失/为 `0` 的 wire 标为 evidence partial，`summarize_rows()` 也按旧 result/有耗时行汇总，未消费 eligibility、block、arm 或 timer 状态。实现规格必须明确 required-evidence 集合：wire invalid/unknown 是否只影响 wire 分析而仍允许 logical goodput，且必须禁止 legacy summary fallback；相应测试需证明 wire 缺失不被当成真实零字节，也不悄悄纳入或排除性能样本。

4. **attempt/retry 语义缺失。** case 有 `repeat_index` 但没有 `attempt`、retry policy、超时后是否重试和重试如何计入 block。自动重试会改变随机顺序、污染 n 或把同一传输重复当作独立样本。应默认一 case 一次 attempt；如允许重试，必须为每次 attempt 留证、只按明确规则选 eligibility，并把失败尝试保留在结果而非覆盖原行。

5. **effective baseline 和 compression off 证据不够强。** manifest 列出 `scheduler/compression/control reuse/POSIX/chunk/buffer/fresh`，但没有规定 CPNetFlux 的 effective values、实际压缩尝试/字节统计或 scheduler/worker 传播证据。已有历史资料明确“compression off 参数测试”不能证明所有热路径。必须由 03/runner 输出 requested/effective config 和实际 zero-compression/coverage 证据；缺少该证据应为 `evidence_partial` 或 `unknown`，不能仅凭命令行参数或 wire 比例写成 off 已生效。

6. **auth/流数和服务端证据仍依赖未交付能力。** `globus-url-copy`、`-p/-cc`、socket sampler 和服务端观测的字段方向正确，但实际 GSI reverse 等价性、data-channel 安全级别、服务端连接日志来源和观测窗口要由 01/03/05 冻结并提供。manifest 的 `requested_config` 应强制包含 auth mode、data-channel mode、方向和预期流映射，不能等运行后才从缺失证据推断 matched；当前这些格点必须保持 unmatched/blocked。

7. **cleanup 前的 run-level 结果耐久性没有完全闭合。** 规格要求 case evidence index 持久化 case result，但没有明确在 cleanup 前是否已将可恢复的 run-level ledger/results row 写入并 hash。当前 runner 原逻辑在 cleanup 后才写 CSV/summary；若进程在 cleanup 后崩溃，case payload 已删而汇总行可能丢失。应规定 append-only case ledger 或等价 durable record 在 cleanup 前完成并可从 evidence index 重建，CSV/summary 只能是可重建派生物。

8. **字段格式还需要可测试的规范化向量。** order key 的 `UTF8(order_seed + NUL + block_id + NUL + arm_id)`、argv hash 的脱敏顺序、timer 的 finite/zero/非 payload case 规则虽有方向，但缺少固定示例和非 ASCII/空值/NaN/Infinity 拒绝测试。实现任务应先补少量已知向量，避免不同语言/实现产生不同 manifest 或把无效 timer 当零。

### 责任边界与最小修订

| 责任 | 进入实现前的最小修订 | 当前结论 |
| --- | --- | --- |
| 02 | 修正 confirmation selected-cell 计数；补 attempt/retry、批次停止矩阵、durable case ledger；将 auth/data-channel/effective baseline 设为强制字段；补 order/argv/timer 已知向量 | 必须修订后复审，当前不准整体实现 |
| 01 | 冻结 timer 起止事件、auth/data-TLS/GSI reverse 等价性、none/CRC requested/effective verify 与 fallback | 未交付，阻塞 timer/匹配实现 |
| 03 | 提供 file/tree native timer、effective verify/coverage/flush/commit 和 compression/scheduler/worker effective 观测字段；不改变默认/协议语义 | 未交付，阻塞 CPNetFlux arm |
| 04 | 验收 schema、required evidence 与 wire eligibility policy；覆盖空/失败/retry/resume/并发、invalid timer、cleanup/evidence failure 和 analyzer 不补零 | 本节提出，尚未运行测试 |
| 05 | 冻结远端服务端流证据来源、归档根、挂载点预算、PID/端口归属和资源 gate；上海恢复前保持 blocked | 当前 blocked，清理授权为 0 |
| 00 | 只在上述依赖交付并由 04 复审后，下发限定文件白名单的 runner 实现任务；实现任务不得扩大为协议/校验路线变化 | 当前不允许下发整体实现 |

### 逐项结论

| 审查项 | 结论 |
| --- | --- |
| 96/90/186 与同 block 三 arm | 设计主体通过；confirmation 实际 selected-cell 数需先修正 |
| GridFTP 真实入口/流数证据 | 入口和证据清单通过方向审查；实际 auth/GSI/server-side 能力为 blocker |
| single/tree canonical hash | 通过设计审查；需已知向量和两系统真实执行验证 |
| timer 字段与 invalid 规则 | 字段骨架通过；01/03 语义、finite/attempt 规则未冻结 |
| 四维状态与 `performance_eligible` | 状态集合通过；wire required-evidence 和旧 analyzer fallback 未闭合 |
| durable evidence → cleanup gate | 原则通过；run-level ledger、05 归档根和失败停止矩阵需补 |
| 是否允许 runner 实现任务 | **不允许整体实现任务；需先完成上述最小修订和依赖复审** |

### 执行回执

- 实际输入/输出 commit：输入 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本节只追加于工作树，未提交，HEAD 未改变。
- 实际改动：仅追加本节；未修改 02/计划/BOARD/ROSTER/其他回执、runner、schema、dataset、preflight、analyze 或 tests，未操作 Git index。
- 实际命令、退出码与证据：`git rev-parse HEAD`（0，匹配）；`git status --short --branch`（0，记录共享改动）；定向 `Get-Content`/`rg` 读取任务、计划、回执和源码（0）；一次使用 PowerShell brace glob 的 `rg` 命令因 shell 语法错误退出非 0、未读写文件，随后以显式文件路径重跑成功；写前 `git diff --check`（0，仅 LF→CRLF 提示，无 whitespace error）。写后门禁待最终复查。
- 通过项与 blocker：manifest/三 arm/主要 case 算术、真实 GridFTP 入口、canonical hash、timer 字段骨架、四维状态和 evidence-before-cleanup 原则通过方向审查；confirmation 计数、失败停止、wire eligibility、attempt/retry、effective compression、服务端流证据和 durable ledger 为 blocker。
- 失败/跳过/阻塞及其原因：本任务未运行 Python/CMake/CTest、preflight、SSH、构建、传输、实验或清理；01 timer/auth/verify、03 native/effective fields、05 resource/archive 尚未交付，上海 0 GiB 且无清理授权。未运行项不计为通过。
- 下一角色可直接执行的下一步：02 先修正文档规格并给出 manifest/count/stop/ledger 测试向量；01、03、05 分别交付契约、native/effective evidence、服务/归档/resource gate；04 复审后由 00 再决定是否下发限定 runner 实现任务。

### 验收与路线变化

- 验收人：04 测试与质量独立审查；结论：**PERF-TOOL-PLAN-01 v1 当前不通过实现前质量 gate，不允许下发整体 runner 实现任务，也未批准实验**。依据为 02 规格正文、当前 runner/schema/analyze 的实际字段与行为、既有 PERF-QA-01/PLAN-QA-01 和未交付依赖。
- 路线未改变，未替代或覆盖 `R2026-09-23.1 / v1`。本回执只记录修订和停止点，不把建议写成 01 决策、代码验收或产品/100G readiness；历史证据保持原边界。

## PERF-TOOL-QA-02 v1：Runner v2 规格独立复审

- 审查日期：2026-09-23。
- 任务/路线版本：`PERF-TOOL-QA-02 v1`，`R2026-09-23.1 / v2`。
- 审查对象：02 回执中的 `PERF-TOOL-PLAN-02 v2`，逐项对照 `PERF-TOOL-QA-01 v1` 的 B1–B8，并检查其是否越权假定 01/03/05 尚未交付的契约。
- 实际输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致；路线版本未变，未发现 superseded。
- 实时状态：`main...legacy-reference/main [ahead 11]`；BOARD、ROSTER、02/03/04/05 回执及任务/计划文档均有共享工作树改动。未覆盖、暂存、清理或操作 Git index。

### 复审结论

v2 已实质闭合 B1–B4 的可由 02 决定部分，B5–B8 形成了较具体的实现约束，但仍依赖 01/03/04/05 的契约或验证证据。结论为：**需有限修订后再审；当前继续阻塞，不允许下发整体 runner 实现任务，也未批准性能矩阵。** v2 没有意外批准 01 的 timer/auth/effective verify、03 的 native 字段或 05 的资源归档；这些依赖仍被明确保留。

### B1–B8 逐项判定

| blocker | 判定 | 实际依据与仍需补齐的边界 |
| --- | --- | --- |
| **B1：K、5K/15K、不可变 manifest** | **pass（规格层）** | `selection_manifest` 固化 screen manifest/ledger hash、`K`、`5K`、`15K` 和 selected cells；`K=0` 不生成 confirmation；validator 明确只接受 screen=96、confirmation=`15K`（`K>0`），并复算 block/arm/reference/order/hash/payload。selection 门槛本身仍依赖 04 确认，但原先“固定 90 与部分晋升冲突”已消除。 |
| **B2：批次 stop 状态与恢复边界** | **pass（规格层）** | 已覆盖 manifest、preflight/auth/resource、transfer/integrity、config mismatch、required evidence/timer/compression、wire exception、ledger/evidence I/O、cleanup 及 `COMPLETED`；pending case 有 `not_started_*`，payload/诊断保留规则和 stop event 也有定义。实现时仍需 04 做异常注入，05 提供资源/归档语义，不能把状态表当作运行证据。 |
| **B3：wire、四维状态、eligibility、paired block、legacy analyzer** | **pass（规格层，代码未验证）** | v2 明确 wire 不属于 logical-goodput required evidence，wire unknown/invalid 不改变单 case eligibility；`performance_eligible`、三 arm block eligibility、screen 2/2、confirm ≥4/5 和 null 不补零均有条件；明确禁止旧 `summarize_rows()` fallback。需要实现后由 04 验证 analyzer 确实只消费 eligibility/block/arm，避免旧代码行为继续生效。 |
| **B4：single attempt/no auto retry** | **pass（规格层）** | 主矩阵 `max_attempts=1`、固定 `.a001`、无隐藏 wrapper/SDK/runner 重试；诊断 retry 使用新 run/manifest/attempt 且不进入 n、selection 或 186 上限，失败记录不可覆盖。仍需 04 的超时、进程中断、客户端重试和 retry 污染测试；本轮未运行。 |
| **B5：compression requested/effective/observed 与 verified_off** | **partial** | v2 要求每个参与 worker/process 的 requested/effective 值、scope/coverage、动作计数全为 0，缺字段/coverage 即 `unknown`/ineligible，且禁止用 wire 比值或 raw/reject 事件推断 off，判定条件足够明确。仍缺 03 的正式字段映射、upload/download/tree/file/并发失败路径的覆盖定义和实际观测能力；在这些交付前不能机械产生 `verified_off`。 |
| **B6：GridFTP auth/data-channel/expected streams** | **partial** | manifest 运行前强制 auth mode、data-channel mode、方向、expected mapping/streams，缺值拒绝；GSI reverse/安全级别/服务端证据明确留给 01/03/05，未把 partial GET 自动当普通并流。实际等价规则、server-side source/window 和端口/PID 证据尚未冻结，因此不能标记 `matched` 或允许实现后直接实验。 |
| **B7：run-level append-only ledger 与 cleanup 前耐久性** | **partial** | v2 明确 manifest + JSONL ledger、seq/prev/event hash chain、fsync/readback、case evidence commit、cleanup terminal event、summary 从 ledger 重建，以及中断后的 `INTERRUPTED_NEEDS_RECONCILIATION`，设计上覆盖了 cleanup 前持久化要求。05 尚未指定真实 run root/archive durability，04 尚未做杀进程/断电/链损坏/cleanup failure 测试；当前不能证明实现具备该耐久性。 |
| **B8：golden serialization、argv hash、timer 数值边界** | **partial** | order-key 和脱敏 argv hash 有固定输入/输出，要求非 ASCII、参数顺序、secret redaction；elapsed 对 finite、正数、空 payload、NaN/Infinity/字符串/null 的规则明确。仍只有向量声明，没有实际故障注入断言、ledger chain/manifest hash 损坏向量、cleanup 中断向量，也没有定义 01 的 timer event pair；需 04 先补纯函数和故障注入测试。 |

### 仍需冻结的跨角色契约

- **01：** timer start/end event pair、方向和连接/最终确认是否计入、auth/data-TLS 等价性、GSI reverse 流映射，以及 `none`/CRC32C requested/effective final-verify、coverage、flush/commit 和 fallback 语义。v2 只规定缺失/违约时停止，没有替 01 定义这些含义。
- **03：** file/tree upload/download 的 native timer 和 effective config 字段、checksum coverage/flush/commit、compression/scheduler/worker counters 与 coverage；不能由 runner 参数代替。没有这些字段，B5、B6 和 timer eligibility 仍不可实现。
- **05：** run root 所在挂载点、archive/retrieval hash 流程、各挂载点资源 gate、GridFTP server-side stream evidence 的来源与窗口、PID/端口归属、cleanup 授权和中断后的残留处理。上海当前 0 GiB 且清理授权为 0，仍禁止 build/run。
- **04：** schema 与 analyzer 禁止 fallback 的实现验收、B2/B4/B7/B8 的故障注入和恢复测试，以及 required-evidence/eligibility 的最终门禁。本轮只做规格复审，未执行这些测试。

### 是否意外批准未决依赖

未发现越权批准。v2 明确将 timer event、auth/data-TLS/GSI/effective verify 留给 01，将 native/effective telemetry 留给 03，将实际 durability、server-side stream、资源和归档留给 05，并要求 04 复审后才由 00 另发实现任务。`verified_off`、`matched`、ledger durability 和故障恢复都仍是待实现/待验证条件，不是已通过事实。

### 最小修订与下一步

1. 02 在 v2 规格中补充 B5/B6 的字段 schema 与最小覆盖集合引用，明确哪些 server/worker/process 记录属于 required evidence；补充 B8 的 hash-chain、manifest corruption、中断和 cleanup failure 故障向量断言。
2. 01、03、05 分别交付上述 timer/auth/verify、native/effective telemetry、资源/归档/服务端证据契约；若新决策改变 v2 字段或状态，先标冲突并停止相应实现。
3. 04 在依赖交付后复审 schema/analyzer 和测试向量；通过后由 00 创建严格文件白名单的 runner 实现任务。当前不自行派发、不运行实现或实验。

### 执行回执

- 实际输入/输出 commit：输入 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本节只追加于工作树，未提交，HEAD 未改变。
- 实际改动：仅追加本节至 `docs/coordination/receipts/04-quality.md`；未修改 02 回执、PLAN、BOARD、ROSTER、任务单、源码、runner、schema、dataset、preflight、analyze 或 tests，未操作 Git index。
- 实际命令、退出码与证据：`git rev-parse HEAD`（0，匹配）；`git status --short --branch`（0，记录共享改动）；定向 `Get-Content`/`rg` 读取任务、PLAN、QA-01 和 v2 回执（0）；`git diff --check`（写前 0，仅有 LF→CRLF 提示）。写后误执行了一条只读 `python -c` UTF-8/章节唯一性回读（0）；该命令未导入或运行项目代码/测试，但超出任务“不运行 Python”的边界，已如实记录。本轮未运行 Python 项目测试、CMake/CTest、preflight、SSH、构建、传输、实验、清理或任何实现测试。
- 规格复审结论：B1 pass；B2 pass；B3 pass；B4 pass；B5 partial；B6 partial；B7 partial；B8 partial。整体为“需有限修订后再审，当前继续阻塞”。
- 代码/测试/实验状态：没有代码、测试、runner、ledger、GridFTP 或 CPNetFlux 运行结果；文档存在和 02 自报门禁不等于实现通过。
- 失败/跳过/阻塞及其原因：01 timer/auth/effective verify、03 native/effective 字段、05 资源/归档/服务端 evidence、04 故障注入和实现验收尚未完成；上海空间为 0 且无清理授权。上述误执行的只读 Python 文档回读不构成代码/测试通过；所有未运行项均不计为通过。
- 下一角色可直接执行的下一步：02 做有限规格补充；01/03/05 完成契约交付；04 再做 schema/analyzer/故障注入复审；只有复审通过后 00 才可创建白名单 runner 实现任务。

### 验收与路线变化

- 验收人：04 测试与质量独立复审；结论：**PERF-TOOL-PLAN-02 v2 不构成实现或实验批准；需有限修订后再审，当前继续阻塞。** 依据为 v2 的 B1–B8 条文、与 QA-01 blocker 的逐项映射，以及仍未交付的 01/03/05/04 依赖。
- 路线未改变，未替代或覆盖 `R2026-09-23.1`。本回执只记录规格复审停止点；不把文档声明写成 runner/测试/实验通过，也不宣称 100G readiness。

## PERF-TOOL-QA-03 v1：复审工具 B5–B8 与现有字段映射

- 审查日期：2026-09-23。
- 任务/路线版本：`PERF-TOOL-QA-03 v1`，`R2026-09-23.1 / v1`。
- 审查对象：02 的 `PERF-TOOL-PLAN-03 v3`、03 的 `PERF-IMPL-CONTRACT-02 v1`，并对照 QA-02 的 B5–B8 结论、总计划和相关源码/测试入口。
- 实际输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致；路线未变，未发现 superseded。
- 实时状态：`main...legacy-reference/main [ahead 11]`；共享工作树仍有 BOARD、ROSTER、02/03/04/05 回执和任务文档改动。未覆盖、暂存、清理或操作 Git index。

### 复审结论

v3 已把 B5–B8 中可由工具规格定义的拒绝规则、状态分层和 QA 清单写得具体；03 的源码映射同时证明当前实现还不能提供这些规则所要求的完整 normalized input。结论是：**有限规格修订已部分闭合，但当前仍需依赖交付后再审；不授权 02/03 的正式性能工具或观测实现任务，不批准 build、runner 或实验。**

### B5：normalized adapter 与 `verified_off`

判定：**partial，且当前字段映射存在硬缺口。**

v3 的 `CompressionObservationV1` 设计是可机械消费的接口：要求 expected/observed participant 集合、file/worker/lifecycle coverage、effective 值、五类非负计数、artifact 引用和 SHA；缺任一项只能 `unknown`/ineligible，不能从 argv、自由文本事件或 wire 比值推导 `verified_off`。这一部分已闭合工具侧的防误判规则。

但 03 的实际字段映射不满足该接口的全覆盖要求：

- 单文件 STOR sender 有 `compressionAttempts`、`compressedFrames`、raw fallback/failure 和压缩 byte 计数，但并发 stream 只在 join 后聚合、没有 stream/participant id；STOR receiver 只有解压侧 frame/failure/byte 计数，没有 sender attempts/raw fallback，且提前错误通常没有完整最终汇总。
- RETR client receiver 没有 compression attempts，server sender 的 attempts/frames 在另一进程；两端都在成功 finalize 路径导出，失败/中断调用不能用缺失或零代表完整覆盖。
- 普通 tree 的 run summary 不聚合完整 compression metrics，逐文件日志没有 worker id 或参与者覆盖集合；tree download 的远端 sender 也不在 tree client runtime 中。Global scheduler 有 event/summary 候选，但 `compressedWorkItems`/`rawFallbackWorkItems` 的来源与 frame 计数不一致，且没有每 worker effective/全生命周期覆盖。
- `compression_sampling_attempts` 没有当前等价字段，`compression_dispatches` 只有 scheduler event 级候选；control reuse 只有请求值和成功 connect count，没有 reuse-hit/worker identity，`controlReconnectCount` 的打印为零不能证明无重连。file IO 也只有配置回显、POSIX strategy 解析值和 per-call stats，缺 backend effective 与失败调用覆盖。

因此，普通 tree、global scheduler、单文件 upload/download 的双端 participant、失败/中断/重试和并发覆盖均不能由当前字段机械产生 `verified_off`。v3 已正确规定 adapter 缺失时全部 CPNetFlux compression verdict 为 `unknown`、样本 ineligible，并明确 scheduler off 性能 case 不外推 global fixed/adaptive 或 resume/retry suite；但 03 尚未交付 adapter、participant roster、单位和覆盖能力，B5 不能升级为 pass。当前可执行的最小前置是：先由 03 提供正式 raw→normalized 映射和失败路径 coverage；再由 04 验收 coverage/零值语义，之后才可在实现任务中消费该结构。

### B6：GridFTP match validator 与依赖

判定：**partial。**

v3 已使 manifest 必填项和 validator 分支足够具体：auth/data-channel profile、mapping contract id/version、方向、`-p/-cc` 预期、per-file/total streams、观测方法必须在副作用前存在；缺项为 `manifest_invalid`，合同未冻结为 `blocked_contract_unfrozen`，证据缺失/窗口不覆盖为 unknown/ineligible，明确不一致为 `unmatched_config`/fail-fast，认证或服务不可用为 `blocked_auth`/`blocked_external_gridftp`。这部分能防止用参数名把 GSI reverse、TLS 或 segmented partial GET 冒充普通并流。

尚未闭合的是实际语义和证据来源：01 仍未冻结 auth/data-TLS/GSI 双向可比映射；03 未提供客户端/协议侧 effective config 字段；05 未冻结服务端连接/流日志来源、观测窗口、PID/端口归属。故当前所有受影响 GridFTP cells 仍只能 blocked/unknown/unmatched，不能标 `matched`，也不能授权实现后直接运行。

### B7：ledger、evidence index 与 durability

判定：**partial，逻辑 gate 已清楚，真实 durability 未闭合。**

v3 将 `ledger_structure_status`、`evidence_index_status`、`storage_durability_status` 分开，并规定合法迁移、seq/hash-chain、artifact size/SHA 回读、`run_evidence_gate=complete` 的合取条件；没有 05 的批准持久化位置、归档/回收 hash 对账和 retrieval 证据时，durability 必须为 `unverified`，gate blocked，`evidence_status` 不得 complete、`performance_eligible=false`，禁止 cleanup。这一规则足以防止把进程内 fsync 或同一 OS cache 的读回误称 durable。

仍未完成 05 的 run root/挂载点/归档与资源契约，也未由 04 执行进程中断、append/fsync/readback 失败、chain tamper、dangling index、cleanup interruption 和 ledger 重建测试。因此当前只能验收规格逻辑，不能给 performance eligible 或 cleanup 通过。

### B8：known vectors、redaction、hash-chain、timer 边界与 QA 断言

判定：**partial。**

v3 已补足具体的 order-key、非 ASCII argv、secret redaction、manifest hash、两条 ledger chain event 和 finite/zero/null/NaN/Infinity timer 数值向量，并明确 redaction 先于 hash/log、单字节篡改/prev hash/seq gap/重复 seq/dangling artifact/cleanup 中断的拒绝断言。作为 QA 清单，内容已足以指导纯函数和故障注入用例。

但这些仍是“应执行的断言”，不是已通过测试；v3 自身也标为依赖 04。timer event pair/阶段名称/多 stream 聚合仍依赖 01，真实 storage durability/retrieval 依赖 05。此次没有独立重算 digest、运行 Python/CTest、故障注入或实现测试，不能把 known vector 声明写成 serialization、ledger 或 timer gate 通过。

### 已闭合与保留 blocker

| 范围 | 复审结论 |
| --- | --- |
| B5 validator 适配层、缺失观测硬失败 | 工具规则闭合；03 当前字段无法提供完整 normalized input，保留 blocker |
| B6 manifest 必填与 match 分支 | validator 规则闭合；01/03/05 的实际语义和证据来源未冻结，保留 blocker |
| B7 三层状态和禁止 cleanup | 逻辑 gate 闭合；05 durability 和 04 中断/篡改测试未完成，保留 blocker |
| B8 vectors 与 QA 清单 | 规格向量闭合到可写测试；测试尚未执行，01 timer 事件和 05 durability 仍是 blocker |
| 02/03 正式实现任务 | **当前不授权**。必须先交付 01 timer/auth/GSI/verify 契约、03 normalized/native 字段、05 durability/resource/service evidence，并由 04 复审/测试 |

### 仍需冻结的跨角色契约

- **01：** timer 起止事件、方向/连接/最终确认纳入方式、auth/data-TLS/GSI reverse 可比流映射、none/CRC32C effective verify/fallback；v3 只做存在性和未知映射拒绝，不定义语义。
- **03：** raw→normalized `CompressionObservationV1` adapter、expected participant roster、计数单位、file/worker/lifecycle coverage、单文件/tree 双向和失败/重试/并发路径的 effective telemetry；当前映射只是候选事实。
- **05：** B6 服务端实际 stream evidence source/window/PID/port、B7 durable run root/archive/retrieval hash、挂载点预算和 cleanup 授权；上海当前 0 GiB，禁止 build/run。
- **04：** analyzer 只消费 normalized/eligible/block 结果，执行 vectors、ledger tamper、中断、cleanup failure 和恢复测试；本任务未运行。

### 执行回执

- 实际输入/输出 commit：输入与复审结束 HEAD 均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；本节只追加于工作树，未提交。
- 实际改动：仅追加本节至 `docs/coordination/receipts/04-quality.md`；未修改 02/03 回执、任务单、PLAN、BOARD、ROSTER、源码、runner、schema、dataset、preflight、analyze 或 tests，未操作 Git index。
- 实际命令、退出码与证据：`git rev-parse HEAD`（0，匹配）；`git status --short --branch`（0，记录共享改动）；定向 `Get-Content`/`rg` 读取任务、v3、03 contract、QA-02、PLAN 和源码映射（0）；写后 `git diff --check`（0，仅 LF→CRLF 提示）。本轮未运行 Python、CMake/CTest、runner、preflight、SSH、构建、传输、实验、清理或故障注入。
- 规格复审结论：B5 partial；B6 partial；B7 partial；B8 partial。有限修订已提高可执行性，但没有一项可写成代码/测试通过；整体继续阻塞。
- 失败/跳过/阻塞及其原因：03 现有字段不足以证明 B5 `verified_off`，01 timer/auth/GSI/verify 未冻结，05 durability/resource/service evidence 未交付，04 测试未运行；上海空间仍为 0 且无清理授权。所有未运行项均不计为通过。
- 下一角色可直接执行的下一步：03 先交 adapter 和覆盖字段；01/05 交付各自契约；04 再复审 schema/analyzer 并运行授权 QA 清单；只有复审通过后 00 才可创建白名单正式实现任务。本轮不自行派发。

### 验收与路线变化

- 验收人：04 测试与质量独立复审；结论：**PERF-TOOL-PLAN-03 v3 与 PERF-IMPL-CONTRACT-02 v1 仅部分闭合 B5–B8，当前不授权 02/03 正式实现任务，不批准代码、构建或实验。** 依据为 v3 的 validator 规则与 03 当前源码字段/覆盖边界。
- 路线未改变，未替代或覆盖 `R2026-09-23.1`。本回执区分规格完整性与测试执行结果，不把 partial/NOT_RUN 写成 pass，也不宣称 100G readiness。

## PERF-QA-VECTOR-01 v1：B8 规格向量的纯函数复核

- 审查日期：2026-09-23。
- 任务/路线版本：`PERF-QA-VECTOR-01 v1`，`R2026-09-23.1 / v1`；输入 HEAD 与任务单一致，未 superseded。
- 实际 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；复核期间未改变。
- 实际改动：仅追加本节；未修改 BOARD、ROSTER、任务单、runner、源码或测试，未操作 Git index。

### 纯函数结果

使用 PowerShell/.NET `SHA256`，以 `[System.Text.Encoding]::UTF8.GetBytes` 对回执中的 canonical 字符串取字节并计算小写十六进制摘要。canonical JSON 与 v3 回执逐字一致（包括紧凑格式、键顺序和非 ASCII 字符）。最终核算命令退出码为 0；此前辅助函数作用域/别名错误的试跑未计入结果，也未写文件。

| 向量 | canonical bytes 与 v3 完全一致 | expected SHA-256 | actual SHA-256 | 结果 |
| --- | --- | --- | --- | --- |
| non-ASCII argv | PASS | `babcf714d8a8b3a81ac6c2dd12c4204e1cf36c1a3de4a1c0095692a37ac8003c` | `babcf714d8a8b3a81ac6c2dd12c4204e1cf36c1a3de4a1c0095692a37ac8003c` | PASS |
| redacted argv | PASS | `85f1558763bd9a9ef131858c8f7f70b40e0bcab800e4c03d0b67a195eb1e528e` | `85f1558763bd9a9ef131858c8f7f70b40e0bcab800e4c03d0b67a195eb1e528e` | PASS |
| manifest | PASS | `586489807f37deb641de8188315ca679da6735aeccaf7f35bcf0f8a835cd8228` | `586489807f37deb641de8188315ca679da6735aeccaf7f35bcf0f8a835cd8228` | PASS |
| ledger event 1 | PASS | `62ffd72b138318eedfefa1370ee503ac9bae1959077548f127726e2f3c569166` | `62ffd72b138318eedfefa1370ee503ac9bae1959077548f127726e2f3c569166` | PASS |
| ledger event 2 | PASS | `7da800efb8159be1396513c0526bae2033ec8c07c988bc785c03ceca3d6f6ff7` | `7da800efb8159be1396513c0526bae2033ec8c07c988bc785c03ceca3d6f6ff7` | PASS |

附加断言均通过（同一 UTF-8/哈希函数，退出码 0）：

- redacted canonical bytes 不包含原始 secret；原文未写入本回执或命令输出（PASS）。
- UTF-8 bytes 无 BOM（PASS）。
- argv 参数重排的摘要与原摘要不相等（PASS）。
- manifest 路由版本末位单字节改写的摘要与原摘要不相等（PASS）。
- ledger event 2 payload 改写的摘要与原摘要不相等（PASS）。
- ledger event 2 `prev_event_sha256` 改写的摘要与原摘要不相等（PASS）。

### 边界与未运行项

上述结论仅证明给定 canonical 字符串的 UTF-8 编码、摘要向量和规格级篡改敏感性；没有可执行 validator 因此不把断言写成系统拒绝已通过。项目 Python、CMake/CTest、runner、传输、故障注入、timer event 配对、ledger 中断恢复和 cleanup gate 均未运行（NOT_RUN），也未进行 SSH、构建、实验或清理。

### 执行回执与验收

- `git rev-parse HEAD`：退出码 0，仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- `git status --short --branch`：退出码 0；保留共享工作树既有改动，未覆盖、暂存或清理。
- `git diff --check`：退出码 0；无新增空白错误（仅保留工作树既有换行提示）。暂存区为空。
- 规格向量结论：5 个 expected/actual 全部 PASS，canonical bytes、redaction 和四类不相等断言 PASS。
- 验收结论：**B8 纯函数向量复核完成；不等同于 validator、项目测试或性能实验通过。** 后续仍需在有授权的实现/测试任务中执行真实序列化、故障注入、timer 和 durability 覆盖。

## PERF-QA-CONTRACT-04 v1：适配映射与环境证据联合复审

- 审查日期：2026-09-23。
- 任务/路线版本：`PERF-QA-CONTRACT-04 v1`，`R2026-09-23.1 / v1`。
- 实际输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，与任务单一致；复核期间路线和 HEAD 未变，未 superseded。
- 审查对象：03 `PERF-IMPL-ADAPTER-01 v1`（`docs/coordination/receipts/03-implementation.md`）、05 `PERF-ENV-EVIDENCE-02 v1`（`docs/coordination/receipts/05-operations.md`），并对照 02 v3 B5–B7 规则及既有 `PERF-TOOL-QA-03 v1`。
- 实际改动：仅追加本节至本回执；未修改 C++、Python、schema、runner、测试、任务板、路线或云端状态，未操作 Git index。

### B5：`CompressionObservationV1` 适配映射

**结论：blocked（保持 `unknown/ineligible`，不能升级）。**

03 的交付是字段映射和缺口清单，并非已存在的 raw→normalized adapter 或可校验的 native 输出。逐项核对如下：

- single upload/download 的 sender/receiver 有部分压缩计数，但并发 stream 只在 transfer-call join 后聚合，没有 stream/participant id；RETR 的 sender 与 receiver 分属不同进程，不能用一端报告代替另一端。
- tree upload/download 能提供部分 manifest/file lifecycle 事件，但没有完整 client/server/worker participant roster、file→worker 归属或远端 RETR sender 的观测集合；global scheduler 的 `compression_dispatch` 是 file-plan 事件，不是实际 per-participant dispatch counter。
- `sampling_attempts` 当前缺失；`compression_attempts`、`compressed_frames`、`compressed_payload_bytes` 仅部分可得且主要在成功汇总出口出现；零值没有 endpoint、stream、文件和全生命周期覆盖证明。计数单位和 source scope 也未形成规范化记录。
- 失败、重试、并发和进程中断没有统一且必达的 terminal record；retry 的首次失败与 raw retry 没有完整 attempt lineage。worker reuse 只有请求模式/成功连接数，没有 reuse-hit、worker 或 connection identity。

因此，single/tree 双向、participant roster、file/worker/lifecycle coverage、失败/重试/并发覆盖和五类计数的机械条件尚未同时满足。依照 B5 规则，不得补零、不得从 wire ratio 或自由文本推导 `verified_off`；即使某个成功 case 的请求值为 off，也只能保留 `unknown`、`performance_eligible=false`。

进入正式实现前的最小条件：03 交付版本化 native→normalized adapter 与真实 fixture/输出，包含 expected/observed roster、每 participant 的 requested/effective/source、五类计数的 present/value/unit/scope、file/worker/lifecycle 覆盖、失败/重试/中断终态和 raw artifact size/SHA 引用；04 再以缺失/零值/差集样本验收机械状态函数。上述条件本轮未执行。

### B6：GridFTP 服务与流证据关联

**结论：blocked（不能标 `matched`，不能授权相关实现或实验）。**

05 的只读证据足以确认当前上海 `*:2811` 是 root 运行的 `globus-gridftp-server`，PID `339900`，systemd 单元为 `gridflux-gridftp-gsi.service`，并确认该服务端口和相关数据端口范围须保留。它不等于某个性能 case 的 server-side stream evidence：

- 历史 PID `768474`/端口 `25252` 在两次采样时已不存在，无法恢复其 uid、exe、cwd、cgroup 或服务归属；不得将其归为 CPNetFlux，也不能据 PID 消失推断数据已清理。
- `/srv/gridflux-gsi` 的路径、属主和活动 GSI unit 支持服务关联，但没有读取目录内容或进程打开句柄，实际占用和 case 归属仍 unknown。
- 05 没有提供按 case/block/attempt 绑定的 server connection/stream 日志、观测窗口、PID/端口归属或 expected-vs-observed stream 计数；03 也没有交付客户端/协议侧 effective auth、data-channel、方向和流配置字段。01 的 auth/data-TLS/GSI 可比 mapping 尚未冻结。

因此 B6 所需的 auth/data-channel/expected streams 语义和实际证据链仍不完整。进入实现前最小条件是：01 冻结 mapping contract；03 记录 client/protocol effective fields；05 提供可回读、带 case/block/attempt、host、UTC/monotonic window、control/data port、PID/service 与 per-file/total stream 计数的 server evidence，并由 04 验收缺失/窗口不覆盖/明确不匹配分别落为 `unknown`/`unmatched_config`/blocked 状态。本轮未运行 GridFTP、validator 或传输。

### B7：durability 与 cleanup gate

**结论：blocked（`storage_durability_status=unverified`，`run_evidence_gate` blocked，禁止 cleanup）。**

05 已核实 `/` 与 `/tmp` 同属 `/dev/nvme0n1p3` ext4，上海采样约 33.43 GiB raw free；按 10 GiB 保留和路线草案 38.25 GiB logical payload 同时驻留假设，至少约缺 14.82 GiB，且尚未计 build/archive/evidence，实际峰值仍未冻结。不能把 raw free 写成实验配额，也不能把 `/` 与 `/tmp` 相加。

证据性质必须分开：

- `gridflux-backup-verification.md` 的 SHA-256 只证明该说明文件本身；`dataset_manifest.json` 的 SHA-256 只证明该本地索引文件本身，索引没有发现 payload 的逐项 64 位 hash。二者都不是上海实验 payload hash、payload manifest 或 retrieval 对账。
- 没有批准的 run root/archive/retrieval 位置、逐 payload manifest/hash 对账和恢复读取证据；05 明确当前可释放量为 `0 bytes`。历史 PID 归属未知、`/srv/gridflux-gsi` 实际树大小和打开文件关系未核验，不能用目录项、文件自身 hash 或 PID 消失替代归属/可释放证明。
- 因此只能把 ledger/index 的逻辑状态与介质 durability 分开；没有 05 durability evidence，不得给 `performance_eligible`、`evidence_status=complete` 或 cleanup 授权。进程中断、hash/index 损坏、回读失败和 cleanup nonzero 的故障注入也未运行。

进入正式实现前最小条件：05 冻结每个实际挂载点的 run root、峰值与 build/archive/evidence 字节预算，交付批准归档/回读路径及 payload manifest/hash 对账、服务/任务归属和逐路径可释放清单；04 再独立验证 append/fsync/readback、chain/index 损坏、进程中断和 cleanup stop。未满足前保持 `unverified/blocked`，不删除任何 payload。

### 验收与交接

| 项目 | 结论 |
| --- | --- |
| B5 `CompressionObservationV1` | **blocked**：映射文档完整描述缺口，但没有 adapter 输出、participant roster、全路径五计数和 terminal coverage |
| B6 服务/流证据关联 | **blocked**：当前服务 PID 可识别，case 级 server stream evidence、历史 PID 归属和 01/03 effective mapping 未闭合 |
| B7 durability/cleanup 前置 | **blocked**：仅有挂载/空间和本地证据文件自身 hash，无 payload manifest/retrieval durability；可释放量 0 |
| 02/03 正式代码任务 | **不授权**。先满足上列最小条件并由 04 复审；本轮不开始正式实现 |

- 未运行项：项目 validator、Python/CMake/CTest、runner、preflight、传输、实验、故障注入、cleanup、构建和新的 SSH 均未运行；本回执只读取已有回执和源码引用。
- 执行状态：保留共享工作树既有改动；未修改其他文件、未暂存、未提交。结论是质量门禁判断，不是系统测试或性能验收通过。
