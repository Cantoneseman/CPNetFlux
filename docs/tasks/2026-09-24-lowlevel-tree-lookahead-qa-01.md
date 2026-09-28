# LOWLEVEL-TREE-LOOKAHEAD-QA-01：有界 lookahead/preconnect 设计独立审查

- 路线/任务版本：R2026-09-24.9 / v1
- 责任：04 测试与质量；验收：00 总指挥
- 目标：独立审查 01 交付的 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-01-result.md`，判断它是否足以授权 03 做最小代码实现。
- 关键设计：默认 off；lookahead depth<=1；每 worker 一个候选；全局最多 2 个 pending control；当前协议不预建未来 data socket，保持 data socket=0；控制前置只在安全元数据/manifest/方向条件满足时进行；stale generation、失败、取消、resume/changed/skip 必须回退原路径；不改 wire、manifest、checksum/resume、scheduler/compression/IO backend。
- 非目标：不改代码/任务板/ROSTER，不建 worktree，不构建/测试/SSH/实验，不派发 03，不合入；只写本任务结果和 last-message。
- 必须检查：目标/非目标是否清楚；控制候选状态机和 ID/generation/attempt 关系；upload/download 边界；资源上限/FD/取消/服务拒绝；与 control reuse worker/file parallelism 的相互作用；默认关闭与旧服务端回退；验收矩阵是否覆盖 dense A/B、fresh/no-range/resume_partial、空文件、changed、失败/取消、hash/manifest、FD/CPU/wall；实现白名单和停止条件是否具体；是否正确拒绝 data preconnect 和第二 control 连接扩张。
- 证据边界：dense profile 是 WSL loopback 12/12 hash success，不是 WAN/GridFTP/100Mbps 结论；telemetry QA-08 为 PARTIAL，no-range/resume on/off、10-pair overhead、wire/frame/manifest 等价仍未完成。QA 不能把设计审查升级为性能通过。
- 结论格式：PASS（可授权 03）/PARTIAL（需补项）/BLOCKED；列 Critical/Important/Minor；明确最小修改要求。若 PASS，允许 00 创建 v1 实现任务，但本轮 QA 不得触碰源码。
- 唯一写入：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-qa-01-result.md`；UTF-8/LF/无尾随空白、git diff --check、HEAD/index/source parity 复核。
