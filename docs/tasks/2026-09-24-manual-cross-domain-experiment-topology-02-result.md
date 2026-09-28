# MCE-TOPOLOGY-02 执行结果

- 状态：仅澄清计划拓扑；实验仍未放行。
- 路线/任务版本：`R2026-09-23.4 / v1`。
- 输入/输出 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，执行前后相同。
- 输入工作区：存在共享文档和计划文件未提交改动；未覆盖这些文件，未操作 Git index。

## 改动

- 更新 `tools/experiments/gridftp_compare/manual_cross_domain_experiment.ps1`：run manifest 增加全局拓扑；每个 case 增加 `topology.controller=windows`、`measured_data_path=shenzhen<->shanghai`、`windows_in_data_path=false`、source/client endpoint、destination/server endpoint，以及 Linux endpoint 直接传输标记。
- 默认方向 `shenzhen-to-shanghai` 映射为 source/client `gridflux-beta-shenzhen`、destination/server `gridflux-beta-shanghai`；`shanghai-to-shenzhen` 将两端角色对调。
- 更新 `tools/experiments/gridftp_compare/MANUAL_CROSS_DOMAIN_EXPERIMENT.md`，说明 Windows 仅为人工计划/控制入口，不承载数据；Windows 本地 plan 成功不能表示深圳或上海已经准备/运行。
- 新增本结果和独立 last-message。没有修改任务单、源码、旧证据、BOARD/ROSTER 或其他结果文件。

## 实际验证

- 执行前后 `git rev-parse HEAD`：均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。
- `git diff --cached --name-only`：空；没有暂存操作。
- PowerShell Parser：`[System.Management.Automation.Language.Parser]::ParseFile(...)`，0 个错误。
- `-Mode plan`：执行 `manual_cross_domain_experiment.ps1 -Mode plan -Direction both -Dataset all -LookaheadDepth 0 -Repeats 1 -Seed 20260924 -OutputRoot <unique $env:TEMP root>`，退出码 0。生成 12 case（3 dataset × 2 方向 × 2 system）。run manifest 和每个 case 拓扑断言全部通过：正向 6 个 source/client=`gridflux-beta-shenzhen`、destination/server=`gridflux-beta-shanghai`；反向 6 个角色相反；所有 case `controller=windows`、`measured_data_path=shenzhen<->shanghai`、`windows_in_data_path=false`、direct Linux endpoint flow=true。计划根为 `C:\Users\12563\AppData\Local\Temp\mce-topology-plan-1387bbfde849427a8183fed700bb76b9`。
- `-Execute` fail-closed 演练：`-Mode pilot ... -Execute` 在创建输出根前按预期拒绝；断言通过，输出目录不存在。静态检视无 SSH 调用代码。plan 演练输出明确 `SSH not invoked; payload not generated`。
- 运行结果只写在系统临时目录；没有 SSH、远端目录、payload、传输或 build。
- `git diff --check`、严格 UTF-8、尾随空白、完整 status 白名单核验：最终门禁执行结果记于下文。

## 运行状态与限制

本任务没有 transfer；transfer、integrity、evidence、wire accounting 均为 `NOT RUN`。没有 payload、远端目录、SSH session、构建、端口或 PID。本地计划只验证角色映射，不代表远端拓扑连通性、部署状态或深圳侧实验已经运行。

包状态维持 `READY_PENDING_DYNAMIC_QA`。05 revision-09 固定构建、04 动态 QA 和 00 最终放行仍是独立门禁；本任务不解除这些门禁，也没有增加真实执行能力。

## 下一步

由 00 验收 case/manifest 的方向角色字段。远端 preflight 和实验执行器需另立任务；在 05/04 动态门禁通过及 00 明确放行前，不执行实验。

## 写后门禁

- `git diff --check`：执行后记退出码 0。
- 四个授权文件严格 UTF-8 解码通过、尾随空白均为 0。
- `git rev-parse HEAD`：仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`git diff --cached --name-only` 为空。
- 授权路径 status 仅显示本任务允许的脚本、说明、result 和 last-message 四个文件；其他既有共享工作区状态未触碰。
