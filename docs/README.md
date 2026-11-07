# CPNetFlux 文档入口

本页是仓库文档索引。项目当前事实以 R770-1 权威工作树、最新固定提交、验收记录和原始实验数据为准；历史文档保留证据，不自动代表当前路线或 readiness。

## 当前优先阅读

1. [项目入口](../README.md)：项目定位、当前性能状态和开发入口。
2. [性能路线图](ROADMAP.md)：唯一当前性能主线。
3. [详细开发计划](tasks/2026-11-04-performance-roadmap.md)：阶段、指标、门槛和目标。
4. [R770 环境](coordination/ENVIRONMENT.md)：SSH alias、主机角色、管理/数据地址、工具链和实验边界。
5. [三阶段收口结果](tasks/2026-11-07-v3-stage-decoupling-01-result.md)与[执行指南](V3_TRANSFER_STAGES.md)：最新实现、配置、性能处置和剩余门。
6. [流水线优先决定](DECISIONS/2026-11-07-v3-pipeline-priority.md)：RDMA优化暂停；[RDMA路线](roadmaps/rdma.md)与[原基线](tasks/2026-11-04-r770-rdma-e2e-baseline-01.md)保留历史及后续参考。
7. [研究基线](RESEARCH_BASELINE.md)：论文与历史证据边界。

## 按用途查找

| 用途 | 入口 |
|---|---|
| 当前架构决策 | [性能路线决策](DECISIONS/2026-11-04-performance-route.md)、[R770 主开发环境决策](DECISIONS/2026-10-30-r770-primary.md) |
| 当前状态和历史结论 | [最新验收状态](tasks/2026-11-07-v3-stage-decoupling-01-result.md)、[实验目录](experiments/)、[任务记录](tasks/) |
| 性能脚本使用参考 | `tools/perf/` 脚本与当前基线任务 |
| 协议、架构和工程规范 | [设计](DESIGN.md)、[工程规范](ENGINEERING.md)、[可观测性](OBSERVABILITY.md) |
| 目录传输、恢复与安全 | [目录传输](DIRECTORY_TRANSFER.md)、[安全边界](SECURITY.md)、[发布限制](release/ALPHA_LIMITATIONS.md) |

## 历史材料规则

- `docs/tasks` 和 `docs/experiments` 中的记录按其输入提交与采样环境解释；历史 “go” 不等于当前单链路 100G 或生产 readiness。
- 深圳/上海云端主开发、旧协作角色、强制 worktree、旧 root 路径和旧 SSH 命令均为历史材料；当前操作规则读 [R770 环境](coordination/ENVIRONMENT.md)。
- 路线变化先更新 [决策](DECISIONS/2026-11-04-performance-route.md) 与 [路线图](ROADMAP.md)，再做源码实现。
