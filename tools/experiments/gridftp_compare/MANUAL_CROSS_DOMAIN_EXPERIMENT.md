# 手动跨域实验包

入口：`manual_cross_domain_experiment.ps1`。

当前交付状态固定为 `READY_PENDING_DYNAMIC_QA`。05 revision-09 固定构建和 04 动态验收未在脚本中伪造为通过；脚本默认只生成本地 case plan，不调用 SSH、不创建远端目录、不生成 payload。

## 生成计划

在 Windows PowerShell 中运行：

```powershell
Set-Location D:\Project\CPNetFlux
Set-ExecutionPolicy -Scope Process Bypass
.\tools\experiments\gridftp_compare\manual_cross_domain_experiment.ps1 `
  -Mode plan -Direction both -Dataset all -LookaheadDepth 0 -Repeats 3 -Seed 20260924
```

`-Mode pilot|full` 只表达计划规模；`-Direction`、`-Dataset`、`-LookaheadDepth`、`-Repeats` 和 `-Seed` 都写入 `case-plan.json`。计划包含 CPNetFlux 与真实 GridFTP 两个 arm：CPNetFlux 固定 POSIX、worker control reuse、scheduler off、compression off、checksum none、fresh/no resume；GridFTP 记录 `globus-url-copy`、GSI 和 data-channel privacy。lookahead depth 0/1 必须分别生成计划。

当前脚本即使带 `-Execute` 也 fail-closed。只有 05 revision-09 固定构建、04 动态 QA、01 timer/auth/data-channel 契约和 05 环境准入全部形成可引用的放行记录后，才能另行修改执行 gate；本交付不授权运行。

## 运行器、客户端与传输端点

Windows 只是用户手动启动入口（`manual_launcher`）。实验运行器和 transfer client 固定在深圳 `gridflux-beta-shenzhen`；对端服务固定在上海 `gridflux-beta-shanghai`。这两个 Linux 主机之间承载测量数据，Windows 不在数据路径中。方向变化只交换数据 source/destination，不移动运行器、transfer client 或 peer service。

| 方向 | Source endpoint | Destination endpoint | Runner/controller | Transfer client | Peer service | 数据路径 |
|---|---|---|---|---|---|---|
| `shenzhen-to-shanghai`（默认） | 深圳 `gridflux-beta-shenzhen` | 上海 `gridflux-beta-shanghai` | 深圳 | 深圳 | 上海 | 深圳 Linux ↔ 上海 Linux |
| `shanghai-to-shenzhen` | 上海 `gridflux-beta-shanghai` | 深圳 `gridflux-beta-shenzhen` | 深圳 | 深圳 | 上海 | 上海源经上海服务发送至深圳客户端/目的地 |

反向 case 中，source endpoint 是上海，但 transfer client 仍运行在深圳；因此 source endpoint 不能推导为 client host。CPNetFlux 反向路径是深圳上的 download client 从上海 service 读取，并写入深圳本地目的目录。GridFTP 反向路径也是深圳 runner 上执行 `globus-url-copy`，其 source URL 指向上海 GridFTP 服务、destination 是深圳本地目录。

这个角色映射与现有 `runner.py` 命令执行结构一致：`build_tree_client_command` 选择本地 build 目录中的 upload/download client 并连接 `args.control_host`；`command_plan_for_case` 在 Shenzhen runner 上为 GridFTP 构造 `globus-url-copy` 命令；`run_case` 通过本地 `run_logged_command(..., cwd=REPO_ROOT)` 启动客户端，CPNetFlux server 则通过 remote helper 部署/停止。`run_gridftp_case` 在 remote endpoint 准备或检查 GridFTP 目录，再由本地 runner 调用 GridFTP client。以上是现有 runner 的命令/执行结构证据，不代表本次运行或动态验证。

每个计划 case 和 run manifest 分别记录 `manual_launcher=windows`、`experiment_controller=gridflux-beta-shenzhen`、`transfer_client_host=gridflux-beta-shenzhen`、`peer_service_host=gridflux-beta-shanghai`、方向相关的 `source_endpoint`/`destination_endpoint`，以及 `windows_in_data_path=false`。Windows 上生成 `case-plan.json` 只表示本地计划已生成；不表示深圳 runner、上海服务或实验已经就绪。

MCE-TOPOLOGY-02 v1 曾将方向 source 与 client、destination 与 server 混写；该旧结果保留作审计记录，但其角色映射已由 revision 2 supersede。当前包仍无远端执行器，不会调用 SSH。

## 计划字段和证据

每个 case 至少包含 case id、arm/system、dataset、精确 logical bytes、file count、方向、repeat、seed、lookahead depth、CPNetFlux/GridFTP 请求配置、transfer/integrity/evidence/wire 状态和 `performance_eligible` 初值。实际运行版必须为每 case 写 `state.json`、完整命令、PID/端点归属、stdout/stderr、实际端口、源/目标 SHA-256 或 canonical tree hash、wire accounting、退出码和计时；run-level 必须先持久化 manifest、summary、失败/blocked/skipped 清单、SHA-256 清单和磁盘/PID 清理报告，再清理可再生 payload。

`unknown`、`blocked`、`skipped`、证据不完整和中断状态不能转为 pass、零耗时或自动重跑。未知状态必须保留原证据，由人工决定是否建立新 run root；不得覆盖旧 evidence。并发阶段时间不能相加伪造成 wall time；准备、hash、日志收集和 cleanup 不进入 transfer timer。

## 运行前门禁

人工运行前必须确认：

1. 05 revision-09 固定 commit、源码归档 SHA-256、两端 binary SHA-256 和构建记录已冻结；脚本输入 hash 与计划一致。
2. 04 动态 QA 已接受真实 tree telemetry、传输/integrity/evidence/wire 分轴和性能 eligibility；01 的计时、认证和 data-channel 语义已冻结。
3. 两端只读 preflight 通过：挂载点空间、旧 GridFTP PID/端口、run root 归属、工具链、commit/hash、端点安全配置；任何失败立即停止，不启动 case。
4. 只使用隔离 run root；绝不触碰 `/root/projects/GridFlux-Beta` 或 `/root/projects/CPSS(DCC)`，不把凭据写入命令、日志或文档。

## 人工停止与交接

用户应保留脚本输出的 run root，不删除未知状态 case。人工停止时先记录停止时间和信号，再保留 state、命令、PID、日志、hash、manifest、summary、失败/blocked/skipped 清单及磁盘/PID 报告；不要重跑或覆盖 `unknown` case。把 run root 路径、`case-plan.json`、run-level summary、`sha256-manifest.txt`、动态 QA/固定构建引用和停止原因交给 00。大 payload 和原始日志不回传 Git。

Windows 入口显式使用系统 OpenSSH `C:\Windows\System32\OpenSSH\ssh.exe`，要求 BatchMode 和 StrictHostKeyChecking；当前 plan 模式不会启动它。脚本不打印凭据/token。

## 异步目录控制调度专项（DIR-ASYNC-CONTROL-02）

使用 `run_async_control_comparison.sh` 手动运行深圳工作区到上海 GridFTP 服务的对照。脚本先要求干净的固定提交与已配置 Linux build，复建后把同一提交的 `cpnetflux-gridftp-server` 复制到上海唯一的新实验目录，并核对本地/远端 SHA-256；不会覆盖历史 GridFlux 目录，也不会自动清理其保留的服务端二进制。

运行前由操作者设置：`CPNETFLUX_EXPERIMENT_REMOTE`、`CPNETFLUX_EXPERIMENT_CONTROL_HOST`、`CPNETFLUX_EXPERIMENT_BUILD_DIR`、`CPNETFLUX_EXPERIMENT_GRIDFTP_HOME`。GridFTP 默认使用 GSI；如当前服务允许匿名方式，可显式设置 `CPNETFLUX_EXPERIMENT_GRIDFTP_AUTH_MODE=anonymous`。SSH 使用 BatchMode 和 StrictHostKeyChecking，脚本不会接收或记录口令、私钥或 token。

矩阵固定 seed `20260831`、双向各 3 次、POSIX、worker control reuse、scheduler/compression/checksum 均关闭。目录配置为 dense 128×1 MiB 的 file parallelism 1/4/8，以及 mixed 128×512 KiB + 16×4 MiB + 4×32 MiB 的 (file parallelism, per-file connections)=(1,1)/(2,2)/(4,2)。depth=0 批次同时采集 CPNetFlux 与真实 GridFTP；depth=1 批次只采集 CPNetFlux。比较器要求每一配置三次传输都成功且源/目标 hash 一致，按 logical goodput 中位数计算 CPNetFlux/GridFTP 比值；只有所有配置 depth=1 比值均不低于 0.90 才报告达标。此专项评价目录吞吐，不把未观测的 GridFTP 内部阶段补造出来。

脚本要求深圳 `/tmp` 至少 13 GiB 可用、上海 `/tmp` 至少 11 GiB 可用，并让 runner 每个 case 维持 10 GiB 门槛。上海服务端数据端口默认从 34000 起，避开已有 GridFTP 32000–32511 范围；如果防火墙未开放该范围，应先调整任务参数/规则，不能启动矩阵。输出和逐配置比较报告保存在 `CPNETFLUX_EXPERIMENT_OUTPUT_ROOT/<run-id>`，缺证据、缺 case 或 hash 不符都会阻止 90% 通过结论。
