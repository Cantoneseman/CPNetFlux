# LOWLEVEL-TREE-STAGE-PROFILE-01：目录关键路径阶段观测结果

- 路线/任务版本：`R2026-09-23.4 / v1`。
- 执行状态：**BLOCKED；未进入构建、smoke 或目录 case。**
- 文档输入/输出 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。

## 结论

本轮未取得目录阶段时间、tree hash 或 inter-file idle 样本，不能判断 EPSV/passive endpoint 准备或 TCP connect 是否可与前一文件 payload 重叠，也不能判断它是否降低 inter-file idle。`LOWLEVEL-TREE-PRECONNECT-IMPL-01` 的依据不足；本报告不支持派发该实现，也不构成 100 Mbps 或 GridFTP 结论。

停止原因是 WSL 服务拒绝访问，无法先核实任务要求的 Linux ext4 `/tmp` 空间和隔离目录。按停止条件没有在 Windows build path、其他 Linux 环境或云端替代运行。

## 输入与运行环境门禁

- 执行前 `git rev-parse HEAD`：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 固定提交对象存在；它是当前 HEAD 的祖先。`git diff --name-only 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src include` 无输出，且 `git diff --quiet … -- src include` 退出码为 0，表明当前 HEAD 中相关源码与固定实现提交相同。
- 上一份独立 `LOWLEVEL-SINGLE-PROFILE-00` 结果记录同一提交 `git archive` SHA-256 为 `bbf320f20230700d61f318c9081694f00294044ae39a5ed308bfcdbc01bf1b5e`。这是引用既有单文件 profile 的来源标识；本轮没有访问该 WSL archive、重新生成/核验 archive，不能把它记为本任务实际输入产物。
- WSL 检查：`wsl.exe --list --quiet` 返回 `Wsl/EnumerateDistros/Service/E_ACCESSDENIED`，PowerShell 显示 WSL exit `-1`；`wsl.exe --exec bash -lc '…'` 返回 `Wsl/Service/CreateInstance/E_ACCESSDENIED`，exit `-1`。随后按既有记录中的发行版名尝试 `wsl.exe --distribution Ubuntu-22.04 --exec bash -lc '…'`，仍返回 `Wsl/Service/E_ACCESSDENIED`，exit `-1`。
- 因 WSL shell 无法启动，distro/工具版本、`/tmp` 文件系统和剩余空间、`/tmp/cpnetflux-runs/LOWLEVEL-TREE-STAGE-PROFILE-01/` 是否存在均**未核实**。本轮没有创建目录、覆盖或清理任何未知目录。
- 已读取本任务、固定输入上的低层设计与 QA 结果、目录源码映射结果及 2026-09-17 directory profiling 决策。它们支持阶段观测的必要性，但不能替代本任务测量。

## 实验状态

| 项目 | 状态 | 本轮证据 |
|---|---|---|
| WSL distro、g++/CMake/Ninja/CTest 工具链、ext4 `/tmp` 与 ≥5 GiB 余量 | BLOCKED | WSL 启动 E_ACCESSDENIED；信息未取得 |
| 任务隔离目录及 source/build/results 子目录 | NOT CREATED | 无法确认现有目录是否存在；未创建/清理 |
| 固定提交 source archive、archive SHA 本轮核验 | NOT RUN | 只有上一任务引用的 archive SHA，非本轮实际 archive |
| CMake configure/build、binary SHA、完整命令 | NOT RUN | 无本轮 build 或二进制 |
| tree upload/download、control reuse、resume、changed-file/edge smoke | NOT RUN | 无退出码；均不计为 skip pass |
| case plan、随机端口、server/client PID 与归属 | NOT CREATED | 无进程、端口或 case 命令 |
| dense 32×1 MiB / 128×1 MiB、fp1/fp8、双向、每格 3 次 | NOT RUN | 任务建议矩阵最多 24 个传输 case；未生成实际 plan 或运行任何 case |
| transfer / integrity / evidence / wire accounting | NOT AVAILABLE | 无输入/目标 tree hash、退出状态、payload 或日志 |
| syscall/event/阶段时间、inter-file idle、wall 和重复离散度 | NOT AVAILABLE | 未取得事件或 trace；不从日志字符串推定阶段边界 |

失败/阻塞/跳过分类：没有传输 case 运行，故无 case failure 或 skipped case。唯一阻塞是本机 WSL 服务的 `E_ACCESSDENIED`。没有连接服务器、SSH、云端、GridFTP；没有生成 payload、trace 或原始输出，也没有 cleanup。

## 阶段数据与最小可观测边界

以下验收阶段本轮均无值；未完成、缺事件和未适用不能记为 0：

| 阶段/指标 | 本轮值 | 说明 |
|---|---|---|
| control auth / EPSV / passive endpoint ready | 未取得 | 没有同进程 monotonic event |
| per-stream TCP connect begin/end | 未取得 | 没有 client event 或 fd/peer 证据 |
| STOR/RETR command 与 150 response | 未取得 | 没有控制通道 trace/event |
| SessionInit / ResumeResponse | 未取得 | 没有 DATA 协议 event |
| first payload、payload I/O bytes/time | 未取得 | 没有 payload 事件与 byte accounting |
| transfer complete / 226 | 未取得 | 没有控制响应与完成事件 |
| manifest finalize begin/end | 未取得 | 没有 manifest 阶段事件或 hash |
| next file dequeue/start、inter-file idle | 未取得 | 没有同一 worker/file 顺序事件 |
| overlap(connect/endpoint preparation, previous payload) | 未知 | 无同一进程 monotonic timestamps；不能跨主机相减 |

低层设计材料要求以同一进程单调时钟和稳定关联键分别记录 worker/file/stream 的 endpoint 准备、TCP connect、150、session、first payload、payload I/O、226/complete、manifest finalize 及下一文件 dequeue/start；重叠保留原事件区间，不对并发阶段求和冒充 wall。本轮既没有这些阶段事件，也没有通过运行确认当前 binary 可提供等价事件。因此如恢复 WSL 后固定提交仍无这些事件，须先派独立 telemetry/instrumentation 任务；不可从当前 smoke 文本日志构造精确阶段时长。

## 与既有单文件 profile 的关系

`docs/tasks/2026-09-23-lowlevel-single-profile-00-result.md` 记录 00 曾在同一固定实现提交的单文件 loopback plain TCP profile、测试和 WSL 构建结果。这是单文件 DATA send syscall 的独立任务，不包含目录 worker 阶段、dense tree hash 或本任务要求的 endpoint/connect 与前一文件 payload overlap。不能将其 wall/CPU、archive 或 smoke 结果移作目录 profile 结果。本轮失败也不推翻那份独立结果。

## 后续解阻与决策门

1. 运维/本机环境恢复 WSL 启动后，先重新检查 distro、工具版本、`/tmp` 类型和余量；达到至少 5 GiB 且确认任务目录冲突/唯一后缀策略后，才可创建隔离根目录。
2. 从固定提交生成并 hash source archive，在 ext4 配置/构建，记录 compiler、binary hash、target build 和每个定向 CTest 的原始退出码。
3. 先证明目标运行能获取同进程阶段事件、源/目标 tree hash 和独立 transfer/integrity/evidence/wire 状态，再启动 32×1 MiB / 128×1 MiB（或声明偏差）的 upload/download、fp1/fp8、每格至少 3 次矩阵。
4. 只有当 EPSV/TCP 连接准备在前一文件 payload 期间可观测地重叠，并且 inter-file idle/wall 的下降超过重复测量噪声，才由 00 单独评估 lookahead 实现任务。否则不派 lookahead；无阶段归因则维持 PARTIAL/BLOCKED。

所有潜在结果仍只回答 loopback 下的目录 worker 机制；不能外推深圳—上海 100 Mbps，也没有 GridFTP 对照或云端运行授权。

## 执行回执

- **实际输入实现 commit、archive SHA-256、输出 HEAD：** 输入 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；本轮 archive 未生成/验证。可参考上一独立 profile 记录的 `bbf320f20230700d61f318c9081694f00294044ae39a5ed308bfcdbc01bf1b5e`，但不作为本任务实测 archive。输出 HEAD 仍是 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- **WSL/空间/目录：** 三种 WSL 启动入口均 `E_ACCESSDENIED`；distro、工具版本、ext4 容量和目录存在状态未核实。
- **build/CTest/cases：** 全部 `NOT RUN`；没有退出码、端口、PID、命令行或 hash。
- **阶段数据与统计：** 全部未取得；不计算中位数/range/median，不填零。
- **实际改动：** 仅新增本任务结果与独立 CLI 摘要文件；未改源码、测试、runner、共享文档、原始证据或 Git index。
- **CLI 摘要：** 本轮没有调用 Codex CLI；独立 last-message 是人工交接摘要，不冒称 CLI 输出。
- **下一角色：** 先恢复并核验本机 WSL 访问；环境门禁通过后续跑本任务，不从云端或其他主机绕行。
