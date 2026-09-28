# MCE-TOPOLOGY-02-R2：修正运行器与传输端点角色

- 状态：done
- 路线版本、任务版本：`R2026-09-23.4`，MCE-TOPOLOGY-02 revision 2
- 发起人：00 总指挥；执行角色：02 实验与证据；验收角色：00 总指挥
- 目标及理由：依据现有 `runner.py` 命令构造校正 v1 计划中的角色混淆。实验运行器和 transfer client 固定在深圳；对端服务固定在上海；反向 case 是深圳客户端从上海源下载至深圳目的地。source endpoint 不能默认等于 transfer client host。
- 非目标：不 SSH、不构建、不创建远端目录、不生成 payload、不执行传输；不更改实现/旧证据/BOARD/ROSTER，不解除动态 QA 或运维门禁。
- 输入 commit（完整 SHA）、工作树状态：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；工作树有其他角色的未提交内容，只改下面白名单。
- 必读资料和证据路径：`tools/experiments/gridftp_compare/runner.py` 中 `build_tree_client_command`、`build_gridftp_case_command`、`command_plan_for_case`、`run_case`；v1 结果 `docs/tasks/2026-09-24-manual-cross-domain-experiment-topology-02-result.md`；当前计划脚本与说明。
- 允许修改的文件/目录；禁止修改的资源：仅修改 `tools/experiments/gridftp_compare/manual_cross_domain_experiment.ps1`、`tools/experiments/gridftp_compare/MANUAL_CROSS_DOMAIN_EXPERIMENT.md`，新增本任务 result/last-message。v1 result 保留为历史记录。禁止其他文件、源码、旧证据、云端资源和 Git index。
- 工作分支/worktree：文档/计划器小修在当前工作区；不切分支、不操作 index。
- 前置条件、环境占用和停止条件：执行保持 fail-closed；不得连接云端。若 runner 源码证据不支持本任务的本地/远端角色映射，报告后停止，不猜测。
- 验收标准及实际可执行命令：PowerShell Parser 零错误；plan mode 输出字段中显式区分 `manual_launcher=windows`、`experiment_controller=gridflux-beta-shenzhen`、`transfer_client_host=gridflux-beta-shenzhen`、`peer_service_host=gridflux-beta-shanghai`；两方向 source/destination 随方向变化，但 client/controller 与 peer service 不变；`windows_in_data_path=false`；12-case 本地计划断言通过；`-Execute` 仍创建输出目录前 fail-closed；不调用 SSH、不创建远端目录、不生成 payload；`git diff --check`、HEAD/index 门禁通过。
- 云端运行目录、端口、资源上限、清理与归档方案：本任务不适用；禁止连接。
- 预期产物路径：更新脚本和使用说明，以及 `docs/tasks/2026-09-24-manual-cross-domain-experiment-topology-revision-02-result.md`、`...-last-message.md`。

## 派给角色的消息

请检查现有 runner 命令构造证据。对两个方向，Windows 仅是用户手动启动入口；深圳是 runner/controller 和 CPNetFlux/`globus-url-copy` 客户端运行主机；上海是远端服务主机。正向 source=深圳,destination=上海；反向 source=上海,destination=深圳；两方向 transfer client 均在深圳，服务端/peer endpoint 均在上海。不要把 source endpoint 写成 client host。计划字段分别表示 launcher、controller、transfer client、peer service、source endpoint 和 destination endpoint。将 v1 报告中的角色映射作为已被本修订 supersede 的计划映射处理，但保留原结果文件。只改白名单，不连服务器，完成后回报 runner 源码引用与实际验证。

## 执行回执

- 实际输入/输出 commit：
- 实际改动：
- 实际命令、退出码与证据文件：
- transfer / integrity / evidence / wire accounting：均为 NOT_RUN。
- 失败/跳过/阻塞及原因：
- 剩余风险：实际远端部署和双向数据流尚未动态验证；固定构建及 04 QA 放行仍是前置门。
- 下一步：00 审核字段是否忠实表达 runner 执行模型；另行制定远端执行器任务。

## 验收与路线变化

验收人、结论和依据：00 于 2026-09-24 独立复核通过。PowerShell Parser 0 错误；本地双向 plan 生成 12 case 并逐 case 通过拓扑断言；`-Execute` 在创建 OutputRoot 前 fail-closed；`git diff --check` 退出码 0，HEAD 与 index 不变。

此修订只 supersede MCE-TOPOLOGY-02 v1 的运行角色字段映射，不改变项目性能路线和实验准入门禁。保留 v1 回执作为变更审计。
