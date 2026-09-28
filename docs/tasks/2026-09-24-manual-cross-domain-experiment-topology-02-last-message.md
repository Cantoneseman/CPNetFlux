已完成 MCE-TOPOLOGY-02。计划的全局 manifest 和每个 case 现在都明确记录：Windows 是控制入口且不在数据路径；深圳与上海 Linux 端点直接传输。默认深圳为 source/client、上海为 destination/server，反向时交换角色。

PowerShell Parser 检查零错误；plan 演练生成 12 个 case，正反向端点映射断言通过；`-Execute` 在创建输出目录前 fail-closed。本轮没有调用 SSH、创建远端目录或生成 payload。`git diff --check` 通过，HEAD 未变，index 为空。

产物：[更新后的脚本](D:/Project/CPNetFlux/tools/experiments/gridftp_compare/manual_cross_domain_experiment.ps1:1)、[使用说明](D:/Project/CPNetFlux/tools/experiments/gridftp_compare/MANUAL_CROSS_DOMAIN_EXPERIMENT.md:1)、[结果回执](D:/Project/CPNetFlux/docs/tasks/2026-09-24-manual-cross-domain-experiment-topology-02-result.md:1)和[独立摘要](D:/Project/CPNetFlux/docs/tasks/2026-09-24-manual-cross-domain-experiment-topology-02-last-message.md:1)。状态仍为 `READY_PENDING_DYNAMIC_QA`，未解除 05/04 门禁。