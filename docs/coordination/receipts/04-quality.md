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
