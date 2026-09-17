# 本地开发与云端实验交接

事实快照：2026-09-17，通过 Windows SSH 读取。实时磁盘、进程、端口和软件依赖会变化，每次实验重新检查。

## 本地

- 开发仓库：`D:\Project\CPNetFlux`。旧 Codex 项目和历史备份：`D:\Project\GridFlux Beta`。
- 本地负责需求、源码、Git、审查、实验设计和结果分析；云端负责 Linux 构建/测试与受控实验。
- 本地 Windows 没有已验证可用的原生 CMake/g++/Ninja 工具链。此前 WSL Ubuntu-22.04 有 g++ 11.4/Python 3.10，但缺 CMake/Ninja，且没有 Windows SSH aliases。不要写“本地构建已通过”。
- Windows OpenSSH 配置保存在用户自己的 `%USERPROFILE%\.ssh\config`；只使用所需别名，不把私钥、口令、完整配置或 token 放进仓库、prompt 或日志。

## SSH 与资源快照

| 项目 | 深圳 | 上海 |
|---|---|---|
| Windows SSH 别名 | `gridflux-beta-shenzhen` | `gridflux-beta-shanghai` |
| SSH 目标 | `root@120.25.121.51:22` | `root@47.116.174.181:22` |
| 主机名 | `iZwz9bgztwf1tic26q48pjZ` | `iZuf6ja2uqvjzh635cugzmZ` |
| `/` 与 `/tmp` 所在盘 | 99G，已用 43G，可用 52G，46% | 99G，已用 99G，可用 0，100% |
| 历史目录 HEAD | `16b377359494f19f386ba5d375d353449e45f7a0` | 同左 |
| 历史目录工作树 | 78 条 dirty status | 74 条 dirty status |

两个别名当时均能非交互登录。上海磁盘满是 **新实验阻塞项**；SSH 可登录不代表可运行实验。

22:03 的运维接手复核见 [05-operations.md](receipts/05-operations.md)：深圳仍余约 51.07 GiB，上海仍为 0；两端已有 liburing 开发包，但没有新固定构建/运行验收。系统 OpenSSH 本轮返回 255，现有 Git OpenSSH 可用。可显式调用 `D:\Software\Git\usr\bin\ssh.exe -F "$env:USERPROFILE/.ssh/config"`，保留 BatchMode 与 StrictHostKeyChecking=yes；不要将客户端调用失败误判为服务器失联。

可以直接在 PowerShell 运行以下只读检查（不要把它们当成实验启动脚本）：

```powershell
ssh -o BatchMode=yes -o ConnectTimeout=15 gridflux-beta-shenzhen 'hostname; df -h / /tmp; git -C /root/projects/GridFlux-Beta rev-parse HEAD; git -C /root/projects/GridFlux-Beta status --porcelain'
ssh -o BatchMode=yes -o ConnectTimeout=15 gridflux-beta-shanghai 'hostname; df -h / /tmp; git -C /root/projects/GridFlux-Beta rev-parse HEAD; git -C /root/projects/GridFlux-Beta status --porcelain'
```

不要使用 `StrictHostKeyChecking=no`，不要打印凭据。Linux 构建须核验 compiler/CMake/Python、依赖和可选 liburing；不要自动安装未经任务单要求的大型工具链。

## 必须保留的边界

- 两台云端的 `/root/projects/GridFlux-Beta` 是历史源码与实验参考，不是新的开发分支，禁止覆盖、git reset/clean 或就地改实现。
- **`/root/projects/CPSS(DCC)` 是用户另一实验，不得进入、修改、清理或停止其进程。** 磁盘盘点时也跳过此目录。
- `science-compressor` 及其 GitHub 备份属于另一项目，不纳入 CPNetFlux 清理范围。
- 2026-09-17 观察到深圳旧 GridFTP 服务监听控制端口 22310、数据范围 23300–23811；上海 GSI 服务控制端口 2811、数据范围 32000–32511。它们不是空闲端口，不假设属于本次任务，也不停止或改配它们。上海相关历史服务路径为 `/srv/gridflux-gsi`。
- 不将“用户过去想腾空间”解释为可删除当前所有未知目录。先只读盘点，明确归属、备份和可再生性，再执行已授权且列明的清理。

## 正确的一次实验流程

1. 总指挥发任务单，运维核实两端磁盘、CPU/内存、依赖、端口、已有运行。每个相关挂载点至少保留 10 GiB，并另计本次峰值 payload；资源不足先阻塞。
2. 从本地已提交的明确 commit 生成源码归档；记录 commit、git status、archive SHA-256、构建参数/依赖和二进制 SHA-256。不能把云端 dirty 树当成这个提交。
3. 使用明确登记的独立运行目录（建议 `/tmp/cpnetflux-runs/<task-id>/src`、`build`、`results`；这是规划路径，创建前检查同名目录归属和空间）。每次只有一个获准占用同一实验环境的实验批次。
4. 运维交付构建清单，实验角色按任务单执行；记录两端命令、数据 seed、角色/方向、配置、重复次数、端口、PID 和完整性证据。SSH/scp 可用于部署和回收资料，不能代替待测传输路径。
5. 保存状态分列：transfer、integrity、evidence、wire accounting；`blocked`/`skipped` 与通过分开。凭据只在受控运行时注入，报告不含值。
6. 每个 case 验证并保留必要证据后清理它自己的可再生 payload；日志、hash、summary、manifest 诊断须先保存，不为清理破坏验收证据。
7. 结果回收到本地 `D:\Project\CPNetFlux-evidence\<task-id>`（规划路径，使用前创建并检查本地空间），做 SHA-256 校验；Git 只放报告、schema、小型样本和清单，大 payload/全量日志留在外部证据目录。
8. 记录磁盘前后、已退出/残留 PID、清理清单、证据索引；复验归档可读后才结束任务。禁止通配删除 `/tmp/*`、`/root/projects/*`。
