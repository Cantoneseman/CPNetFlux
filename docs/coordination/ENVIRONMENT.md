# 本地开发与云端实验交接

事实快照：历史条目分别注明采样时间。实时磁盘、进程、端口和软件依赖会变化，每次云端任务重新检查。

## 云端优先与 GitHub 备份（2026-09-24）

项目采用云端优先。迁移验收后，深圳上的独立 CPNetFlux Git 仓库是唯一权威开发工作区；源码、项目文档、任务管理、构建、测试、分析和实验都在该仓库或隔离 worktree 完成。上海只作为跨域传输对端，不作为第二开发仓库。Windows 用于连接/管理会话、查看结果和按任务单应急回收证据，不作为日常源码或 Git 工作区。完整原则和迁移门禁见 [云端优先决策](../DECISIONS/2026-09-24-cloud-first-development-and-github-backup.md)。

**当前迁移尚未完成。** `D:\Project\CPNetFlux` 是过渡期现有仓库；资料没有证明深圳已存在权威 CPNetFlux clone，也没有核实 GitHub remote 身份、写权限或 push 成功。候选路径 `/root/projects/CPNetFlux` 使用前必须核实存在性、属主、Git 状态、进程占用、挂载点和空间，不能覆盖任何既有路径。当前下一项是 BOARD 中的 `CLOUD-GITHUB-01`；在其验收前，不启动本地源码实现、构建或性能实验，也不宣称云端工作区/GitHub 备份已经建立。迁移期间允许的操作限于该任务所需的盘点、hash/清单、计划和获准的规则文档。

每个可独立审查的任务阶段使用云端 `codex/<task-id>` 分支，精确提交授权文件并推送到经核实的 CPNetFlux GitHub remote。阶段完成或长任务的恢复检查点应记录 commit SHA、push 退出状态和远端 SHA 回读；推送失败即为“GitHub 备份未完成”。GitHub 保存源码、文档、报告、schema、证据索引和必要小样本；大 payload、全量日志和大型证据包留在有归属的外部证据存储，提交路径/manifest/SHA-256。凭据只走受控凭据机制，绝不写入仓库或日志。

本决定不改变性能/正确性门禁，也不授权覆盖历史树、清理未知文件或停止未知进程。两端 `/root/projects/GridFlux-Beta` 均为 dirty 历史资料，不得覆盖、清理或就地开发；绝不进入 `/root/projects/CPSS(DCC)`。若工具实际只能写本地工作区，角色应停止项目写操作并报告环境绑定不符合要求，不得静默回退。

## Windows 过渡与连接端

- 过渡期仓库：`D:\Project\CPNetFlux`。在迁移门禁完成前，应保全其中全部 tracked、untracked 和 dirty 内容；迁移后用于连接、查看和应急恢复，不作为权威开发分支。旧 Codex 项目和历史备份：`D:\Project\GridFlux Beta`。
- Windows 用于发起/观察云端工作、SSH 连接管理和按任务回收/校验外部证据；日常源码、Git、项目文档、构建、测试、分析和实验迁往深圳权威仓库。大证据不因回收而默认放进 Git 工作树。
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

## 云端任务与实验流程

1. 总指挥发任务单，运维核实两端磁盘、CPU/内存、依赖、端口、已有运行。每个相关挂载点至少保留 10 GiB，并另计本次峰值 payload；资源不足先阻塞。
2. 从深圳权威 clone 的明确 commit/任务 worktree 构建并运行，记录 commit、Git status、源码 SHA-256 manifest、构建参数/依赖和二进制 SHA-256。迁移完成前如任务单授权临时快照，必须明确记录来源和完整 dirty 内容；不得从云端历史 dirty 树构建并冒充该输入。
3. 使用经任务单登记和现场检查的隔离路径：权威仓库候选 `/root/projects/CPNetFlux`；运行证据可用 `/tmp/cpnetflux-runs/<task-id>/{src,build,results}`。每次仅一个获准实验批次。
4. 运维交付构建清单，实验角色按任务单执行；记录两端命令、数据 seed、角色/方向、配置、重复次数、端口、PID 和完整性证据。SSH/scp 可用于部署和回收资料，不能代替待测传输路径。
5. 保存状态分列：transfer、integrity、evidence、wire accounting；`blocked`/`skipped` 与通过分开。凭据只在受控运行时注入，报告不含值。
6. 每个 case 验证并保留必要证据后清理它自己的可再生 payload；日志、hash、summary、manifest 诊断须先保存，不为清理破坏验收证据。
7. 报告、schema、小样本、证据清单和 SHA-256 索引提交并推送到 GitHub；大 payload/全量日志留在有归属的外部证据目录。若任务明确要求本地应急回收，须校验 SHA-256 并记录云端原路径及保留状态。
8. 记录磁盘前后、已退出/残留 PID、清理清单、证据索引、commit SHA 与 push 状态；核实归档可读和备份状态后结束。禁止通配删除 `/tmp/*`、`/root/projects/*`。
