# DIR-SIDECAR-ENUM-FIX-01：目录传输排除已验证的逐文件状态清单

- 状态：review
- 路线版本、任务版本：目录命令流水线 DEV-02 后续正确性修复，v1
- 发起人：00 总指挥；执行角色：03 核心实现（总控接管远端补丁传输）；验收角色：04 测试与质量
- 目标及理由：避免 CPNetFlux 有效的上传/下载逐文件恢复清单被树扫描和 LIST/NLST 当成业务文件，导致上传/下载集合、manifest 及传输计数膨胀，破坏 depth=0/1 小文件流水线的正确性。
- 非目标：不改命令流水线协议/状态机、manifest 格式、resume/checksum/scheduler/compression/TLS/IO backend；不作性能或跨域实验，不声称性能收益。
- 输入 commit（完整 SHA）、工作树状态：7d7b77c9238b1c20b0f3281f238b95051948c8cc；新建于深圳隔离 worktree，初始 clean；不是 live root。
- 必读资料和证据路径：本任务单；输入 commit 的 tree_scan、control_server、transfer/download manifest 实现；DEV-02 QA 结果 commit 9904a62cf330d3933b723d70fb6c2e41ecd7d617 指出的 sidecar 集合膨胀。
- 允许修改的文件/目录；禁止修改的资源：include/cpnetflux/core/tree/tree_scan.h、src/core/tree/tree_scan.cpp、src/protocol/control/control_server.cpp、tests/unit/tree_scan_test.cpp、tools/test/run_gridftp_tree_pipeline_sidecar_smoke.py、CMakeLists.txt、本任务与结果文档；禁止 live root /root/projects/CPNetFlux、DEV-02/QA worktree、上海、/root/projects/GridFlux-Beta、/root/projects/CPSS(DCC)。
- 工作分支/worktree：codex/DIR-SIDECAR-ENUM-FIX-01；/tmp/cpnetflux-runs/DIR-SIDECAR-ENUM-FIX-01/src。
- 前置条件、环境占用和停止条件：深圳 /、/tmp 同设备，写入前实测可用 17 GiB；只在临时目录运行 loopback smoke。不得访问 live root 或上海。若文件系统低于 10 GiB 或涉及用户实验端口/进程，立即停止。
- 验收标准及实际可执行命令（不要只写“所有测试”）：Release CMake configure；构建 cpnetflux_unit_tests 和全部目标；ctest --test-dir <build> -R TreeScanTest --output-on-failure；ctest --test-dir <build> -R cpnetflux_tree_pipeline_sidecar_smoke --output-on-failure；全量 CTest 时为 token-auth smoke 注入仅用于本次运行的随机测试值，逐项记录 skip。回归必须验证合法 transfer/download manifest 被排除、无效或路径不匹配的近似用户文件保留，并对 depth=0/1 双向 loopback 核验文件集合、独立 SHA-256、file/data-transfer counts。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：build=/tmp/cpnetflux-runs/DIR-SIDECAR-ENUM-FIX-01/build；loopback payload 使用 Python TemporaryDirectory，由测试自动清理；通过 free_port() 取得临时本地端口，不进入既有 GridFTP 控制/数据端口范围；不保留大 payload。
- 预期产物路径：源代码/测试；本任务单与 docs/tasks/2026-10-04-dir-sidecar-enum-fix-01-result.md；GitHub 分支 codex/DIR-SIDECAR-ENUM-FIX-01 上的明确提交。

## 派给角色的消息

直接以本任务正文作为唯一规格继续；隔离 worktree 内未提供本任务文件时，由总控补写并引用聊天已授权的任务范围。完成后写交接结果、提交允许的明确文件并 push，回读本地与远端 SHA。QA 独立复核，不要把本次 loopback 结果说成性能证据。

## 执行回执

- 实际输入/输出 commit：输入 7d7b77c9238b1c20b0f3281f238b95051948c8cc；实现/测试/任务单提交 e7f0ee3ba317d8dc45f997c1f04ec94570013051，已 push 且远端 SHA 回读一致。
- 实际改动：见本目录结果文档；仅修改任务白名单文件。
- 实际命令、退出码与证据文件：见本目录结果文档和云端隔离 evidence 目录。
- transfer / integrity / evidence / wire accounting（适用时）：loopback transfer 与 SHA-256 核验作为正确性证据；不作 wire 或性能结论。
- 失败/跳过/阻塞及其原因：未提供测试 token 的初次 CTest 有 2 项认证 smoke 失败；注入一次性随机测试值后完整 CTest 220 项无失败，io_uring 可选测试 skipped。
- 剩余风险、未完成事项：pending reject/timeout、listener disconnect、candidate cancel/cleanup 故障注入仍未由 DEV-02 QA 完成。
- 下一角色可直接执行的下一步：04 在固定实现提交上独立复核新回归与文件集合正确性；仍需单独关闭 pipeline 故障门后才可放行性能实验。

## 验收与路线变化

验收人、结论和依据：待 04 独立验收。

若路线改变：本任务只修复 DEV-02 QA 指出的 sidecar 枚举正确性，不改变 A/B 优化方向，也不宣称跨域或性能收益。
