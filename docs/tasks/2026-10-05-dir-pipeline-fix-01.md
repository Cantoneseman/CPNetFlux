# DIR-PIPELINE-FIX-01：多 worker 流水线回退保护

- 输入提交：`4c776f02e15c26427a7171be8bc37dd0076b62dd`
- 分支：`codex/DIR-PIPELINE-FIX-01`
- 目标：修复 `control-pipeline-depth=1` 与多文件并发组合造成的吞吐回退。
- 范围：`tree_transfer_client.cpp` 的有效调度选择与摘要口径；流水线 smoke 的控制连接回归断言。
- 非目标：不改协议帧、manifest 格式、checksum/resume 语义、服务端或云端实验脚本。
- 根因证据：depth=0、file_parallelism=8 使用 8 个 worker 控制连接；当前 depth=1 额外持有两个流水线控制槽，同时仍启动 7 个普通 worker，控制连接变为 9，WAN 试跑吞吐从 97.548 Mbps 降至 91.812 Mbps。
- 设计：当前两控制槽实现只在 `fileParallelism=1` 启用；多 worker 请求回到普通 worker pool，避免额外连接与调度开销。摘要将反映实际启用的流水线深度。共享控制池作为后续独立任务。
- 验收：sidecar smoke 覆盖 depth=0/1 与 file_parallelism=1/8 的上传、下载、文件集合、SHA-256、计数；多 worker depth=1 的 `control_connect_count <= file_parallelism`；cpnetflux 单元测试通过；`git diff --check` 通过。
- 产物：本任务修改的源代码、回归 smoke、该任务单；完成后提交并推送 GitHub，回读远端 SHA。
