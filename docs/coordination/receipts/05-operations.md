# 05 云端运维与发布接手回执

- 角色：05 云端运维与发布；交付对象：00 总指挥。
- 任务：首次只读接手，SETUP-01 角色回执；待总指挥验收，不代改任务板状态。
- 日期：2026-09-17，Asia/Shanghai（UTC+08:00）。本地核查时间 21:51:43、21:56:13；远端采样约 21:55:37。
- 输入提交：`51820e7aed918fd9e6840ef834acd1705c3848de`，来自本次本地命令，不沿用资料中的旧 HEAD。
- 输出证据：本文件记录命令、采样结果、状态摘要和后续边界；本次不另建日志或证据目录。

## 1. 修改前确定的范围与验收

范围：读取协作入口、角色 prompt、工程规范和 runner 资源治理逻辑；核对本地 HEAD/status；使用现有 SSH 身份只读读取两端 hostname、磁盘、历史 Git 状态、监听服务和构建依赖；仅写本回执。

非目标：不执行 ENV-01 深度目录盘点或清理，不生成源码归档，不构建、不运行 CTest/实验、不安装软件、不启停服务、不触碰受保护项目，不改 BOARD/ROSTER，不切分支、不操作 index、不 git add/commit。

唯一受影响文件：`docs/coordination/receipts/05-operations.md`（接手时不存在）。

验收标准：两端取得带时间的只读快照；失败命令如实记录；区分依赖存在、构建成功和功能验收；明确上海资源阻塞及建议 ENV-01 范围；回执通过 UTF-8、内容完整性和空白检查，最终复核 HEAD/status。按用户首次接手边界，不启动 CMake/CTest。

## 2. 本地仓库与已读资料

仓库实测为 `D:/Project/CPNetFlux`，分支为 `main...legacy-reference/main [ahead 10]`。21:56:13 复核 HEAD 与首次读取一致。

接手时已有以下其他协作改动，本角色未改写：

```text
 M docs/coordination/ROSTER.md
 M docs/coordination/START_HERE.md
 M docs/coordination/prompts/00-commander.md
?? docs/coordination/DISPATCH.md
?? docs/coordination/receipts/02-experiments.md
?? docs/coordination/receipts/03-implementation.md
?? docs/coordination/receipts/04-quality.md
```

已读：根 AGENTS.md；协作目录 START_HERE.md、PROJECT_BRIEF.md、ENVIRONMENT.md、BOARD.md、prompts/05-operations.md、receipts/README.md；docs/ENGINEERING.md、docs/RESEARCH_BASELINE.md。按任务读取了 CMakeLists.txt 的平台/依赖逻辑、tools/experiments/gridftp_compare/runner.py 的空间门禁、路径、清理与证据回收逻辑，检索了相关 smoke token 要求。

资料中的历史 go 不代表生产 readiness。新 profiling、固定构建矩阵和全套 CTest 的状态未由本次只读盘点重新验收。

## 3. SSH 实测与客户端限制

- 系统自带 `C:\Windows\System32\OpenSSH\ssh.exe` 文件版本为 9.5.6.2；本次调用 `-V` 和两个别名命令均立即返回 255，stdout/stderr 为空。在普通 PowerShell、PTY 和 Python subprocess 中均可复现版本查询失败。根因未定位，未修复或修改客户端。
- 已安装的 `D:\Software\Git\usr\bin\ssh.exe` 返回 `OpenSSH_10.0p2, OpenSSL 3.5.3 16 Sep 2025`，版本查询退出码 0。
- 使用此现有客户端，显式复用用户原有 SSH 配置和两个别名，两端只读盘点退出码均为 0，主机名与交接信息一致。未输出完整 SSH 配置、身份文件内容、进程参数、环境变量或 token。
- 启用 BatchMode=yes、StrictHostKeyChecking=yes、ConnectTimeout=15；未关闭主机密钥校验。目前可用的是显式 Git OpenSSH 路径，不能报告“裸 ssh 命令已恢复”。

实际 PowerShell 调用结构如下；remoteReadOnly 是只驻留内存、通过 stdin 传入的 Python 只读探针，检查命令见第 8 节：

```powershell
$remoteReadOnly | & 'D:\Software\Git\usr\bin\ssh.exe' -F "$env:USERPROFILE/.ssh/config" -o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes -o ServerAliveInterval=10 -o ServerAliveCountMax=2 gridflux-beta-shenzhen 'python3 -'
$remoteReadOnly | & 'D:\Software\Git\usr\bin\ssh.exe' -F "$env:USERPROFILE/.ssh/config" -o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes -o ServerAliveInterval=10 -o ServerAliveCountMax=2 gridflux-beta-shanghai 'python3 -'
```

## 4. 两端资源与历史仓库快照

| 项目 | 深圳 | 上海 |
|---|---|---|
| SSH 别名 | gridflux-beta-shenzhen | gridflux-beta-shanghai |
| 实测主机名 | iZwz9bgztwf1tic26q48pjZ | iZuf6ja2uqvjzh635cugzmZ |
| 远端 UTC 采样开始 | 2026-09-17 13:55:37.018444 | 2026-09-17 13:55:37.535760 |
| 远端 UTC 采样结束 | 2026-09-17 13:55:37.118263 | 2026-09-17 13:55:37.657521 |
| 系统 / 内核 | Ubuntu 22.04.5 LTS / 5.15.0-181-generic x86_64 | 同左 |
| /、/tmp、历史仓库所在设备 | /dev/nvme0n1p3，ext4，挂载 / | 同左 |
| 文件系统总字节 | 105286258688 | 105286258688 |
| 已用字节 | 45942800384 | 105269481472 |
| 可用字节（df Avail） | 54830804992（约 51.07 GiB） | 0 |
| df -hT 摘要 | 99G / 已用 43G / 可用 52G / 46% | 99G / 已用 99G / 可用 0 / 100% |
| inode 使用 | 247798 / 6532624，4% | 231539 / 6532624，4% |
| 可见 CPU 数 | 2 | 2 |
| 内存总字节 / 可用字节 | 3664908288 / 2923261952 | 3664908288 / 3023585280 |
| Swap | 0 | 0 |
| load average（1/5/15 分钟） | 0.00 / 0.00 / 0.00 | 0.00 / 0.00 / 0.00 |
| 历史 Git HEAD | 16b377359494f19f386ba5d375d353449e45f7a0 | 同左 |
| 历史 status 条数 | 78：49 条工作树修改、29 条未跟踪 | 74：47 条工作树修改、27 条未跟踪 |

历史 Git 检查仅针对 `/root/projects/GridFlux-Beta`，使用 `--no-optional-locks -c core.fsmonitor=false` 避免 status 的可选 index 刷新和 fsmonitor hook。条数按 `--porcelain=v1 --untracked-files=normal` 统计，未跟踪目录算一条，不等于内部文件数。

本次完整 status 输出逐行保留空格、统一 LF 并补终止换行后计算的 SHA-256（仅标识 status 文本，不是源码归档 hash）：

- 深圳：`f85e788506293ae375f276bbc9433ec6c998c7d4554a52ee98f09fa29c88c99c`。
- 上海：`25c09cc2b08eae6510897b7b6381a1c53a406f35d65c4d3a2d9b33a0b8534c12`。

上海可用空间仍为 0，继续阻塞新实验和构建；inode 未耗尽。深圳扣除 10 GiB 保留量后约剩 41.07 GiB，尚未扣除源码、构建、证据和峰值 payload，不等于已批准实验配额。两端 /tmp 与 / 同盘，不能将两处可用空间相加。

## 5. 现有服务与端口

本次只读取 `ss -H -ltnp` 和 `ps -eo pid=,ppid=,user=,comm=`；未读取 cmdline/environ，未连接 GridFTP 执行测试，未停止服务。

| 主机 | 监听端口 | 进程名 / PID | 边界 |
|---|---|---|---|
| 深圳 | *:22310 | globus-gridftp- / 2493350 | 已有 GridFTP，保留 |
| 上海 | *:2811 | globus-gridftp- / 339900 | 已有 GridFTP，保留 |
| 深圳 | *:21 | vsftpd / 794 | 已有 FTP，保留 |
| 上海 | *:21 | vsftpd / 788 | 已有 FTP，保留 |
| 深圳 | IPv4/IPv6 :22 | sshd / 3747193 | 保留 |
| 上海 | IPv4/IPv6 :22 | sshd / 302699 | 保留 |

其他监听：深圳 loopback 39395（code-1b6a188127 / PID 5893）、33399（code-08d4889f9e / PID 3447817）；两端 loopback DNS 53（深圳 systemd-resolve / PID 2104297，上海 / PID 84661）。

交接资料中的深圳数据范围 23300–23811、上海 32000–32511 继续按既有服务占用范围避让；本次未读取服务配置重验范围，瞬时未见这些端口监听不代表可以分配。进程名筛选未见 CPNetFlux/gridflux/iperf 用户进程；模糊 fio 匹配到的 vfio-irqfd-clea（PID 98）为内核线程名，不能当作 fio 实验。精简快照不能证明环境没有任何 Python/shell 编排作业，下一任务仍须核对批次归属。

## 6. 可用构建能力与依赖边界

两端本次查询结果一致：

| 项目 | 版本 / 状态 |
|---|---|
| CMake / CTest | 3.22.1 / 3.22.1 |
| g++ | Ubuntu 11.4.0-1ubuntu1~22.04.3，11.4.0 |
| Ninja / GNU Make | 1.10.1 / 4.3 |
| Python / Git | 3.10.12 / 2.34.1 |
| pkg-config | 0.29.2 |
| zlib | pkg-config 1.2.11；zlib1g-dev 1:1.2.11.dfsg-2ubuntu9.2 |
| OpenSSL | pkg-config 3.0.2；libssl-dev 3.0.2-0ubuntu1.29 |
| spdlog | pkg-config 1.9.2；libspdlog-dev 1:1.9.2+ds-0.2 |
| GTest | pkg-config 1.11.0；libgtest-dev 1.11.0-3 |
| liburing | pkg-config 2.0；liburing-dev 2.1-2build1 |

上述五个 dev 包均为 install ok installed；/usr/include 下 zlib.h、openssl/ssl.h、liburing.h、spdlog/spdlog.h、gtest/gtest.h 均存在。/usr/local/include/liburing.h 不存在。pkg-config 与包管理器的 liburing 版本按各自原始结果记录，不合并。

**新事实：两端现在存在 liburing 开发包和系统头文件。** 不能继续把“当前缺 liburing”作为已核实事实，也不能反推旧实验当时具有此依赖。安装时间未查明，本角色未安装任何软件。当前仍缺固定提交的真实 backend 链接、内核运行及测试证据，io_uring 验收保持未验证。

本地 CMake 配置要求 Linux、C++20、CMake >= 3.20，必需 Threads/ZLIB；spdlog/GTest 优先系统包，未找到才 FetchContent v1.17.0；TLS 默认 ON，io_uring 默认 OFF。io_uring 请求 ON 但未找到库时仍可产生 unavailable stub，配置成功不能当作 backend 可用。本次仅确认版本与依赖存在，没有配置/编译/链接，未验证依赖下载或 Windows 原生构建。

## 7. runner 静态核查与运维缺口

针对输入提交的 tools/experiments/gridftp_compare/runner.py，以下均为源码阅读结果，未运行 runner：

- disk_budget_error（约 2403 行）检查本地 output_dir 文件系统和远端 /tmp，默认 min-free-gib=10.0；未自动叠加每 case 峰值 payload、构建和证据预算，也未覆盖可能处于其他挂载点的全部实际输出路径。
- 每 case 空间门禁位于 run_experiment 循环；link baseline、外部 GridFTP 服务准备、dataset catalog 和 preflight 在循环之前。必须在启动整个 runner 前完成运维资源预检。
- cleanup_case_resources（约 2422 行）默认删除本地 source_payload/dest_payload 和计算出的远端 case root；retain-payloads 可跳过。清理函数本身未强制核验本地证据回收/hash 成功，也未检查远端清理返回码。
- 主循环 cleanup 在本轮 results.csv/summary.csv 写出之前（约 2871–2875 行）。正常路径有 manifest 获取，finally 有日志回收，但不能据此认定已满足“证据先持久化、回收核验后再删 payload”的运维要求。
- io_uring_available（约 2684 行）以本地/远端二进制 ldd 是否含 liburing 为门禁；开发包存在不足以替代该检查或真实运行验收。

建议总指挥/04 在后续固定构建任务中验收这些边界；本次未改 runner，不能宣称资源治理闭环。

## 8. 本次实际命令与失败记录

本地执行 git rev-parse --show-toplevel、git rev-parse HEAD、git status --short --branch；结束前使用 git --no-optional-locks 复核。首次中文 Get-Content 显示编码不正确，已用显式 UTF-8 重新读取，未改动文档编码。

远端通过 Python subprocess.run 参数数组执行下列只读命令，超时 25 秒（ps 为 15 秒）；两端这些检查返回码均为 0。版本命令只回显第一行；ps 只显示匹配的进程名元数据。

```text
hostname
cat /etc/os-release
uname -srmo
df -hT / /tmp /root/projects/GridFlux-Beta
df -B1 --output=source,fstype,size,used,avail,pcent,target / /tmp /root/projects/GridFlux-Beta
df -i / /tmp
nproc
free -b
cat /proc/loadavg
git --no-optional-locks -c core.fsmonitor=false -C /root/projects/GridFlux-Beta rev-parse HEAD
git --no-optional-locks -c core.fsmonitor=false -C /root/projects/GridFlux-Beta status --porcelain=v1 --untracked-files=normal
ss -H -ltnp
ps -eo pid=,ppid=,user=,comm=
cmake --version
ctest --version
g++ --version
ninja --version
make --version
python3 --version
git --version
pkg-config --version
pkg-config --modversion zlib
pkg-config --modversion openssl
pkg-config --modversion liburing
pkg-config --modversion spdlog
pkg-config --modversion gtest
dpkg-query -W -f=${Package}\t${Version}\t${Status}\n zlib1g-dev libssl-dev liburing-dev libspdlog-dev libgtest-dev
```

dpkg-query 的格式是一个 subprocess 参数，含实际制表符/换行，未经 shell 插值。时间取 Python UTC 时钟；头文件检查用 os.path.isfile。探针只从 stdin 执行，没有创建远端脚本。

调用准备阶段有 JavaScript 模板插值错误和 PowerShell here-string 结束标记错误，均发生在 SSH 启动前；已修正内存命令，未写文件或改变远端。系统 SSH 的 255 失败及现有替代客户端见第 3 节。

## 9. 阻塞、建议 ENV-01 范围与交接

当前阻塞：

1. 上海 df Avail=0，恢复前不启动新实验/构建。
2. 原生 Windows SSH 调用异常；后续使用裸 ssh/scp 的脚本须先解决或显式指定已验证客户端，不能假定 runner 自动使用此回退。
3. 固定提交源码归档、archive SHA-256、构建参数、二进制 SHA-256、完整 CTest 和 token 安全注入均尚未交付。本次未读取/注入测试 token。
4. 新 profiling 和固定性能矩阵本次未验收；runner 预算与证据清理边界需后续任务落实。

建议 ENV-01 由总指挥下发明确的只读盘点任务，范围如下（本次尚未执行）：

- 以上海为主、深圳仅做必要对照；先枚举 /tmp 和 /root/projects 一级条目元数据，不递归未知目录、不跟随符号链接，再对白名单内已确认归属的 CPNetFlux/GridFlux 实验目录逐项统计占用。
- `/root/projects/CPSS(DCC)` 完全跳过；science-compressor 及其他未知项目不纳入。禁止广扫父目录时顺带递归受保护目录。历史 GridFlux-Beta 的源码、dirty 改动和实验结果均须保留，不能将整个目录作为清理候选。
- /srv/gridflux-gsi 仅在明确列入任务单且确认服务归属后盘点相关实验 payload；不把服务安装、配置、凭据或活动任务目录列为可删项。现有数据端口范围继续避让。
- 每项候选记录绝对路径、属主、任务/run_id、挂载点、实际/表观大小、可再生方式、关联 PID/活动状态、log/hash/summary/manifest 完整性及本地备份对应关系。`D:\Project\GridFlux Beta\_server_backups` 只是备份入口，本次未核验覆盖范围和完整性。
- 核验备份可读及 SHA-256 后，形成逐路径清单：预计可释放字节、保留证据、影响、是否已获明确清理授权。无法确认归属/备份者保持保留；ENV-01 本身不删除。
- 空间恢复目标按每个相关挂载点 `可用 >= 10 GiB + 本任务峰值 payload + 构建/证据等新增预算` 计算；确认一套环境仅一个实验批次。隔离根 `/tmp/cpnetflux-runs/<task-id>` 和回收根 `D:\Project\CPNetFlux-evidence\<task-id>` 尚未创建，使用前检查归属、挂载点和本地空间。

本次未进入、修改或清理受保护项目，未停止任何进程。交接状态：首次盘点结果已写回执，ENV-01 仍待总指挥派发；未自动执行任务板，未向其他聊天派发消息或声称其他角色已收到/已执行。等待 00 总指挥下发具体运维任务。

## 10. 回执验收记录

已执行内联 Python 门禁：严格 UTF-8、必要证据字段、10 个小节、Markdown 围栏配对、末尾换行、行尾空白和私钥块检查，结果 `receipt_content_gate: PASS`。Git 空白门禁为 `git --no-optional-locks -c core.autocrlf=false diff --no-index --check -- /dev/null docs/coordination/receipts/05-operations.md`，stdout/stderr 均空，结果 `receipt_git_whitespace_gate: PASS`。原始 no-index 返回码为 1（新增文件相对空文件有差异），不将此返回码误报为文本错误。

首次 Python 门禁因 PowerShell 向 stdin 传递中文时使用默认编码，把断言文字变成问号而失败；回执 UTF-8 内容正常。仅为当前命令进程显式设置 `$OutputEncoding` 为 UTF-8 后重跑通过，未修改系统编码或其他文件。首次 Git 检查有 LF/CRLF 转换提示；以本次命令级 core.autocrlf=false 重跑后无诊断，未修改 Git 配置。

22:02:44 复核 HEAD 仍为输入提交；status 中新增本回执，以及其他协作产生的 `docs/coordination/SETUP_STATUS.md` 修改。本角色未写该文件，未改其他角色文档，未操作共享 index。回执补记后再次执行同一文档门禁和 HEAD/status 复核。

CMake/CTest 未运行：首次接手明确禁止启动构建/实验，文档回执使用脚本门禁验收。
