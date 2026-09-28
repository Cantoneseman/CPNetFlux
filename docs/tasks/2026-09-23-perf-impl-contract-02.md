# PERF-IMPL-CONTRACT-02：源码级 effective telemetry 字段映射

- 状态：done，等待 04 参考复审
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：03 核心实现；验收角色：00，随后由 04 参考复审
- 目标：只读定位 CPNetFlux 单文件与目录 upload/download 现有可观测的 compression/scheduler/control-reuse/file-IO effective 状态、计数来源和覆盖范围，为 QA-02 B5 提供真实字段映射候选。
- 非目标：不改 C++、头文件、CMake、runner、schema、测试、协议、默认值、校验/resume、IO backend 或 scheduler；不定义 01 的 timer/auth/GSI/verify 语义；不运行 CMake/CTest、传输、SSH、构建、实验或清理。
- 输入 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；若 HEAD 或路线变化则停止并报告 superseded。
- 必读资料：本任务、03 既有 `PERF-IMPL-PLAN-01` 回执、04 `PERF-TOOL-QA-02` 回执、02 `PERF-TOOL-PLAN-02 v2`，以及相关现有源码。
- 允许修改：只追加 `docs/coordination/receipts/03-implementation.md` 的 `PERF-IMPL-CONTRACT-02 v1` 小节。

## 交付内容

- 列出单文件和 tree upload/download 各路径已有字段/计数器、产生位置、scope（process/worker/file/run）、是否可覆盖失败/重试/并发。
- 区分“请求值”“运行时 effective 值”“实际 observed counter”；指出缺失字段不能被 runner 参数或 wire 比值替代。
- 给出最小候选字段映射与无法从当前源码证明的缺口；不提出未经授权的协议消息或默认行为改变。
- 记录现有测试入口可覆盖与不能覆盖的边界，所有测试保持 NOT_RUN。

## 验收

- 只读、append-only 回执；记录 HEAD/status、路径定位、未运行项和 `git diff --check`。
- 不声称 telemetry 已实现、不创建 worktree、不改共享 Git index；交付后由 00 决定是否形成实现任务。

## 执行回执

- 03 已通过既有聊天 `01a0af8d-b8fd-7c21-ada2-986374bda893` 实际执行，CLI 终态退出码 0。
- 已在 `docs/coordination/receipts/03-implementation.md` 追加 `PERF-IMPL-CONTRACT-02 v1`；HEAD 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，暂存区为空。
- 未运行代码、测试、构建、SSH、清理或实验；映射仍是候选事实，不等于 telemetry 已实现。
