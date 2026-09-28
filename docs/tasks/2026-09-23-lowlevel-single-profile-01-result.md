# LOWLEVEL-SINGLE-PROFILE-01：单文件 plain TCP syscall 基线结果

- 路线/任务版本：`R2026-09-23.4 / v1`。
- 执行状态：**BLOCKED，未进入归档、构建或 profile 阶段。**
- 文档输入 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 固定源码提交：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。

## 停止原因

本机 WSL 服务入口返回 `E_ACCESSDENIED`，无法启动 Linux shell：

| 门禁 | 实际尝试 | 结果 |
|---|---|---|
| 发行版枚举 | `wsl.exe --list --quiet` | WSL 输出 `Wsl/EnumerateDistros/Service/E_ACCESSDENIED`；PowerShell 记录 WSL 进程退出值 `-1` |
| 直接执行 Linux shell | `wsl.exe --exec bash -lc '…'`（只读采集 distro、工具版本、`df/findmnt /tmp`、任务目录状态） | WSL 输出 `Wsl/Service/CreateInstance/E_ACCESSDENIED`；PowerShell 记录 WSL 进程退出值 `-1` |

因此不能实测 distro、g++/CMake/Ninja/strace/tc 版本，不能读取 `/tmp` 可用空间、文件系统类型或既有任务目录。任务提供的工具版本只是预期环境，未被本轮核实。由于必须先验证至少 5 GiB 余量、确认目录未存在并在 WSL ext4 建立隔离根目录，本轮没有创建目录、归档、构建、启动服务端或运行任何 case；没有尝试 Windows `/mnt/d` build，也没有换用其他 Linux 环境。

## 已完成的只读核对

- 执行前 `git rev-parse HEAD` 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，固定实现提交对象 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 存在，且为当前 HEAD 的祖先。
- `git diff --name-only 3b0820dab6dc149f549bd3e81ef403ea7953c4e9 HEAD -- src include` 无输出；`git diff --quiet … -- src include` 退出码 0。也就是说当前文档 HEAD 的相关源码树与固定实现提交一致；但这不替代从该提交创建并核验归档。
- 已读取本任务、`docs/DECISIONS/2026-09-23-lowlevel-design.md`、`docs/tasks/2026-09-23-lowlevel-design-01-result.md` 和 `docs/tasks/2026-09-23-lowlevel-design-qa-01-result.md`。QA 门禁要求 profile 必须限定到客户端 DATA fd；总 syscall 数不能冒充 DATA 成本。
- 输入 HEAD 已复核；index 查询为空。结果文件写前不存在。

## 测量与验收状态

| 项目 | 状态 | 结果 |
|---|---|---|
| 从固定 commit 创建 `git archive` | NOT RUN | 未生成 archive；SHA-256 无值 |
| WSL distro、ext4 `/tmp` 与剩余空间 | BLOCKED | WSL 无法启动；均未核实 |
| 隔离 build/source/results 根目录及随机端口 | NOT RUN | 未创建目录、未分配端口 |
| CMake configure/build、compiler/binary SHA | NOT RUN | 无构建产物 |
| `cpnetflux_file_transfer_smoke` | NOT RUN | 无退出码；不是 skip-as-pass |
| `cpnetflux_file_resume_smoke` | NOT RUN | 无退出码；不是 skip-as-pass |
| `cpnetflux_file_checksum_smoke` | NOT RUN | 无退出码；不是 skip-as-pass |
| 256 MiB、connections=1/8、每档 3 次 | NOT RUN | 无 payload、传输、hash 或 timing 样本 |
| client-only `strace -yy` DATA fd 归因 | NOT RUN | 无 trace；DATA syscall/frame 数、短写、CPU 秒/GiB 均无数据 |
| client-process wall/goodput 与离散度 | NOT RUN | 无 case 样本，不计算中位数或 range/median |

本轮没有任何失败传输、blocked case 或被跳过的测量；唯一阻塞是 WSL 服务拒绝访问。由于没有有效 profile 数据，本任务不能给出 sendmsg 优化 go/no-go，亦不能据此判断 syscall 成本。

## 边界与下一步

现有架构审查把 plain-TCP DATA header/payload vectored write 列为待本机 profile 验证的单文件候选。本结果不确认也不否定该候选。只有 WSL 可启动、隔离 ext4 `/tmp` 余量达到门槛、固定 commit archive 可核验并通过指定 build/smoke 后，才能续跑既定 client-only case；仍须用 `strace -yy` 按 data peer 排除控制 fd，并确认帧数和调用数可对应。若不能建立这种归因，结果必须标 PARTIAL，不能用总 syscall 数替代。

测量即使完成也只说明本机 loopback 的 syscall/CPU 机制，不代表深圳—上海 100 Mbps 性能或 GridFTP 对照。当前没有实验准入、sendmsg 实现授权或云端工作。

## 执行回执

- **实际输入实现 commit、source archive SHA-256、输出 HEAD：** 固定实现输入 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；archive 未创建，SHA-256 不适用；文档输入/输出 HEAD 均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- **WSL distro、工具/依赖版本、`/tmp` 空间和目录：** WSL 两个入口均 E_ACCESSDENIED；其他字段未核实；任务 ext4 根目录未创建。
- **CMake、build、CTest：** 全部 NOT RUN；无命令退出码。
- **case、端口、PID、传输/hash、DATA trace、CPU/goodput：** 无 case 启动；均无样本。
- **失败/blocked/skipped 与偏差：** WSL 服务拒绝访问为阻塞；没有把未运行项记作通过。未使用云端、SSH、GridFTP、其他构建环境、payload 或 cleanup。
- **CLI last-message：** 本轮没有调用 Codex CLI，因此没有生成 last-message 文件；如后续调用 CLI，摘要路径应使用 `docs/tasks/2026-09-23-lowlevel-single-profile-01-last-message.md`，与本完整结果路径不同。
- **下一步：** 恢复本机 WSL 服务访问后，重新执行 distro、空间和既有目录门禁，再从固定 commit 归档开始。继续沿用本任务的 5 GiB 余量及隔离目录约束。
