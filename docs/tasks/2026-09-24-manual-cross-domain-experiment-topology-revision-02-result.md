# MCE-TOPOLOGY-02 revision 2 执行结果

- 状态：计划拓扑字段已修正；实验未放行。
- 路线/任务版本：`R2026-09-23.4 / revision 2`。
- 输入/输出 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- v1 审计：保留 `docs/tasks/2026-09-24-manual-cross-domain-experiment-topology-02-result.md` 不变；v1 将方向 source/client、destination/server 混为一组的映射由本 revision supersede。

## Runner 源码依据

定向检查当前 `tools/experiments/gridftp_compare/runner.py`：

- `build_tree_client_command`（约 1396–1437 行）从 `args.local_build_dir` 选出 upload 或 download client，命令连接 `args.control_host`；方向决定客户端类型，不改变客户端二进制的本地运行位置。
- `build_gridftp_case_command`（约 1938–2002 行）正向使用本地 source URL、远端 GridFTP destination URL；反向使用远端 GridFTP source URL、本地 destination URL。
- `command_plan_for_case`（约 2536–2592 行）从 runner 本地 `case_dir` 传入本地 payload/目标路径，并为 tree case 构造本地客户端命令。
- `run_case`（约 2595–2681 行）将 CPNetFlux client 命令交给本地 case 流程，并通过 remote root/server helper 与对端协作；GridFTP case 走 `run_gridftp_case`。
- `run_gridftp_case`（约 2086–2205 行）在远端准备 source/destination 后，由本地 `run_logged_command(..., cwd=REPO_ROOT)` 启动 `globus-url-copy`。反向方向设置 GSI client source passive 环境变量，也佐证 client 在 Shenzhen 侧而 source service 在远端上海。

据此，Windows 只作用户人工启动入口；当前运行器/client 固定深圳，对端 service 固定上海。方向字段描述逻辑数据 source/destination，不能用 source endpoint 推断 transfer-client host。未连接远端进行动态验证。

## 修改

- 仅修改 `tools/experiments/gridftp_compare/manual_cross_domain_experiment.ps1` 与 `tools/experiments/gridftp_compare/MANUAL_CROSS_DOMAIN_EXPERIMENT.md`，新增本 revision result 与 last-message。
- 每个 case 和 run manifest 现在区分 `manual_launcher`、`experiment_controller`、`transfer_client_host`、`peer_service_host`、`source_endpoint`、`destination_endpoint` 和 `windows_in_data_path`。
- Shenzhen→Shanghai：source 深圳、destination 上海；runner/client 深圳、peer service 上海。
- Shanghai→Shenzhen：source 上海、destination 深圳；runner/client 仍深圳、peer service 仍上海。反向表示深圳 download client 从上海 service 拉取，写入深圳本地目标。
- `-Execute` 继续 fail-closed；没有 SSH、payload、远端目录、构建或传输。

## 实际验证

- PowerShell Parser：`[System.Management.Automation.Language.Parser]::ParseFile(...)`，实际 `PARSER_ERRORS=0`。
- 本地 plan 演练：执行 `manual_cross_domain_experiment.ps1 -Mode plan -Direction both -Dataset all -LookaheadDepth 0 -Repeats 1 -Seed 20260924 -OutputRoot <unique $env:TEMP root>`，退出码 0；生成 12 case（3 dataset × 2 direction × 2 system）。00 独立逐 case 断言 launcher/controller/client/service 固定、source/destination 随方向映射，全部通过。计划根：`C:\Users\12563\AppData\Local\Temp\mce-topology-r2-388e99df97b548eea2baf2d674e6c539`。
- `-Execute` 演练：使用 `-Mode pilot ... -Execute`，捕获到预期的 fixed-build/dynamic-QA fail-closed 错误；断言通过，拒绝前未创建 OutputRoot。
- `git diff --check`：退出码 0。脚本、说明、task、result、last-message 均严格 UTF-8 可解码且无尾随空白。HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，暂存区为空，白名单 status 只有获准的两个工具文件和本任务 task/result/last-message；本任务单由 00 后置标记完成。
- 静态检查确认脚本没有 SSH 调用表达式；计划演练明确 SSH 未调用、payload 未生成。本任务 transfer、integrity、evidence、wire accounting 均为 `NOT RUN`。
- 02 的 CLI 续接进程在写入上述产物后，因远端模型响应流断连而以退出码 1 结束；00 重新独立运行以上验收并确认通过，不将该 CLI 退出码当作动态测试结果。

## 状态与下一步

该变更只 supersede v1 的角色字段映射，不改变性能路线和实验准入。仍须满足 05 revision-09 固定构建、04 动态 QA 及 00 单独放行。由 00 审核计划字段；真实远端部署/双向数据流验证需另立执行任务。
