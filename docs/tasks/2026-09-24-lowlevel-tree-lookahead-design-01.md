# LOWLEVEL-TREE-LOOKAHEAD-DESIGN-01：有界目录 lookahead/preconnect 契约

- 路线/任务版本：R2026-09-24.9 / v1
- 责任：01 架构与需求；协同输入：03 实现、04 质量；验收：00 总指挥
- 目标：依据 `docs/tasks/2026-09-24-lowlevel-tree-dense-profile-local-01-result.md` 的 12-case dense profile，冻结一个可实现、可回退、可验收的目录 lookahead/preconnect 设计，使当前 worker 传输当前文件时有限度准备后续文件的控制/数据连接，减少每文件固定等待。设计必须覆盖 upload/download。
- 证据依据：本地 WSL loopback 128×1 MiB 文件，upload/download × fp1/fp8 × 3；所有 12 case exit=0/hash match；fp8 无端到端 wall 收益；高并行时 control_prepare/data_connect/first_payload/interfile_idle 阶段增大。此证据不是 WAN/GridFTP 结论。
- 非目标：不修改源码/协议帧/握手/manifest 格式/checksum/resume 语义；不改变 scheduler/compression/IO backend 默认值；不连接云端、不 SSH、不运行新实验、不合入 sendv；不把 lookahead 设计扩展成全年调度重构。
- 输入固定：实现基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；telemetry 候选只作为观测输入；主仓当前文档 dirty，不操作 index。
- 设计必须回答：
  1. lookahead 深度与资源上限（控制连接、数据 socket、文件描述符、内存、每 worker/global），默认 off 与显式 enable 方式；
  2. upload/download 对称与不对称之处，连接何时可预建、何时必须延后到 transfer ID/manifest/resume 决策；
  3. ready/pending/in_use/failed/cancelled 生命周期，代际/路径/attempt/stream 绑定，避免把 stale socket 用到 retry/resume；
  4. 取消、断连、服务端拒绝、部分文件失败、resume skip/partial、changed file 与 manifest flush 的清理和回退；
  5. 与 control reuse worker、file parallelism、connections 的关系，如何避免 fp8 仅增加争用；
  6. 旧 wire compatibility、旧服务端回退、可观测字段和最小阶段事件；
  7. 只允许先实现的最小 slice（建议深度 1、每 worker 一个 pending、只预建可安全复用的 control/data 前置）；明确禁止的扩展；
  8. 验收矩阵：本地 dense 12-case A/B、fresh/no-range/resume_partial、失败/取消、hash/manifest、wall/CPU/FD；哪些门是实现前置，哪些可后置。
- 交付：只写 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-01-result.md`，包含目标/非目标/契约/状态机/接口影响/验收/风险/下一步任务版本；UTF-8、无尾随空白；不写 BOARD/ROSTER。
- 完成后等待 00 收口；没有架构契约不得派发 03 代码实现。
