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

## 11. ENV-01 v1 实时重验（2026-09-23）

任务单：`R2026-09-23.1 / v1`；本地输入 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。本次只更新本回执，不切分支、不操作共享 index、不改云端文件。

### 本地状态

2026-09-23 09:33:22（Asia/Shanghai）执行 `git rev-parse --show-toplevel`、`git rev-parse HEAD`、`git status --short --branch`，均返回码 0。当前 HEAD 与任务输入一致；当前事实不是干净树：

```text
## main...legacy-reference/main [ahead 11]
 M docs/coordination/BOARD.md
?? docs/tasks/2026-09-23-env-01.md
?? docs/tasks/2026-09-23-perf-matrix-plan.md
?? docs/tasks/2026-09-23-perf-route.md
```

上述改动由总指挥任务准备产生，本角色未修改。总指挥派单中的“工作树干净”与本次重新读取结果不一致，后续归档须以本次实时 `git status` 为准。

### SSH、时间与远端总览

系统 OpenSSH 名称被本地沙箱的 `ssh.bat/ssh.cmd` 包装器遮蔽；Git OpenSSH 本轮出现 Cygwin signal-pipe `Win32 error 5`。改用显式 `cmd.exe /d /c C:\Windows\System32\OpenSSH\ssh.exe` 后，两端连接均成功，仍使用 `BatchMode=yes`、`ConnectTimeout=15`、`StrictHostKeyChecking=yes`、`ServerAliveInterval=10`、`ServerAliveCountMax=2`，未关闭主机密钥检查。

远端只读探针经 stdin 临时传入 Python，没有创建远端脚本；外层 SSH 和探针内每条检查均返回码 0。核心采样窗口（UTC）如下：

| 主机 | hostname | 探针开始 | 探针结束 | SSH 外层 |
|---|---|---|---|---|
| 深圳 | `iZwz9bgztwf1tic26q48pjZ` | 2026-09-23 01:36:57.343254 | 2026-09-23 01:36:57.886742 | 0 |
| 上海 | `iZuf6ja2uqvjzh635cugzmZ` | 2026-09-23 01:37:02.814846 | 2026-09-23 01:37:02.904129 | 0 |

实际调用形态为：

```powershell
$remoteReadOnly | cmd.exe /d /c 'C:\Windows\System32\OpenSSH\ssh.exe -o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes -o ServerAliveInterval=10 -o ServerAliveCountMax=2 gridflux-beta-shenzhen python3 -'
$remoteReadOnly | cmd.exe /d /c 'C:\Windows\System32\OpenSSH\ssh.exe -o BatchMode=yes -o ConnectTimeout=15 -o StrictHostKeyChecking=yes -o ServerAliveInterval=10 -o ServerAliveCountMax=2 gridflux-beta-shanghai python3 -'
```

未打印 SSH 配置、身份、命令行参数、环境变量或 token。没有创建本地/远端独立证据文件；本回执是任务允许的唯一持久化产物，探针 stdout 以本节摘要和命令时间为追溯依据。

### 空间、主机资源与挂载

两端都是 Ubuntu 22.04.5 LTS、Linux 5.15.0-181-generic x86_64、2 个可见 CPU、无 swap；`/` 与 `/tmp` 均为 `/dev/nvme0n1p3` ext4，同一挂载点，不能相加。

| 指标 | 深圳 | 上海 |
|---|---:|---:|
| `df -hT` | 99G / 已用 42G / 可用 52G / 45% | 99G / 已用 99G / 可用 0 / 100% |
| 总字节 | 105286258688 | 105286258688 |
| 已用字节 | 45057716224 | 105269481472 |
| `df` 可用字节 | 55715889152（51.89 GiB） | 0 |
| inode 使用 | 239726 / 6532624（4%） | 231448 / 6532624（4%） |
| 内存总字节 | 3664908288 | 3664908288 |
| 内存可用字节 | 2948841472（2.75 GiB） | 3011235840（2.80 GiB） |
| load average | 0.28 / 0.07 / 0.02 | 0.08 / 0.02 / 0.01 |

深圳扣除每挂载点 10 GiB 保留后约剩 41.89 GiB，尚未计入固定构建和峰值 payload；上海仍为 0 GiB，后续隔离构建/实验继续 blocked。`/tmp/cpnetflux-runs` 两端均不存在；本任务未创建运行目录。

### 历史树、依赖与构建能力

两端 `/root/projects/GridFlux-Beta` 均以 `git --no-optional-locks -c core.fsmonitor=false` 读取：HEAD `16b377359494f19f386ba5d375d353449e45f7a0`，命令返回码 0。深圳 porcelain 条数 78（` M` 49、`??` 29），status 文本 SHA-256 `f85e788506293ae375f276bbc9433ec6c998c7d4554a52ee98f09fa29c88c99c`；上海 74（` M` 47、`??` 27），status 文本 SHA-256 `25c09cc2b08eae6510897b7b6381a1c53a406f35d65c4d3a2d9b33a0b8534c12`。未执行 reset、clean、写入或构建。

两端版本探针返回码均为 0：CMake/CTest 3.22.1/3.22.1，g++ 11.4.0，Ninja 1.10.1，GNU Make 4.3，Python 3.10.12，Git 2.34.1，pkg-config 0.29.2；pkg-config 为 zlib 1.2.11、OpenSSL 3.0.2、liburing 2.0、spdlog 1.9.2、GTest 1.11.0。`zlib1g-dev`、`libssl-dev`、`liburing-dev`、`libspdlog-dev`、`libgtest-dev` 均为 `install ok installed`；所需头文件存在，`/usr/local/include/liburing.h` 不存在。

这些是工具链/依赖存在证据，不是 CPNetFlux 固定提交构建、二进制 hash、CTest 全绿或 io_uring runtime 验收。本任务按范围没有运行 CMake、CTest、链接或实验。

### 监听端口与活动进程

`ss -H -ltnp` 和 `ps -eo pid=,ppid=,user=,comm=` 返回码均为 0；未读取 cmdline/environ，未停止服务。

| 主机 | 监听及 PID | 结论 |
|---|---|---|
| 深圳 | SSH 22 / sshd 3747193；FTP 21 / vsftpd 794；GridFTP 22310 / globus-gridftp- 2493350 | 既有服务，保留 |
| 上海 | SSH 22 / sshd 302699；FTP 21 / vsftpd 788；GridFTP 2811 / globus-gridftp- 339900 | 既有服务，保留 |

当前精简进程筛选未见 CPNetFlux、iperf3 或 fio 进程；仅见上述服务和探针自身的短生命周期 `python3`。这不能替代读取未知编排任务的归属，下一次实际运行仍须按 PID/任务单确认。深圳 23300–23811、上海 32000–32511 数据端口范围按既有服务约定继续避让；本次 `ss` 未见这些端口瞬时监听，不把它们当作可分配端口。

### 上海一级空间盘点与候选

`os.scandir`/`os.lstat` 只读取 `/tmp` 和 `/root/projects` 一级条目元数据，不递归未知项目。上海 `/tmp` 有 96 个一级条目（25 个目录、71 个非目录），单个一级非目录项均小于 1 MiB，空间主要在目录内。上海 `/root/projects` 一级可见 `CPSS(DCC)`、`GridFlux`、`GridFlux-Beta`、`ce2s_second_revision`、`fast-data-transfer`、`science-compressor`；仅对 `GridFlux-Beta` 做了任务要求的 Git HEAD/status 读取，其他项目没有递归或候选清理判断。深圳一级项目可见 `CPSS(DCC)`、`GridFlux-Beta`、`dataset`、`science-compressor`；未对其未知目录递归盘点。

下表的大小是针对明确路径执行 `du -x -s` 的只读结果，`allocated` 为文件系统占用上限估算，`apparent` 为表观字节；路径属主证据仅为 lstat uid/gid=0，命名和时间戳是归属线索，不是备份证明。所有候选当前“可释放”均为 0，只有在独立备份、hash/manifest/summary 证据回收、PID/任务归属确认和逐路径授权后，才可申请释放表中的 allocated 上限。表中嵌套子路径不与其父路径相加。

| 候选路径 | allocated / apparent | 文件数 | mtime（UTC） | 再生性/备份关系/状态 |
|---|---:|---:|---|---|
| `/tmp/gridflux-gridftp-compare/20260916T114921Z` | 40445587456 / 40428005134（37.668 / 37.652 GiB） | 16895 | 2026-09-16 14:09:53 | 时间戳比较批次；可再生性和上海备份未确认，候选上限，当前保留 |
| `/tmp/gridflux-gridftp-compare/20260914T022424Z` | 22200086528 / 22183478452（20.675 / 20.659 GiB） | 15643 | 2026-09-14 04:25:22 | 时间戳比较批次；备份/任务归属未确认，候选上限，当前保留 |
| `/tmp/gridflux-gridftp-compare/20260902T083220Z` | 5380300800 / 5376893208（5.011 / 5.007 GiB） | 3372 | 2026-09-02 09:39:07 | 时间戳比较批次；备份/任务归属未确认，候选上限，当前保留 |
| `/tmp/gridflux-gridftp-compare`（含上列子目录） | 68846399488 / 68808620519（64.118 / 64.083 GiB） | 36000 | 2026-09-16 11:50:46 | 父目录总量；不得与子目录相加，含 `data`/`gsi-roundtrip*`/`logs`/`preflight`，服务关系未确认 |
| `/tmp/gridflux-compare-fresh-off-global-remote` | 539086848 / 538246777（0.502 / 0.501 GiB） | 596 | 2026-09-14 08:30:20 | 历史比较目录；无直接备份映射，候选上限，当前保留 |
| `/tmp/gridflux-compare-fresh-one-alias-remote` | 269545472 / 269125729（0.251 / 0.251 GiB） | 298 | 2026-09-14 08:23:00 | 历史比较目录；归属/备份未确认，当前保留 |
| `/tmp/gridflux-compare-fresh-reuse-worker-remote` | 269512704 / 269095155（0.251 / 0.251 GiB） | 298 | 2026-09-14 08:36:24 | 历史比较目录；归属/备份未确认，当前保留 |
| `/tmp/gridflux-compare-fresh-download-reuse-remote` | 268820480 / 268816926（0.250 / 0.250 GiB） | 150 | 2026-09-14 08:40:45 | 历史比较目录；归属/备份未确认，当前保留 |
| `/tmp/gridflux-recovery-demo` | 838938624 / 838909194（0.781 / 0.781 GiB） | 16 | 2026-09-08 03:14:04 | recovery demo；证据/归属未确认，当前保留 |
| `/tmp/gridflux-retest-build` | 11489280 / 11290993（0.011 / 0.011 GiB） | 90 | 2026-09-16 11:40:19 | 目录内为 CMake/Ninja 和 GridFlux 二进制；不是本地固定 CPNetFlux 构建，当前保留 |
| `/tmp/gridflux-tree-private-source-20260829T085304Z` | 1200128 / 1191958（0.001 / 0.001 GiB） | 4 | 2026-08-29 08:53:05 | private source 命名；归属/备份未确认，当前保留 |
| `/tmp/pytest-of-root` | 8126464 / 6334537（0.008 / 0.006 GiB） | 473 | 2026-09-16 07:01:56 | 测试临时目录；无任务归属/证据映射，当前保留 |

`/tmp/gridflux-gridftp-compare` 的一级子目录分解显示：20260916T114921Z 约 37.668 GiB、20260914T022424Z 约 20.675 GiB、20260902T083220Z 约 5.011 GiB；其余时间戳子目录约 64–128 MiB，`gsi-roundtrip-ready-1788260965` 约 64 MiB，`logs` 约 12.8 MiB，`preflight` 约 128 MiB。大父目录内含 `server.pid`、`server-secure.launcher.pid`、GSI roundtrip 和 data 命名，不能将整个父目录自动视为可再生 payload。

本地只读检查发现 `D:\Project\GridFlux Beta\_server_backups\2026-09-16_shenzhen`，一级含 `GridFlux-Beta`、`reports`、`science-compressor`、`scripts`；未发现名为 Shanghai 的备份入口，也未建立上述上海 run ID 与备份条目的逐项 SHA/manifest 对应。`D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results` 存在，是分析证据入口，不等于远端 payload 备份。因而即使以三大 run 的 allocated 合计约 68025974784（63.354 GiB）作为潜在回收上限，当前批准回收仍为 0 GiB。

明确排除：`/root/projects/CPSS(DCC)` 只在 `/root/projects` 一级条目枚举中出现，未进入、未读取内容、未递归统计；`/tmp/dcc_formal_gridflux` 与 `/tmp/gridflux_cpss_sampling_dedup_matrix` 未执行 `du`；`science-compressor`、`GridFlux`、`dataset`、`ce2s_second_revision`、`fast-data-transfer`、`/srv/gridflux-gsi` 和其他未知目录未递归盘点、未列为可删候选。`/root/projects/GridFlux-Beta` 未执行 `du`、清理或改写；GridFTP/FTP 服务和端口未停止或改配。

### ENV-01 结论与交接

1. SSH 两端可达，实时 hostname/df/CPU/内存/工具链/依赖/端口/历史 HEAD/status 均有新鲜返回码 0 证据；SSH 可达和 liburing-dev 存在仍不等于构建/runtime 验收。
2. 上海 `/` 与 `/tmp` 仍为 0 可用空间，后续隔离构建预算不满足“至少 10 GiB + 峰值 payload”；任务继续 blocked。深圳具备约 51.89 GiB 原始可用空间，保留 10 GiB 后才约 41.89 GiB，仍需另计构建和 payload。
3. 已形成逐路径只读候选方案，但没有任何路径获得清理授权；本任务不标记空间恢复，不删除、不移动、不回收、不杀进程。
4. 下一步需由总指挥单独派发逐路径清理任务：先确认任务/服务归属，核验本地或受控备份的日志、hash、summary、manifest 与 SHA-256，再逐路径批准；完成证据回收和磁盘复验后，才可重新评估隔离构建。

本轮实际探针、`du`、`find`、Git、`ss`、依赖命令及本地备份一级盘点均只读；本回执是唯一更新文件。ENV-01 未满足空间恢复，状态保持 `in_progress`，等待 00 总指挥验收。

### 最终稳定性复核修订

2026-09-23 01:45:05–01:45:14 UTC 使用同一原生 OpenSSH 调用，在每台主机上间隔约 2 秒连续执行两次 `df -B1 --output=source,fstype,size,used,avail,pcent,target / /tmp` 和 `git ... status --porcelain=v1 --untracked-files=normal`；两次输出均一致，外层 SSH 与各命令返回码均为 0。最终预算依据为：

- 深圳：已用 `48259616768` 字节，可用 `52513988608` 字节（约 48.88 GiB），48%；历史 HEAD 仍为 `16b377359494f19f386ba5d375d353449e45f7a0`，status 78 条，稳定 hash `f85e788506293ae375f276bbc9433ec6c998c7d4554a52ee98f09fa29c88c99c`。
- 上海：已用 `105269481472` 字节，可用 0，100%；历史 HEAD 相同，status 74 条，稳定 hash `25c09cc2b08eae6510897b7b6381a1c53a406f35d65c4d3a2d9b33a0b8534c12`。

01:44:20–01:44:25 UTC 的一次短探针曾在相同条数下得到不同 status hash（深圳 `cedcc38f6b4da2bd18fede11dfac5267802c74a232748bfda9c3e2a53d31493f`、上海 `bff911ab6dcefe85cf8a217bf97969500a1cc33c1742639490530f87eebb7780`）；随后连续复核恢复到上述稳定 hash。该瞬时差异与深圳同时发生的约 2.30 GB 已用空间增长一起记录为外部写入/状态竞争风险，不能据此覆盖稳定快照；任何后续构建前仍需重新核查。

因此深圳早先表格中的 55,715,889,152 字节可用值已被最终复核值取代；上海空间阻塞结论不变。任务仍未执行清理，当前可释放量仍为 0 GiB。

### PERF-ENV-SPACE-PLAN-01 v1

- 路线/任务版本：`R2026-09-23.1 / v1`；输入本地 commit `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，本轮首尾 `git rev-parse HEAD` 均为该值，未 superseded。当前本地工作树已有其他任务修改；本节只追加本角色回执，未暂存或提交。
- 观察窗口：2026-09-23 03:54:40–03:58:47 UTC（11:54:40–11:58:47 中国标准时间）。有效的完整上海快照时间为 03:58:47 UTC。工具 stdout 留在本次执行记录中；按任务白名单未另建证据文件。
- SSH：Git OpenSSH `D:\Software\Git\usr\bin\ssh.exe -V` 在客户端启动失败，错误为 `couldn't create signal pipe, Win32 error 5`，退出 `-1073741502`，没有连接服务器。随后用现有 Windows OpenSSH `C:\Windows\System32\OpenSSH\ssh.exe -o BatchMode=yes -o StrictHostKeyChecking=yes -o ConnectTimeout=12 -o ServerAliveInterval=5 -o ServerAliveCountMax=2 gridflux-beta-shanghai <只读探针>`；退出 0，严格 host-key 检查与 BatchMode 保持启用，未输出凭据。一次较早的探针在本地辅助函数参数处退出 1；另一次嵌套 PowerShell 转义失败、没有执行 SSH。二者均未造成远端修改。

| 检查 | 命令/读取方式 | 结果与退出码 |
|---|---|---|
| 主机/系统时间 | `hostname -f; uname -srmo; date -Is` | `iZuf6ja2uqvjzh635cugzmZ`，Ubuntu 22.04 系 Linux 5.15.0-181-generic x86_64；03:58:47 UTC；0 |
| 文件系统 | `df -hT / /tmp; df -i / /tmp` | `/`、`/tmp` 同为 `/dev/nvme0n1p3 ext4`，99G 总、62G 已用、32G 可用、67%；inode 213893/6532624（4%）；0 |
| CPU/内存 | `nproc; grep ... /proc/meminfo; cat /proc/loadavg` | 2 CPU；MemTotal 3579012 kB、MemAvailable 2995224 kB、无 swap；load `0.16 0.05 0.01`；0 |
| 基础工具 | `command -v` 加各工具 `--version` | CMake/CTest 3.22.1、Ninja 1.10.1、G++ 11.4.0、pkg-config 0.29.2、Git 2.34.1、Python 3.10.12；0。依赖包版本本次没有得到有效输出，OpenSSL/zlib/liburing 等当前版本记为 unknown；工具存在不等于固定提交构建或 runtime 验收。 |
| 一级目录 | 只对 `/tmp`、`/root/projects` 使用 `os.listdir`/`os.lstat`；过滤 CPSS/DCC 名称后不对其 stat | `/tmp` 234 个可见一级项，`/root/projects` 4 个可见一级项；没有递归未知目录；SSH 探针 0 |
| 历史树 | `git -C /root/projects/GridFlux-Beta rev-parse HEAD` 与 porcelain 行数 | HEAD `16b377359494f19f386ba5d375d353449e45f7a0`；`-uno` 47 行、普通 porcelain 74 行（其中含未跟踪项）；组合探针 0。未读取状态正文、未执行 `du` 或写操作。 |
| 监听/进程 | `ss -H -ltnp`；名称筛选的 `ps -eo pid,ppid,user,comm,etimes` | 见下表；两个只读命令均 0 |
| 服务路径 | `/srv/gridflux-gsi` 的 `stat`、`findmnt -T`；不列内容 | uid/gid 997/997、mode 750，目录 lstat 12288 bytes；挂 `/` 同一 ext4；0。该值不是树大小。 |
| 已确认 run 路径 | `os.path.lexists('/tmp/gridflux-gridftp-compare/20260916T114921Z')` | 当前不存在；没有 `du` 或遍历；确认不存在后可释放量为 0，若将来重新出现则大小仍 unknown。 |

空间状态有明显外部变化：此前 ENV-01 稳定快照（01:45 UTC）上海可用为 0；03:54:40 UTC 的早期只读采样为 `35,908,001,792` bytes（约 33.44 GiB）；03:58:47 UTC `df -hT` 显示 32G。任务本身没有写盘，期间约 1 GiB 量级的可用空间下降和此前从 0 到约 33.44 GiB 的变化均无法归因到具体路径/进程。由于样本间有变化，32G 只代表该采样时点，不作为稳定容量保证。

监听端口和活动进程快照：

| 地址/端口 | 最小进程信息 | 归属与限制 |
|---|---|---|
| `0.0.0.0:22`、`[::]:22` | `sshd` PID 302699 | SSH 服务，保留 |
| `*:21` | `vsftpd` PID 788 | FTP 服务，保留 |
| `*:2811` | `globus-gridftp-` PID 339900 | 现有 GSI/GridFTP 服务，保留；`/srv/gridflux-gsi` 不列为候选 |
| `0.0.0.0:25252` | `gridflux-gridft` PID 768474，采样时 etimes 约 135 秒 | 新近活动监听，是否为服务或批次及所属目录未知；按条件停止相关目录占用检查，不探查 cmdline/environ、不停止进程。上海数据端口范围 32000–32511 继续保留；单次 `ss` 未见该范围监听不代表可分配。 |

名称筛选未发现 `cpnetflux`、iperf、fio（`vfio-irqfd-clear` 只是筛选 `fio` 子串时的误匹配）、cmake/ninja/make/ctest 批次。由于 PID 768474 归属未明，不据此断言环境空闲。观察到它以后没有执行目录递归、`du`、文件内容读取或活动任务归属探查；同一只读探针中只完成了 `/srv/gridflux-gsi` 的允许范围 stat/findmnt 和精确 run 路径的存在性判断。

本地对应关系：

- `D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results\case_plan.json` 与 `environment.json` 的 `run_id` 均为 `20260916T114921Z`；case plan 有 218 cases。该结果目录包含 JSON/CSV/JSONL、summary、command log、preflight 和 dataset manifest 等分析证据类型，可将 run ID 映射到历史回执记录的 `/tmp/gridflux-gridftp-compare/20260916T114921Z`。它是分析证据，不等于远端 payload 的备份。
- `D:\Project\GridFlux Beta\_server_backups\2026-09-16_shenzhen\reports\gridflux-backup-verification.md` 报告的是 Shenzhen `/root/projects/GridFlux-Beta` 源树备份（报告称 40,618 个本地文件、66,157,505,572 bytes）；没有证明上海实验 payload 被备份，也没有提供本任务候选逐文件 SHA-256。可读报告不等于候选备份 hash 已核验；候选 run 的备份关系仍为 unknown。

逐路径分类。旧回执中记录的字节数仅作为先前时点证据，不代表本次当前大小；凡备份/hash 未核验者不据此估算可释放量。

| 分类/绝对路径 | 归属、再生性与证据/备份 | 活动状态 | 当前预计可释放 | 风险与授权 |
|---|---|---|---:|---|
| 可考虑清理（仅待复核）：`/tmp/gridflux-gridftp-compare/20260916T114921Z` | run ID 已由本地 case plan 确认；旧回执先前测得 allocated/apparent `40445587456 / 40428005134` bytes（16,895 files），不作为当前测量。实验 payload 理论上可按 case plan 重生成；日志、summary、command、manifest 是证据，不能用重跑替代。上海备份及 SHA-256 未验证。 | 当前路径不存在；PID 768474 的目录归属未知 | `0`（当前不存在）；未来若出现则不估 | 目前没有可执行清理目标。若路径重现，需先回收并校验全部证据、确认无活动 PID，再由用户对该绝对路径单独授权。 |
| 必须保留：`/tmp/gridflux-gridftp-compare` | 根目录 uid/gid 0/0、mode 777，mtime `2026-09-23T02:02:37Z`；lstat 4096 bytes 只是目录项元数据。历史盘点显示混有多批次、data、GSI roundtrip、logs、preflight 和服务 PID 文件，不能把父目录视为单一可再生 payload；本地无对应备份 hash。 | PID 768474 是否关联未知；GridFTP 服务活动 | 不估 | 可能混有服务状态/实验结果；不得删父目录。任何子项需另行归属及用户明确授权。 |
| 必须保留：`/srv/gridflux-gsi` | uid/gid 997/997，mode 750；服务存储，内容/文件类型未读取，目录 lstat 不代表数据量；没有上海备份 hash。 | GSI/GridFTP PID 339900 正监听 2811 | 不估 | 活动服务相关目录；禁止把它或数据列为清理项，需用户对具体非服务 payload 另行授权。 |
| 必须保留：`/root/projects/GridFlux-Beta` | 历史 dirty 源树，HEAD `16b377359494f19f386ba5d375d353449e45f7a0`，porcelain 74 条；深圳源树备份报告不构成上海实验数据备份。 | 未查其子项进程关联；不对目录递归 | 不估 | 禁止覆盖、清理或就地开发；不得用作固定提交构建源。 |
| 未知保留：`/tmp/gridflux-gridftp-compare/20260914T022424Z`、`/tmp/gridflux-gridftp-compare/20260902T083220Z` | 旧回执曾记录大小；本次因活动归属未明未检查存在性/大小，本地结果索引未建立逐项任务与备份对应。 | 当前状态未知 | 不估 | 未确认属于当前 CPNetFlux 可再生 payload；保持原样，逐路径授权前不得处理。 |
| 未知保留：`/tmp/gridflux-compare-fresh-off-global-remote`、`/tmp/gridflux-compare-fresh-one-alias-remote`、`/tmp/gridflux-compare-fresh-reuse-worker-remote`、`/tmp/gridflux-compare-fresh-download-reuse-remote` | 当前只见一级名称/属主/时间元数据；缺少任务 ID、内容类型和上海备份 SHA 对应。 | 未关联到 PID；整体活动状态未知 | 不估 | 名称不足以确认归属或再生性；全部保留，清理需逐路径明确授权。 |
| 未知保留：`/tmp/gridflux-recovery-demo`、`/tmp/gridflux-retest-build` 及其他未映射 `/tmp` 条目 | 仅一级 lstat；目录内部、备份关系、证据类型和实际大小均未知。 | 未知 | 不估 | 不能因位于 `/tmp` 或名字含 GridFlux 就判定可删；全部保留，逐路径授权。 |

本次没有确认任何当前存在、备份完整且可释放的 payload；已授权可回收字节为 `0`。不得进入或统计 `/root/projects/CPSS(DCC)`，本轮只在 `/root/projects` 一级过滤其名称，未 stat/进入；`science-compressor` 同样跳过未 stat。未检查深圳，因为本任务上海空间、活动状态已构成阻塞；未触碰服务配置、凭据或服务文件。

资源门槛及 runner 逻辑：

- `/` 与 `/tmp` 是同一挂载，空间不可重复相加。按本次约 32 GiB 可用，扣除至少 10 GiB 保留后，余量上限约 22 GiB，尚未包含峰值 payload、build、archive、evidence。当前路线草案的完整矩阵为 38.25 GiB logical payload（不是已冻结的同时驻留峰值）；若按该量级同时驻留，上海明显不满足。精确 peak 与 D 的构建/归档预算尚未冻结，因此本次不能签发 D/E 空间通过；活动 PID 归属也未解决，D/E 继续 blocked。
- 只读检查 `tools/perf/run_gridftp_private_matrix.py`：`collect_environment()` 记录本地/远端 `/tmp` 的 fs type 和 free bytes，但没有发现执行前强制 `10 GiB + peak` 门槛；`cleanup_remote_paths()` 对生成的精确路径调用 `rm -rf`，本地 `run_root` 默认在退出时递归删除（失败或 `--keep-files` 时会保留）。`tools/perf/run_gridftp_tree_private_matrix.py` 的 `cleanup_remote()` 也直接调用 `rm -rf`。本轮只读源码，未运行 runner；未来任务必须先通过独立预算/证据回收门，再授权 runner 清理行为。

结论与下一步：当前上海盘已从先前 0 可用变为约 32G，但出现未归因的空间变化和新近 `gridflux-gridft` 监听；这不等于已恢复到可构建/实验状态。由 00 验收后，下一任务应先重新确认 PID 768474 归属与空间稳定性，再针对可确认的精确历史 payload 核验本地/受控备份可读性和 SHA-256，形成独立逐路径清理授权。没有授权前保持本节所列分类；不提交、不暂存、不改 BOARD/ROSTER。

### PERF-ENV-EVIDENCE-02 v1

- 路线/任务版本：`R2026-09-23.1 / v1`；输入 commit `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。开始和写后复核的本地 HEAD 均一致，未 superseded；暂存区为空。本节是唯一写入。
- 云端采样窗口：2026-09-23 04:25:01–04:28:29 UTC（12:25:01–12:28:29 中国标准时间）。深圳采样 04:27:13 UTC，上海 04:27:10/04:28:29 UTC。均为只读，无远端证据文件创建。
- SSH：Git OpenSSH `D:\Software\Git\usr\bin\ssh.exe -V` 客户端启动失败，`couldn't create signal pipe, Win32 error 5`，退出 `-1073741502`，未连接服务器。改用现有 `C:\Windows\System32\OpenSSH\ssh.exe` 和 alias；`BatchMode=yes`、`StrictHostKeyChecking=yes`、连接超时/keepalive 均启用。两端基础快照 SSH 退出 0；上海 PID/服务复核 SSH 退出 0。未输出凭据或 cmdline/environment。

| 主机/采样时间 | `/`、`/tmp` 同盘快照 | inode | 相关运行根只读状态 |
|---|---|---|---|
| 深圳 `iZwz9bgztwf1tic26q48pjZ`，04:27:13 UTC | `/dev/nvme0n1p3 ext4`，总 `105286258688`、已用 `23305764864`、可用 `77467840512` bytes（约 72.15 GiB），24%；`/` 与 `/tmp` 同一挂载 | 239840/6532624（4%） | `/tmp/cpnetflux-runs`、`/tmp/gridflux-gridftp-compare` 及已知 `20260916T114921Z` 子路径均不存在；每项 lstat 返回不存在，SSH 0 |
| 上海 `iZuf6ja2uqvjzh635cugzmZ`，04:27:10 UTC | `/dev/nvme0n1p3 ext4`，总 `105286258688`、已用 `64874110976`、可用 `35899494400` bytes（约 33.43 GiB），65%；`/` 与 `/tmp` 同一挂载 | 213844/6532624（4%） | `/tmp/cpnetflux-runs` 不存在；`/tmp/gridflux-gridftp-compare` 存在，lstat uid/gid 0/0、mode 777、目录项 4096 bytes；`20260916T114921Z` 子路径不存在。SSH 0 |

两端 `findmnt -T /` 与 `findmnt -T /tmp` 均返回 `/`、`/dev/nvme0n1p3`、ext4；上海 `/srv/gridflux-gsi` 也在该挂载上。不得将 `/` 和 `/tmp` 可用量相加。运行根数据只检查到明确路径的 lstat，没有递归、`du` 或文件内容读取。上海 04:25:01 与 04:27:10 的可用量分别为 `35899514880` 和 `35899494400` bytes，约 3 分钟变化 20 KiB；04:28:29 复采时 `/srv` 仍同盘。观察窗口内没有本任务写入迹象，快照近似稳定，但这不是空间锁或实验配额。

#### 上海进程归属

| 对象 | 已核实证据 | 判断 |
|---|---|---|
| 历史 PID 768474 / `0.0.0.0:25252` | 04:25:01 的 `ps -p 768474` 退出 1 且无行，`/proc/768474` 不存在；同次 `ss` 没有 25252。04:28:29 再次 `ps` 退出 1、`ss -H -ltnp '( sport = :25252 )'` 退出 0 但无监听。 | 当前已退出/不监听。PID 消失后无法从 `/proc` 复原其 uid、exe、cwd、cgroup 或服务单元；当前 systemd 单元清单也没有证明该历史 PID 的归属。不得把它归为 CPNetFlux runner 或认定其数据已清理。历史归属 blocked，相关旧空间变化仍 unknown。 |
| 当前 `*:2811` GridFTP 服务 | `ss` 显示 `globus-gridftp-` PID 339900；`ps` 为 UID/GID 0/0、root。`/proc/339900/exe` 为 `/usr/sbin/globus-gridftp-server`，cwd `/`，cgroup `/system.slice/gridflux-gridftp-gsi.service`。`systemctl show` 标识 `gridflux-gridftp-gsi.service`，描述 `GridFlux experimental GridFTP server with GSI authentication`，MainPID 339900，active/running；WorkingDirectory/User/Group 属性为空（默认 cwd 由 `/proc` 确认为 `/`，实际进程用户为 root）。只读取 unit 元数据，没有读 unit 文件。 | 服务归属已核实；是现存 GridFlux/GSI 服务，不是 PID 768474。端口 2811 必须保留。其是否直接使用 `/srv/gridflux-gsi` 的文件未由打开句柄核实，目录仍按服务相关资源保留。 |
| `/srv/gridflux-gsi` | lstat uid/gid 997/997、mode 750、目录项 12288 bytes，mtime `2026-09-16T12:57:01+08:00`；`findmnt` 指向根 ext4。 | 路径名、属主和活动 GSI unit 支持服务关联，但没有检查目录内容或进程 fd；不是清理候选，实际占用字节 unknown。 |
| 其他常驻监听 | SSH 22 / `sshd` PID 302699；FTP 21 / `vsftpd` PID 788；DNS loopback 53 / `systemd-resolve` PID 84661。GridFTP 数据范围 32000–32511 继续保留；未因单次无监听而视为空闲。 | 服务保留，不停止、不改配。 |

要解除历史 PID blocker，需要 00 提供 PID 768474 对应的 task/launcher/service 记录，或另派明确的只读元数据审计任务，限定到 2026-09-23 约 11:56–12:25 中国时间的 systemd/journal 记录，并仅提取 unit、PID、exe、cwd 等非敏感字段；本任务没有读取日志、cmdline、environment 或配置。

#### 资源预算

| 主机 | 当前可用 | 扣除 10 GiB 保留后的上限 | 若将路线草案 38.25 GiB logical payload 全部同时驻留，保留空间后的缺口 | 结论 |
|---|---:|---:|---:|---|
| 上海 | 约 33.43 GiB | 约 23.43 GiB | 至少约 14.82 GiB，尚未计 build/archive/evidence | 不满足该同时驻留假设；实际 peak 尚未由 runner/batch manifest 冻结。D 的 build/archive/evidence 预算也未给出，因此不能签发隔离构建或实验通过。 |
| 深圳 | 约 72.15 GiB | 约 62.15 GiB | 该假设下余约 23.90 GiB 给 build/archive/evidence | 仅是算术上限，不是运行许可；实际 peak、证据与构建预算、批次占用均需任务级冻结和重验。 |

门槛仍是每个实际使用的挂载点分别满足 `10 GiB 保留 + 峰值同时驻留 payload + build + archive + evidence`。38.25 GiB 是路线设计的 logical 汇总量，不等于单时刻 peak；因此实际所需量目前是 unknown。上海当前 raw free 约 33.43 GiB，不能写作 33.43 GiB 实验配额；`/tmp/cpnetflux-runs` 未创建，两端当前批准可释放量均为 `0 bytes`。历史 run 子路径当前不存在，其他上海比较父目录内容未测量、不得据其 4096-byte 目录项估算空间。

#### 本地备份索引

- 有限检查入口 `D:\Project\GridFlux Beta\_server_backups`、`2026-09-16_shenzhen`、其直接 `GridFlux-Beta` 目录及 `reports`，只查这些目录的直接文件名，不递归、不恢复 payload、不进入 `science-compressor`。`reports` 直接项有 inventory 文本、`gridflux-backup-verification.md`、计划文本和 `.b64` 文件；根、日期目录、GridFlux-Beta 两级目录及 reports 中未发现文件名匹配 manifest/hash/SHA-256/checksum 的直接索引。没有打开 inventory 大文本或 `.b64`。
- 小报告 `D:\Project\GridFlux Beta\_server_backups\2026-09-16_shenzhen\reports\gridflux-backup-verification.md`（835 bytes）可读，其本地文件 SHA-256 为 `A960DE5AB375B7CEFA14074E65B4EC309C4258076938529289657FA2EC4C0909`。该 hash 只校验这份说明文件；报告描述的是深圳历史源树文件数/字节数，不是上海实验 payload 的逐文件 manifest，也没有提供可验证的 payload checksum。
- 结果索引 `D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results\dataset_manifest.json`（73154 bytes）可解析为 JSON，顶层键为 `generated_at, materialize_catalog, profiles, schema_version, seed`，未发现 64 位十六进制 hash 值；该索引文件自身 SHA-256 为 `8B50CB6147B5F8D218C0238F77602523C0F512B05D51A5B9CB10D0140190F0F6`。同目录 `case_plan.json` 和 `environment.json` 的 run_id 均为 `20260916T114921Z`，case plan 218 cases。它们证明本地分析索引可读，不证明云端 payload 备份可读或内容一致。
- 结论：备份入口与小型报告可读；**未找到可校验上海 payload 的 manifest/hash 索引，payload SHA-256 未核实**。两个上列 SHA-256 是本地证据文件本身的 hash，不是云端备份校验结果。

#### 结论与交接

- 已核实：两端 `/`、`/tmp`、inode、挂载和相关 run-root 存在性；当前上海 2811 监听服务属 `gridflux-gridftp-gsi.service`；本地两个小型证据文件自身 SHA-256；本地结果 run_id 映射。
- Unknown/blocked：历史 PID 768474 的启动归属和 cwd；`/srv/gridflux-gsi` 实际树大小及其与 PID 339900 打开的文件关系；上海历史 payload 的备份/内容 SHA-256；当前矩阵 peak 与 D 的 build/archive/evidence 预算。
- 可释放量：未授权任何清理，`0 bytes`。虽然上海目前约 33.43 GiB raw free，实验门槛没有通过证明；不能构建、运行 CTest、runner 或实验。本任务未执行这些命令，也未停止服务。
- 实际 SSH 为上述 Windows OpenSSH 严格模式 fallback；远端主机/df/inode/path/systemd/ss 检查返回码见本节，已知失败的历史 PID `ps` 为 1（进程不存在），其余所列查询为 0。未运行项：构建、CTest、runner、传输/实验、文件删除/移动、服务配置读取、cmdline/environment/journal 读取。
- 完成本节后由 00/04 验收。继续推进前需提供/授权历史 PID 的非敏感归属证据，并冻结每个挂载点的同时驻留峰值与 build/archive/evidence 字节预算；有独立任务授权前不清理。
