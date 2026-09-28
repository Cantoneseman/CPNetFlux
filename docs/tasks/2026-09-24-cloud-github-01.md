# CLOUD-GITHUB-01：建立深圳权威工作区与 GitHub 分阶段备份链路

- 路线版本：`R2026-09-24.16`
- 状态：ready；本文件是迁移门禁，不表示已迁移
- 负责人：00 总指挥；执行：05 云端运维；独立复核：04 质量；必要时 01/03/02 只交付各自清单
- 固定项目路径：Windows 过渡仓库 `D:\Project\CPNetFlux`；深圳候选权威仓库 `/root/projects/CPNetFlux`

## 目标

在不覆盖历史树、不触碰受保护项目的前提下，建立深圳唯一权威 CPNetFlux clone/worktree，并验证一个安全的 GitHub remote。完成本地过渡仓库与深圳迁移范围的逐文件清单/hash 对账，创建任务分支，提交一份非敏感迁移清单并推送到 GitHub，回读远端 SHA，证明后续可按阶段备份。

## 非目标

- 不启动源码实现、构建、CTest、性能实验或跨域传输。
- 不覆盖、reset、clean 或就地修改 `/root/projects/GridFlux-Beta`。
- 不进入、修改、清理或停止 `/root/projects/CPSS(DCC)`；不碰 science-compressor。
- 不把私钥、口令、token、环境变量、build、payload 或大型原始日志复制到 GitHub。
- 不删除本地、云端或旧证据；未分类/活动占用/空间不足即停止。

## 前置与允许范围

1. 05 只读核实深圳/上海实时磁盘、挂载点、PID、端口和候选路径归属；GitHub remote 只读核实仓库身份、默认分支和可用的受控认证方式，不打印凭据。
2. 00 分类 Windows 仓库的 tracked、untracked、dirty 文件；为每项记录路径、大小、SHA-256、归属、是否可提交。大证据只登记外部路径和 hash。
3. 仅在候选路径为空闲、空间预算满足且无活动冲突时创建 `/root/projects/CPNetFlux`；创建前保存目录元数据和 ownership marker。不得把旧 GridFlux 树复制改名充当新仓库。
4. 迁移完成后使用 `codex/CLOUD-GITHUB-01` 分支精确提交迁移清单、规则文档和必要小型索引；推送到核实的 CPNetFlux GitHub remote，回读远端 commit SHA。

## 验收

- 深圳仓库路径、属主、Git 根、HEAD、工作树和进程归属可追溯；上海仍只作传输对端。
- 本地过渡资料与云端允许迁移资料逐文件对账，无未解释差异；历史 GridFlux/CPSS 边界保持不变。
- 任务 commit 的文件清单明确、无秘密/大文件，push 退出成功，远端分支指向同一完整 SHA。
- 任务回执记录实际命令、退出码、源/目标 SHA、空间、凭据处理方式、未迁移清单和下一步。
- 任一门禁未满足时，状态为 `blocked`，明确写“云端迁移/GitHub 备份未完成”，不启动其他开发任务。

## 输出

`docs/tasks/2026-09-24-cloud-github-01-result.md`、迁移清单/sha manifest、非敏感 GitHub push 证据和 05/04 回执。大证据留在经核验的外部目录。
