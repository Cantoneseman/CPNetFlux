# LOWLEVEL-TREE-TELEMETRY-QA-08：目录阶段观测实现与 Linux 证据独立审查

- 路线/任务版本：R2026-09-24.9 / v1
- 执行角色：04 测试与质量；验收角色：00 总指挥
- 目标：独立审查 telemetry 窄实现的代码正确性、旧事件兼容性及已运行的 Linux 测试证据，判断它是否可进入后续目录阶段 profile。结论不得扩展为性能优化或跨域验收。
- 非目标：不改代码/测试/BOARD/ROSTER；不新建或切换 worktree；不 SSH、不做跨域传输/清理、不启动新的性能矩阵、不提交 Git。只写本任务 result 和 CLI last-message。
- 固定源码输入：实现基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。03 候选位于 `C:\Users\12563\AppData\Local\Temp\cpnetflux-lowlevel-tree-telemetry-impl-01\worktree`，分支 `codex/LOWLEVEL-TREE-TELEMETRY-IMPL-01`；执行前核验其 HEAD/status 与任务回执，不能假设尚未变化。
- 必读：实现任务 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-impl-01.md` 与实现回执（候选 worktree 内 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-impl-01-result.md`，主仓 last-message 仅作角色答复）；契约 `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`、PLAN-07/QA-07 和本任务。
- 外部 Linux 证据目录：`D:\Project\CPNetFlux-evidence\LOWLEVEL-TREE-TELEMETRY-IMPL-01-local-20260924\linux-validation`。
  - bundle：`lowlevel-tree-telemetry-linux-evidence.tar.gz`，SHA-256 `7757a497593ef2e5fb4dad55b1d81e89377ae73bdcaabefedb6af93968239b70`
  - WSL 源树归档 SHA-256：`04fef0a5f848afd1d9c4148e93a9dec415339c448ebdf20724b2d2666dd31d1e`
  - 内部 SHA 清单记录 configure/build/CTest/summary 和 3 个相关二进制哈希；构建参数为 tests=ON、TLS=ON、io_uring=OFF。
  - 实测记录：build 112/112；定向 CTest 27/27；全单元 CTest 183/183，另有可选 `FileIoTest.IoUringContextReadWriteSmokeWhenAvailable` skipped（不算功能通过）；fresh 上传/下载 telemetry off/on 均退出 0，源/目标 tree SHA-256 相同；off 0 条新阶段记录，on 上传 90 行、下载 206 行。
  - Windows 源快照带 CRLF 的测试脚本在独立 WSL 构建副本中曾做 LF 归一化后重跑定向 smoke；审查时查明并记录归一化脚本名和范围，判断它是否仅影响测试脚本。不得把测试副本调整描述成仓库源码修改。
- 要求的复核：
  1. 独立检查候选 diff 是否限制在任务白名单；核对阶段边界、attempt/stream/worker 关联、空/skip/retry/error/nullability、并发计时、logger 故障是否可能改写 transfer/integrity，以及旧 event-log 消费兼容。
  2. 校验外部 bundle SHA-256，读取内部 SHA 清单和关键日志；复核 build、CTest、fresh on/off 结论及 skip 语义。用证据文件路径、行或 JSON 片段支持判断，区分本轮独立重算与复核 00 生成的日志。
  3. 输出可进入后续 profile 的结论（PASS / PARTIAL / BLOCKED）及 Critical/Important/Minor 问题；明确仍缺的 no-range/resume on/off 等价和正式 10-pair overhead gate。本任务不授权忽略这些缺口；判断它们是后续目录 profile 的硬前置还是可有限探索并说明理由。
  4. 不得从 telemetry on/off hash 一致或测试通过推导传输性能提升、阶段真实性超出现有代码观测能力，或跨域/100 Mbps 达标。
- 唯一写入：`docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-08-result.md`；写后检查 UTF-8、空白和工作区无源码/index 变化。
- 输出：本结果和 `docs/tasks/2026-09-24-lowlevel-tree-telemetry-qa-08-last-message.md`。按要求标出命令、退出码、证据根、限制、阻塞和可执行的最小下一步。

