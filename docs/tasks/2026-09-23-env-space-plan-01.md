# PERF-ENV-SPACE-PLAN-01：上海空间恢复的只读证据清单

- 状态：done，环境仍 blocked
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：05 云端运维与发布；验收角色：00
- 目标：在不删除、不改动服务和不启动构建/实验的前提下，形成解除上海空间阻塞所需的逐路径只读清单，优先找出已确认归属、可由用户决定的可再生内容。
- 非目标：不删除或移动任何文件；不进入、统计或修改 `/root/projects/CPSS(DCC)`；不触碰未知项目、`science-compressor`、服务配置、凭据或活动任务；不覆盖 dirty `/root/projects/GridFlux-Beta`；不安装软件、不启动/停止进程、不构建、不运行 CTest/runner/传输/实验。
- 输入：路线 `R2026-09-23.1`，环境回执 `docs/coordination/receipts/05-operations.md`；若 HEAD 或路线变化则停止并报告 superseded。
- 允许修改：只追加 `docs/coordination/receipts/05-operations.md` 的 `PERF-ENV-SPACE-PLAN-01 v1` 小节。

## 只读检查范围

1. 使用已验证的 Git OpenSSH 客户端、既有别名和严格 host-key 校验；记录命令、时间、退出码和客户端限制，不打印凭据。
2. 上海为主、深圳仅作必要对照：先列 `/tmp`、`/root/projects` 一级条目元数据；对确认属于 CPNetFlux 历史实验的目录再统计实际/表观大小、挂载点、属主、更新时间和活动 PID 关联。不得递归未知目录。
3. 对 `/root/projects/GridFlux-Beta`、`/srv/gridflux-gsi` 和备份入口只记录归属、证据文件类型和是否存在活动服务；不将整棵 dirty 树或服务目录列为可删项。
4. 与 `D:\Project\GridFlux Beta\_server_backups` 及本地结果索引做路径/任务标识对应；不能验证备份可读或 SHA-256 时标为 unknown，不估算可释放空间。
5. 输出逐路径候选表：绝对路径、归属、可再生性、证据/备份关联、活动状态、预计可释放字节、风险、是否需要用户明确授权。未知归属一律保留。

## 验收

- 明确上海 `/` 与 `/tmp` 的实时可用空间、外部写入/活动进程和本任务观察窗口。
- 形成“可考虑清理 / 必须保留 / 未知保留”三类清单；本任务不改变任何分类对应的文件。
- 记录恢复门槛：每个相关挂载点至少 `10 GiB + 峰值 payload + build/archive/evidence 预算`；空间未达到前继续阻塞 D/E。
- 只读回执通过 `git diff --check`；不提交、不暂存、不改 BOARD/ROSTER。

## 执行回执

- 05 已通过既有聊天 `01a0af8d-bfc9-7b33-b812-d4e5da9f0b3d` 实际执行，CLI 终态退出码 0。
- 已在 `docs/coordination/receipts/05-operations.md` 追加 `PERF-ENV-SPACE-PLAN-01 v1`；记录上海 `/` 与 `/tmp` 约 32 GiB 可用、同盘、活动 PID 768474 `gridflux-gridft` 监听 25252 且归属未明。
- 当前没有可确认的清理路径，授权可释放量仍为 0；旧 run 路径不存在且本地/受控备份 SHA 未核实。
- 未删除或移动文件、未改服务/进程、未安装软件、未构建、未运行 CTest/runner/传输/实验；`git diff --check` 通过，暂存区为空。
