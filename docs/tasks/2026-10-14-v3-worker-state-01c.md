# V3-WORKER-STATE-01C：统一 control pipeline 与持久 data worker

日期：2026-10-14
状态：已完成
输入提交：41eb6e74e32cf13590776bba28560bece85db41b
权威源码：/root/projects/CPNetFlux

## 范围

- 允许同一目录任务同时声明 control pipeline depth 与 persistent data session。
- 将 pipeline depth 解释为持久目录调度的受界预取窗口；工作项只通过 DynamicFileQueue 领取，data worker 的 lease 贯穿 begin/in-flight/commit-pending/result。
- 保留取消、失败和连接关闭时的统一队列回收；不再把组合请求悄悄改成 per-file。
- 在 JSON summary 中记录组合实际窗口和模式，确保 requested/actual/fallback 如实可见。
- 增加 parser、loopback 上传/下载和取消/身份回归。

## 非目标

- 不重写旧 per-file control pipeline 的协议实现。
- 不在本阶段接入 global scheduler、TLS/认证策略、桌面端或跨主机性能实验。
- 不扩大 range/checksum/resume 协议语义；只复用现有已验收的 V3 持久事务和 lease。
- 未通过全部代码门禁前，不把该组合声明为稳定版或启动实验。

## 验收

静态文件调度与持久 pipeline 组合明确拒绝；组合必须通过动态队列调度。

1. \`--data-session-reuse tree --control-pipeline-depth {1,2,4}\` 解析成功，不产生 per-file fallback。
2. 默认目录模式与显式组合均走 persistent_tree；上传/下载 loopback 的 tree hash、manifest、checksum、range/resume 保持正确。
3. 组合窗口有界，失效/重复/跨 channel lease 被拒绝；取消和 data/control 断线不会泄漏 active lease。
4. 定向 CTest、V3 stability matrix、全量 CTest 和 \`git diff --check\` 通过；可选环境能力只记录 skip。
5. 只提交本任务明确文件，push 后回读远端 SHA。

## 允许路径

- \`src/config/tree_transfer_options.cpp\`
- \`src/core/io/tree_transfer_client.cpp\`
- \`include/cpnetflux/core/io/persistent_tree_transfer.h\`（必要时）
- \`src/core/io/persistent_tree_transfer.cpp\`（必要时）
- \`tests/unit/tree_transfer_options_test.cpp\`
- \`tests/unit/persistent_data_session_test.cpp\`（必要时）
- \`tools/test/run_v3_stability_matrix.py\`（必要时）
- 本任务单与对应结果文档、任务板行。

## 回滚

代码使用 \`git revert <commit>\`；文档和测试证据保留。不得 reset/clean 或触碰其他 dirty 文件。

## 结果

- control-pipeline-depth=1|2|4 与 persistent tree 现在共享 DynamicFileQueue 的有界 dispatch window；不再 fallback 或在连接前拒绝组合。静态调度组合仍明确拒绝。
- 默认目录参数已由单测固定为 persistent_tree、动态文件调度、CRC32C、pending window=2；未显式设置 depth 时使用同一共享持久 dispatch（summary 的 depth=0 表示自动窗口）。显式 --data-session-reuse off 仍是可回滚的 per-file V3 编排入口。
- 本任务只把 depth 统一为持久 worker 的预取/背压窗口，不伪造旧 pipeline 的逐项控制报文预创建；每项工作仍由 worker 原子领取并在 lease 完成/取消/错误时释放。

## 验证证据

输入提交：41eb6e74e32cf13590776bba28560bece85db41b。

- Release build：cmake -S /root/projects/CPNetFlux-worktrees/v3-worker-state-01c -B /tmp/cpnetflux-v3-worker-state-01c-build -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON；构建通过。
- 定向组合/队列/调度 CTest：22/22 通过。
- 带测试专用 token 的全量 CTest：299/299 通过；io_uring 可选能力 1 项按环境 skip。
- run_cpnetflux_tree_dynamic_queue_smoke.py：mixed、dense 128×1MiB、large 129MiB range 均通过，含双向 hash/manifest/resume、碰撞拒绝、取消回收。
- run_v3_stability_matrix.py --rounds 16：TLS control/data/handshake RST、persistent TLS/checksum/range/resume、压缩混合决策均通过。
- git diff --check 通过。

## 未完成与下一步

- TLS、data TLS、压缩 auto、global scheduler 仍需按部署策略显式配置；本任务没有把需要凭据或链路容量策略的选项强行默认打开。
- 远端实验室跨主机与 GridFTP 对照实验未启动，等待 00 安排短实验。
