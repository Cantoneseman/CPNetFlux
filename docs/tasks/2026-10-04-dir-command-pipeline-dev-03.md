# DIR-COMMAND-PIPELINE-DEV-03：封闭目录命令流水线的故障清理门

- 状态：review / blocked-validation（实现已写，动态门禁待深圳实验窗口）
- 路线版本、任务版本：目录小文件命令流水线优先路线，v1；用户已授权优先实现该机制，不等待一般迁移门收尾。
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：04 测试与质量。
- 目标：在 opt-in depth=1 流水线和 DEV-02 候选身份校验之上，补齐 pending 命令拒绝/超时、控制连接断开、当前文件失败时取消候选及资源释放的可验证行为，避免挂死、错误完成或 listener/thread/连接泄漏。
- 非目标：不改 wire/data frame、manifest/resume/checksum、depth=0 默认行为；不改 scheduler/compression/IO backend；不做性能 A/B、GridFTP 对照或跨域实验，不宣称吞吐收益；不访问或修改 live root。
- 固定输入：753a67d415614cef0b10cde95275f3431fe11de0；隔离 worktree 初始 clean，分支 codex/DIR-COMMAND-PIPELINE-DEV-03。
- 必读：docs/tasks/2026-10-03-dir-command-pipeline-dev-02-result.md；docs/tasks/2026-10-04-dir-sidecar-enum-fix-01.md、同名前缀的 result 和 qa-result。已存在 TreePipelineCandidateIdentityTest；QA 已接受 sidecar 修复，但 pipeline 整体仍 BLOCKED。
- 允许修改：include/cpnetflux/core/io/tree_pipeline_control_io.h、include/cpnetflux/core/io/tls_socket.h、src/core/io/tree_pipeline_control_io.cpp、src/core/io/tls_socket.cpp、src/core/io/tls_socket_stub.cpp、tests/unit/tree_pipeline_control_io_test.cpp；src/core/io/tree_transfer_client.cpp、src/core/io/file_transfer_server.cpp、src/core/io/file_download_sender.cpp、src/protocol/control/control_server.cpp、对应 include/cpnetflux/core/io 头文件；tests/unit/tree_pipeline_* 及相关 listener/control 单测；必要时新增 tools/test/run_gridftp_tree_pipeline_fault_smoke.py；CMakeLists.txt、本任务与结果文档。若需超出白名单先报告。
- 禁止：/root/projects/CPNetFlux live root、其他 worktree/index、/root/projects/GridFlux-Beta、/root/projects/CPSS(DCC)、上海、现有用户实验进程/端口/数据。
- worktree：/tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-03/src。
- 环境限制：用户此前说明深圳可能有活动实验。本轮先做静态实现/测试编写；未经 00 明确开启测试窗口，不运行 CMake/build/CTest、loopback、故障注入或性能命令，不停止或干扰进程。

## 验收标准

1. 保留并覆盖候选 generation、manifest file index/path、upload/download direction 的拒绝向量；旧候选不能被新 generation 或另一文件接管。
2. depth=0 默认行为保持不变；depth=1 两个控制槽在 pending command reject/timeout、control HUP/EOF、当前传输失败/取消后有界退出和确定状态；未传输候选不能标 Completed。
3. 每个故障向量断言 listener、候选线程/控制 FD 和 pending 状态收敛；至少一个真实 socket/listener 测试覆盖 disconnect/cancel，不能只测原子 bool。
4. 测试窗口获准后，在隔离 build 执行 cmake --build <isolated-build> --parallel 2；ctest --test-dir <isolated-build> --output-on-failure -R 'TreePipeline|cpnetflux_tree_pipeline'。已有 sidecar smoke 必须继续验证 depth 0/1、upload/download 的文件集合、SHA-256、tree/per-file manifest 和 summary counts。由 00 决定是否跑全套 CTest。
5. 当前无测试窗口时，仅交代码、测试源、静态门禁和一条可复制的有限测试命令；报告状态 review/blocked-validation，不得宣称验收通过。

## 资源与产物

仅使用指定隔离 worktree；获准后使用独立 build/evidence 子目录、free_port loopback 端口，避开历史服务端口。先保留日志/hash，再清理本测试临时文件。每次运行前确认可用空间不少于 10 GiB 且没有活动实验冲突。
产物：本任务单、白名单内代码/测试、docs/tasks/2026-10-04-dir-command-pipeline-dev-03-result.md。完成静态阶段后可提交明确文件并推送 codex/DIR-COMMAND-PIPELINE-DEV-03，精确回读远端 SHA；动态验证未运行必须明示。

## 派单与回执

请核对任务版本、固定 SHA、白名单与深圳活动状态；用户已要求此机制优先，故不等待一般迁移门，但不得碰 live root。测试窗口打开前仅静态实现和测试编写。若提交，清晰报告构建/动态验证是否未运行。

- 实际输入 commit：753a67d415614cef0b10cde95275f3431fe11de0；实现 commit：3ac89fe0e0723f8da3f53aa7972d6aa5545efa50。
- 产物与静态检查：见 docs/tasks/2026-10-04-dir-command-pipeline-dev-03-result.md；UTF-8/尾空格门禁和 CRLF-aware git diff --check 通过。
- transfer/integrity/evidence/wire：不作性能或 wire 结论；传输/完整性证据待测试窗口。
- 阻塞：动态测试需避开深圳活动实验。
- 下一步：04 对固定提交窄审；测试窗口打开后运行本任务门禁。

## 验收与路线变化

用户已将目录命令流水线机制提升为当前最高优先级；此任务只解除该机制的实现排队依赖，不代表迁移、运行环境或性能实验已验收。04 结论待定。
