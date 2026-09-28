# LOWLEVEL-CLOUD-PREFLIGHT-01 执行结果

- 路线/任务：`R2026-09-23.4 / v1`。
- 结论：**BLOCKED；只读盘点完成，不准启动云端 build 或 transfer。**
- 本地输入/输出文档 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- 工作区起始状态：非干净，65 条 porcelain 项（7 条已修改跟踪文件、58 条未跟踪文件），暂存区 0 项。既有改动不在本任务范围；本任务只新增指定结果和摘要文件。
- 远端范围：仅 `gridflux-beta-shenzhen` 与 `gridflux-beta-shanghai`。未访问 `/root/projects/CPSS(DCC)`、science-compressor 或未知项目内容；未读取 cmdline、environ、凭据、token、私钥。
- 未构建、未运行 CTest、未传输/启动 workload、未写云端、未创建目录、未安装、未清理、未停止/改配进程或服务。transfer、integrity、wire accounting、实验 hash 均未运行，不适用。

## 派单与 SSH

按 `docs/coordination/DISPATCH.md` 和 `ROSTER.md` 检查既有 05 会话 `01a0af8d-bfc9-7b33-b812-d4e5da9f0b3d`；本轮工具清单无专用聊天派单入口，PowerShell 与 Codex CLI 可用。实际续接命令：

```powershell
Get-Content -Raw -Encoding utf8 'docs/tasks/2026-09-23-lowlevel-cloud-preflight-01.md' |
  codex exec resume 01a0af8d-bfc9-7b33-b812-d4e5da9f0b3d `
    --output-last-message 'docs/tasks/2026-09-23-lowlevel-cloud-preflight-01-last-message.md' -
```

退出码 1：`state_5.sqlite` 以只读方式打开失败，app-server 初始化报 `E_ACCESSDENIED`。此调用没有启动角色轮次，CLI last-message 未生成。为完成任务授权的只读 SSH 检查，后续由当前执行直接运行同范围探针；没有并发重派或创建新会话。摘要文件由本执行人工形成，并明确标成**非 CLI 自动输出**。

SSH 客户端 `C:\Windows\System32\OpenSSH\ssh.exe`；`ssh.exe -V` 退出 0，版本 `OpenSSH_for_Windows_9.5p2, LibreSSL 3.8.2`。两端使用既有用户 config（不输出配置内容）、`BatchMode=yes`、`StrictHostKeyChecking=yes`、`ConnectTimeout=12`、`ServerAliveInterval=5`、`ServerAliveCountMax=2`，只读脚本 SSH 总退出码均为 0。Git for Windows SSH 本轮未重试；上一任务已记录其本地启动 Win32 error 5，故沿用已验证的系统 OpenSSH 回退。

## 资源快照

远端时间由 `date --iso-8601=seconds` 读取。两端 `/`、`/tmp` 同为 `/dev/nvme0n1p3 ext4`；上海 `/srv/gridflux-gsi` 也在同一挂载点，不能把路径可用量相加。1 GiB = 1,073,741,824 B。

| 主机 | 本地采样（+08:00） | 远端主机名/时间 | `/` 与 `/tmp` 总量/已用/可用 | 使用率 | inode 已用/总量 | CPU / 内存 / 负载 |
|---|---|---|---|---:|---|---|
| 深圳 | 2026-09-23 21:06:15 | `iZwz9bgztwf1tic26q48pjZ` / 21:06:17+08:00 | 105,286,258,688 / 26,989,973,504 / 73,783,631,872 B（68.716 GiB 可用） | 27% | 241,009 / 6,532,624（4%） | 2 vCPU；RAM 3,664,908,288 B，available 2,955,702,272 B；无 swap；load `0.00 0.00 0.00` |
| 上海 | 2026-09-23 21:06:16 | `iZuf6ja2uqvjzh635cugzmZ` / 21:06:18+08:00 | 105,286,258,688 / 64,891,568,128 / 35,882,037,248 B（33.418 GiB 可用） | 65% | 214,753 / 6,532,624（4%） | 2 vCPU；RAM 3,664,908,288 B，available 3,114,229,760 B；无 swap；load `0.00 0.00 0.00` |

`/tmp/cpnetflux-runs` 两端均不存在，未创建。21:22 对 `/tmp` 的 `stat` 与 `findmnt` 均退出 0：目录 uid/gid `0/0`、mode `1777`，仍挂在根 ext4；规划的 `/tmp/cpnetflux-runs/LOWLEVEL-CLOUD-VERIFY-01` 也不存在。该父目录模式不证明未来同名路径的归属或权限状态，创建前须重查冲突和预算。上海 `/srv/gridflux-gsi` 是 uid/gid `997/997`、mode `750`；stat 的目录项 bytes `12288` 不是递归树大小，未遍历服务数据。候选隔离根为每端 `/tmp/cpnetflux-runs/LOWLEVEL-CLOUD-VERIFY-01/{src,build,results}`。

## 进程、服务与端口

主资源查询使用 `ss -lntup`、限定字段 `ps -eo pid,ppid,user,comm,lstart,etimes`、`/proc/<pid>/comm`/`cgroup` 与 `systemctl show`；各主机 SSH 脚本总退出码 0。

| 主机 | 服务与归属证据 | 判断 |
|---|---|---|
| 深圳 | `vsftpd` root PID 794 TCP/21；`sshd` root PID 3747193 TCP/22；GridFTP root PID 2493350 TCP/22310。其 exe `/usr/sbin/globus-gridftp-server`、cwd `/`、PPID 1、cgroup `/user.slice/user-0.slice/session-5237.scope`。`loginctl show-session 5237 -p Name -p User -p State -p Remote -p Service --no-pager` 为 root、sshd、remote=yes、state=closing（退出 0）。没有活动匹配 systemd unit。 | 二进制和 root 账号可确认；session 已 closing，启动者/责任人及认证、数据保护未知。不得停止或复用服务资源。 |
| 上海 | `vsftpd` root PID 788 TCP/21；`sshd` root PID 302699 TCP/22；GridFTP root PID 339900 TCP/2811。exe `/usr/sbin/globus-gridftp-server`、cwd `/`、cgroup `/system.slice/gridflux-gridftp-gsi.service`；unit MainPID 339900、active/running，Description `GridFlux experimental GridFTP server with GSI authentication`。 | GSI 标称服务和 unit 可确认，必须保留；描述不能证明 data-channel DCAU/PROT/TLS 模式。 |

先前未知深圳 PID 3886978 的专项查询：`ps -p 3886978 -o pid,ppid,user,comm,lstart` 两端均退出 1，`/proc/3886978` 不存在；该 PID 已退出，但不能推断其此前归属。21:06 限定进程快照未见 python、cmake、ctest、ninja 或 cpnetflux 进程；名称筛选退出 0（仅表头）。这不证明不存在名称不匹配的外部批次。SSH 自身产生的 sshd 子 PID 不视为实验。

执行偏差：首轮受限名称筛选额外记录了 UID 与 elapsed 字段（`ps -eo pid,uid,user,comm,etimes`），超过任务列出的最小 `ps` 字段；查询仍未读取 cmdline、environ 或完整 argv。发现后续未知 PID 时使用了任务指定字段。该偏差不提供其归属证据，也不改变 BLOCKED 结论。

两端历史 `/root/projects/GridFlux-Beta` 只读查询：HEAD 均为 `16b377359494f19f386ba5d375d353449e45f7a0`；普通 porcelain 条数深圳 78、上海 74。未编译、遍历、清理或修改该 dirty 历史树。

既有端口继续避让：深圳 GridFTP 控制 `22310`、历史数据 `23300–23811`；上海控制 `2811`、历史数据 `32000–32511`。本轮仍监听两控制端口，数据范围当时无 listener，但仍视为保留。`vsftpd:21` 与 `sshd:22` 保留。

仅供未来任务讨论、不是分配：深圳候选控制 `26000`、数据 `26010–26073`；上海候选控制 `27000`、数据 `27010–27073`。21:10 的筛选查询显示候选范围无 listener，`/proc/sys/net/ipv4/ip_local_port_range` 两端均为 `32768–60999`；查询退出 0。云安全组、防火墙和服务端口预留未核验；运行前仍要逐端复查并取得任务授权。本轮没有绑定端口。

## 依赖与认证/数据 TLS

两端均有 GCC/G++ 11.4.0、CMake/CTest 3.22.1、Ninja 1.10.1、pkg-config 0.29.2、Python 3.10.12、Git 2.34.1。`dpkg-query` 显示 OpenSSL dev 3.0.2、zlib dev 1.2.11、GoogleTest 1.11.0、spdlog 1.9.2、liburing-dev 2.1-2build1；pkg-config 显示 liburing 2.0，`ldconfig` 有 `liburing.so`。这些仅证明工具/包存在；固定提交配置、编译、链接、CTest 和 runtime 均未验收，io_uring backend 未运行。

本地 [SECURITY.md](../SECURITY.md) 描述 CPNetFlux alpha 能力：控制认证默认 anonymous，可选 token；控制 TLS 默认 off；STOR/RETR framed data TLS 可设 required，但服务端须同时 `--tls-mode required` 与 `--data-tls-mode required`，客户端也需 data TLS 和 CA。该 data TLS 不是 GSI 替代品，LIST/NLST 被动元数据仍明文。云端没有 CPNetFlux 服务进程，实际认证/TLS 参数未知。

上海 GridFTP unit 只标称 GSI authentication，数据通道是否受保护未知；深圳 GridFTP 进程来源、认证及 data protection 均未知。没有读取服务配置/凭据或握手。必须由服务责任人提供可公开的安全模式状态后，才能确定可比口径。SENDV 实现报告表明优化仅走 plain TCP，TLS 路径保持原有写法；若安全策略要求 CPNetFlux data TLS，SENDV 优化不会触发。不能为测出收益而降低 TLS/GSI 保护。

## 候选矩阵与逐盘字节预算

下表从已有 profile 提案折算，仅是**规划上限**，不是冻结或授权矩阵：

| 候选阶段 | 组合假设 | case 数 | 单 case 最大 payload | 顺序执行峰值/端 |
|---|---|---:|---:|---:|
| 单文件 SENDV 对照 | 256 MiB；baseline/candidate 两个二进制；connections 1/8；双向；每格 3 次 | 24 | 268,435,456 B | 每端一份源或目标，268,435,456 B |
| 目录阶段观察 | 32×1 MiB、128×1 MiB；双向；file-parallelism 1/8；每格 3 次 | 24 | 134,217,728 B | 每端一份源或目标，134,217,728 B |
| 合计 | 两阶段串行、同一时刻最多一个 case | 48 | 取单文件最大 case | 每端 268,435,456 B（256 MiB） |

单文件累计逻辑传输 `256 MiB × 24 = 6 GiB`；目录为 `(32+128) MiB × 2 directions × 2 parallelism × 3 repeats = 1.875 GiB`；总计 7.875 GiB 网络逻辑字节。它是跨 case 累计量，不是同时驻留量。逐 case 保存证据并核验后清理时，已知 payload peak 为每端 256 MiB；若不能确认 case 间 payload 已清理，保守按每端累计 7.875 GiB 估算。

| 主机挂载点 | 当前可用 | 扣 10 GiB reserve 后 | 再扣单 case peak 256 MiB 后，余给 build/archive/evidence/系统增长 | 若累计 payload 全保留，余给上述项目 |
|---|---:|---:|---:|---:|
| 深圳 `/`=`/tmp` | 73,783,631,872 B | 63,046,213,632 B（58.716 GiB） | 62,777,778,176 B（58.466 GiB） | 54,590,496,768 B（50.841 GiB） |
| 上海 `/`=`/tmp`，含 `/srv/gridflux-gsi` | 35,882,037,248 B | 25,144,619,008 B（23.418 GiB） | 24,876,183,552 B（23.168 GiB） | 16,688,902,144 B（15.543 GiB） |

每端 build tree、固定提交源码归档、baseline/candidate 二进制、日志/诊断和证据暂存的峰值字节未定义；矩阵也未冻结（SENDV 仍由 04 review，目录阶段 profile 当前 blocked）。表中仅为已知 payload 后的算术余量，不是可分配额度。不能证明挂载点满足 `10 GiB + 峰值 payload + build/archive/evidence/系统增长预算`，准入仍 BLOCKED。

## 命令、退出码与验收状态

资源采样主脚本的 SSH 总退出码每端均为 0，执行了以下只读命令：

```sh
hostname
date --iso-8601=seconds
df -B1 -T / /tmp
df -i / /tmp
findmnt -no SOURCE,TARGET,FSTYPE --target /
findmnt -no SOURCE,TARGET,FSTYPE --target /tmp
nproc
free -b
cat /proc/loadavg
for x in g++ cmake ctest ninja pkg-config python3 git; do command -v "$x"; "$x" --version | head -n 1; done
dpkg-query -W -f='${binary:Package} ${Version}\n' build-essential cmake ninja-build pkg-config libssl-dev zlib1g-dev liburing-dev libgtest-dev libspdlog-dev
for x in openssl zlib liburing gtest spdlog; do pkg-config --modversion "$x"; done
ldconfig -p | grep -E 'liburing\.so'
ss -lntup
ps -eo pid,uid,user,comm --sort=pid | awk 'NR==1 || tolower($4) ~ /(gridftp|vsftpd|sshd|cpnetflux)/'
ps -eo pid,ppid,user,comm,lstart,etimes --sort=pid | awk 'tolower($4) ~ /(python|cpnetflux|gridftp|globus|vsftpd|sshd|cmake|ctest|ninja|iperf|fio)/'
systemctl list-units --all --no-legend '*gridftp*' | cut -c1-180
systemctl show gridflux-gridftp-gsi.service -p MainPID -p ActiveState -p SubState -p User -p Description --no-pager
cat /proc/sys/net/ipv4/ip_local_port_range
stat -c '%n|type=%F|uid=%u|gid=%g|mode=%a|mtime=%y' /tmp
findmnt -no SOURCE,TARGET,FSTYPE --target /tmp
git --no-optional-locks -c core.fsmonitor=false -C /root/projects/GridFlux-Beta rev-parse HEAD
git --no-optional-locks -c core.fsmonitor=false -C /root/projects/GridFlux-Beta status --porcelain=v1 | awk 'END {print NR}'
```

专项命令：`ps -p 3886978 -o pid,ppid,user,comm,lstart` 两端退出 1（进程不存在）；`loginctl show-session 5237 -p Name -p User -p State -p Remote -p Service --no-pager` 深圳退出 0、上海退出 1（会话不存在）；`stat -c ... /tmp`、`findmnt ... /tmp`、`cat /proc/sys/net/ipv4/ip_local_port_range` 两端退出 0；候选/历史端口筛选 `ss -H -lntup` 两端退出 0。服务 unit/PID 查询所在 SSH 脚本总退出 0。主资源脚本没有为每条简单子命令单独封存 shell `$?`，这里只报告 SSH 总码，不能说每个子命令都有独立退出码。

- **READY/BLOCKED：BLOCKED。** 深圳 GridFTP session 责任人和两端有效认证/data TLS 条件未闭合；build/archive/evidence 预算未知；矩阵未冻结；进程筛选不能排除名称不匹配的外部批次。
- **下一步：** 深圳服务责任人确认 PID 2493350/session 5237 的非敏感归属；两端责任人给出 GridFTP GSI 与数据保护模式，并确认 CPNetFlux 获准的数据 TLS/认证策略；04 完成 SENDV review、目录阶段取得可审计数据后，冻结矩阵和逐端构建/归档/证据预算；然后另派 preflight/run 任务。当前不能启动 build 或 transfer。
