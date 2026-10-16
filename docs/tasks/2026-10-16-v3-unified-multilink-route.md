# V3 统一编排与十链路路线任务单

- 任务 ID：V3-ROUTE-RESET-20261016
- 输入提交：922401b49450b47366a026a8285bc72fb52067dd
- 权威工作区：/root/projects/CPNetFlux
- 分支：codex/v3-route-reset-01
- 目标：固定后续 V3 的统一 worker、单文件/目录收敛和 10×100Gbps LinkSet 开发路线。

## 任务范围

1. 固化当前实现边界：pipeline + persistent dynamic queue 已共享 lease 语义，但 persistent + global scheduler 仍被拒绝。
2. 固化统一执行器设计：WorkItem → WorkLease → Worker → ConnectionEpoch → frame → checksum → commit。
3. 固化十链路设计：LinkSet、容量/RTT/BDP/队列/在途/健康度、加权最小负载或 weighted deficit round-robin。
4. 固化大文件 range 迁移、连接 drain/cut、watchdog、故障回收和默认值门禁。
5. 为每个实现阶段登记允许路径、非目标、验收和回滚方式。

## 非目标

- 本任务不修改传输实现，不扩展协议。
- 不启动深圳—上海实验或实验室性能实验。
- 不修改桌面端。
- 不删除历史原始证据、凭据、GridFlux-Beta 或 CPSS(DCC)。

## 验收

- 决策文档和任务单能独立描述当前边界、依赖、接口契约、测试顺序和回滚方式。
- 路线明确把单文件、目录和 global scheduler 收敛到统一执行器。
- 路线明确把 10 条 100Gbps 链路建模为 LinkSet，而不是简单增加连接数。
- 工作区文档 diff 通过 git diff --check。
- commit、push 和远端 SHA 回读一致。

## 交接

第一阶段：V3-WORK-UNIFY-01。
下一阶段依赖：V3-WORK-UNIFY-01 通过单元/loopback/CTest 后，才能开始 V3-LINKSET-01。
