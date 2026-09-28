# LOWLEVEL-SINGLE-PROFILE-01：单文件 plain TCP syscall 基线

- 状态：in_progress
- 路线版本、任务版本：`R2026-09-23.4 / v1`
- 发起人：00 总指挥；执行角色：02 实验与证据；验收角色：00 总指挥，04 复核统计口径
- 目标及理由：按 04 的实现前门禁，在本地 WSL Linux 对固定实现提交的单文件 plain TCP DATA 路径做 client-only syscall/CPU 基线，判断每 DATA 分开发送 header 与 payload 的成本是否值得投入向量写实现。
- 非目标：不修改源码、测试、runner、配置默认值；不实现 sendmsg、不运行云端/深圳—上海传输、不访问服务器、不改旧证据、不操作云端数据、不运行 GridFTP 对照、不提交或暂存 Git。
- 输入 commit（完整 SHA）、工作树状态：源码固定为最近实现提交 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；当前文档 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。执行前实测二者与代码树关系；用 `git archive` 从固定实现提交生成本地源码输入并记录归档 SHA-256，不从 dirty 文档工作树构建。
- 必读资料和证据路径：本任务；`docs/DECISIONS/2026-09-23-lowlevel-design.md`；`docs/tasks/2026-09-23-lowlevel-design-01-result.md`；`docs/tasks/2026-09-23-lowlevel-design-qa-01-result.md`；按需读取 `CMakeLists.txt`、file client/server CLI、现有 file transfer smoke。
- 允许修改的文件/目录；禁止修改的资源：仓库只新增 `docs/tasks/2026-09-23-lowlevel-single-profile-01-result.md`。构建、payload、trace、二进制和原始 stdout/stderr 仅放 WSL ext4 下独立 `/tmp/cpnetflux-runs/LOWLEVEL-SINGLE-PROFILE-01/`（创建前检查同名路径和空间；已有目录不覆盖/清理，另选唯一后缀目录）。禁止其他仓库文件、角色回执、BOARD/ROSTER、Git index、云端、凭据和历史 payload。
- 工作分支/worktree：本任务只读固定 commit 的源码；无源代码修改，不建代码 worktree；WSL ext4 隔离 build/source/results 目录，不能从 Windows `/mnt/d` build path 计性能。
- 前置条件、环境占用和停止条件：本地 WSL 已安装 g++ 11.4、CMake 3.22.1、Ninja 1.10.1、strace 5.16、tc。先检查 WSL `/tmp` 挂载可用空间，至少保留 5 GiB 再开始；依赖不能构建时停止并报具体包/下载错误，不访问云端补环境。一次只启动一个本地 case/服务端；使用随机空闲端口并记录。数据通道 plain TCP/TLS off，不能打印或读取任何凭据。
- 验收标准及实际可执行命令：从固定 commit 归档并记录 SHA、CMake 配置、compiler、binary SHA；build 后至少通过 `cpnetflux_file_transfer_smoke`、`cpnetflux_file_resume_smoke`、`cpnetflux_file_checksum_smoke`。随后同一台 WSL、同一 source、客户端/服务端参数、256 MiB 固定 payload、64 KiB buffer、固定 chunk/checksum、fresh target 做 connections=1 与 8 各 3 次 client-only trace。strace 限定 trace client 及其线程，不 trace server；用 `-yy` 识别 data fd/peer，单列 DATA header/payload 的 `sendto/sendmsg/write/writev` 次数和时间，排除控制通道 syscall；记录每 DATA frame 调用数、CPU 秒/GiB、client-process wall/goodput、exit/hash。检查 short write/实际帧数可从现有日志或 trace 识别；没有 client-only fd 归因时明确 PARTIAL，不用总 syscall 数冒充 DATA 成本。输出中位数与 range/median；本地 loopback 仅回答 syscall/CPU 机制，不外推 WAN。给出 go/no-go 结论：何种结果才值得派 sendmsg A/B 实现任务。记录全部失败/blocked/skipped，不以 skip 当 pass。
- 云端运行目录、端口、资源上限、清理与归档方案（如适用）：不适用，严禁 SSH。WSL case 只使用本任务自有临时目录，先核对 payload、日志、build 均在 ext4 `/tmp`；保留带 hash 和命令的 evidence，结束后不清除 task 根目录。
- 预期产物路径：本地报告 `docs/tasks/2026-09-23-lowlevel-single-profile-01-result.md`；WSL 原始证据保留在实际登记的任务目录。

## 派给角色的消息

请续接既有 02 实验聊天。按本地 WSL Linux 流程从固定 `3b0820d` 归档构建并仅测 plain TCP file transfer；先核实依赖、空间、随机端口与 build 配置。独立启动服务端，只对客户端进程/线程运行 strace 并用 `-yy` 分离 data peer，避免把 control socket 或 server syscall 混作 DATA。只读任务，不改仓库代码和旧证据。结果写入授权 result，CLI `--output-last-message` 使用另一个 `docs/tasks/2026-09-23-lowlevel-single-profile-01-last-message.md`。本地 loopback 不作 100 Mbps 跨域性能结论。

## 执行回执

- 实际输入实现 commit、source archive SHA-256、输出 HEAD：
- WSL distro、工具/依赖版本、`/tmp` 空间和目录：
- CMake 配置、target build、定向 CTest 命令/退出码：
- 每个 case 的参数、端口、PID/归属、重复和 transfer/hash 证据：
- client-only data-fd syscall/frame、CPU/GiB、client-process wall/goodput：
- 失败/blocked/skipped 与偏差：
- sendmsg 实现建议、收益阈值与下一任务：

## 验收与路线变化

00 验收；04 可按需 spot-check trace 分类。profile 若不能识别 data fd，结果保持 PARTIAL，先修观测口径，不派优化实现。即使 go，也只授权独立 `codex/<task-id>` worktree 内窄的 sendmsg A/B 实现，不能直接进入云端实验。
