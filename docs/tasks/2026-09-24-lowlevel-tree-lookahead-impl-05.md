# LOWLEVEL-TREE-LOOKAHEAD-IMPL-05

- 任务版本：`R2026-09-24.12 / v1`
- 执行角色：03 核心实现
- 独立质量验收：04 测试与质量
- 总指挥验收：00
- 状态：ready；本任务已获 04 的 PLAN-REVISION-04 `PASS`，允许创建独立实现 worktree。

## 目标

在固定实现基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 上实现一个默认关闭、深度最多 1 的目录 worker lookahead 机制，重叠下一 Pending 文件的 metadata 或受限普通 control login 与当前文件处理，降低多文件目录的固定控制等待。目标是形成可审查的底层机制，不宣称性能收益或跨域结果；后续 04 验收通过后才允许云端验证。

## 输入与契约

- 当前资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`
- 设计契约：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md`
- 契约复审：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-revision-qa-02-result.md`（PASS）
- v2 实现计划：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-revision-04-result.md`
- v2 计划复审：`docs/tasks/2026-09-24-lowlevel-tree-lookahead-plan-revision-qa-04-result.md`（PASS）
- 本任务固定代码输入：`3b0820d`；实施前在 worktree 检查基线和源码 parity。若固定输入与计划锚点不符，停止并回报 BLOCKED。

## 允许修改范围（白名单）

只允许在独立 `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05` worktree 修改：

- `include/cpnetflux/config/tree_transfer_options.h`
- `src/config/tree_transfer_options.cpp`
- `src/core/io/tree_transfer_client.cpp`
- 可选新增 `include/cpnetflux/core/io/tree_lookahead.h`
- 可选新增 `src/core/io/tree_lookahead.cpp`
- `tests/unit/tree_transfer_options_test.cpp`
- 可选新增 `tests/unit/tree_lookahead_test.cpp`
- `CMakeLists.txt`（仅注册上述 core/test 源）

禁止修改：file transfer/download client、任何 framed/control protocol、manifest/checkpoint/resume/checksum、scheduler/compression/IO backend、服务端、旧 summary/event/runner/消费者、默认 control reuse、BOARD/ROSTER/决策、云端文件、凭据。

## 必须实现的行为

1. 新增目录客户端选项 `--lookahead-depth`，默认 0，只接受 0/1；负数、>1、溢出、缺值沿现有 invalid-argument 处理，绕过 parser 的非 0/1 运行时值 effective depth=0。默认 0 必须保持原行为。
2. 仅普通 worker scheduler 启用；global scheduler effective depth=0。`control_reuse=worker` 只做 metadata-only candidate，不建立第二 worker control；`control_reuse=off` 才允许每 worker 一个普通 control candidate，全 run pending ordinary control ≤2。
3. 候选 eligibility 必须是 `TreeFileStatus::Pending`，且方向、规范相对路径、endpoint/TLS/options fingerprint 和 metadata 校验可用。Completed、Changed、Failed、Transferring/未知状态均不 reservation、不推进 candidate cursor、不占 pending/FD/memory。
4. 候选阶段禁止 `SIZE/MDTM/EPSV/REST/STOR/RETR`、data FD、range、SessionInit、transfer/attempt ID、manifest 写入和外部事件/summary/schema/reason/key。固定源码没有 control ID：候选如需 control 只持有进程内 opaque `ControlClient`/句柄；不得引入/序列化 `control_id`，不得把文件 `transferId` 当控制身份。
5. 直接收紧当前真实 `runTreeScheduler` worker lambda 的裸 `state.nextIndex++`：普通 claim、candidate reserve、cancel 归还、handoff consume 必须由同一 `SchedulerState::mutex` 保护的统一 helper 管理。一个 index 只能处于 free/candidate_reserved/claimed 之一；cancel 归还恰好一次；handoff 只消费一次；不重复、不丢失、不改变 manifest 序列化顺序。
6. 网络 connect/auth/login、close/wait 和阻塞等待不得持有 scheduler mutex；带 owner/generation 回锁提交。任何 stale、owner/fingerprint mismatch、cancel、TLS/login reject、server cap、connect/close/wait、worker first error、manifest failure 释放资源并回到该 index 原路径，不改 manifest transfer/integrity 状态。
7. 固定资源门：depth≤1；每 worker candidate≤1；全 run pending ordinary control≤min(worker_count,2)；extra FD≤2；data FD=0；candidate memory≤1 MiB、全 run≤2 MiB；Linux `getrlimit(RLIMIT_NOFILE)` 失败/不可信或 `active_fd + pending_control + 32 > soft_limit` 时 effective depth=0。不得增加无界线程/连接池。
8. upload/download handoff 后保留原文件处理顺序和 retry/resume/checksum/manifest/mtime/wire 语义；候选失败在同一 index 调原 controlForFile，不增加 attempt/transfer ID。

## 测试与静态验收

必须新增/更新窄单元测试覆盖：

- parser：默认/0/1，>1/负数/溢出/缺值，worker/off/global effective depth。
- eligibility：upload/download Pending 正常；Completed/Changed/Failed/Transferring/未知、方向/路径/fingerprint/metadata/stat 失败；断言 no reservation、no cursor advance、no resource。
- reservation：两个 worker 同 index 竞争；普通 claim vs candidate reserve；cancel 恰好归还一次；handoff 一次、二次失败；owner/generation/fingerprint stale；cancel-vs-handoff；close/wait 与 run cancel 清理。
- opaque identity：固定源无 `control_id/controlId`；文件 `transferId`/REST/attempt 不作 candidate token。

必须运行并记录真实命令/退出码：

1. `git diff --name-only 3b0820d -- <whitelist>` 与反向禁改路径检查。
2. 构建配置/编译：`cmake -S <src> -B <build> -DCPNETFLUX_BUILD_TESTS=ON`；`cmake --build <build> --parallel <jobs>`。
3. 定向单测：新增 lookahead/options 测试及受影响 tree 单元测试。
4. 完整 unit CTest；任何失败、skip、环境缺依赖必须如实记录，不能用 skip 当 pass。
5. 静态门：`rg -n "control_id|controlId" src include` 无匹配；lookahead 不引用 file data client/connect、manifest 写入、协议命令；worker lambda 无裸 `state.nextIndex++` 旁路；固定禁改模块 diff 为空；默认 depth=0。
6. loopback smoke 至少覆盖 tree upload/download、fresh、empty、Completed skip、resume/changed、control_reuse worker/off；源/目标独立 hash、manifest 最终状态、旧 summary/event 字节等价。不能用 loopback 结果宣称跨域收益。

## 输出证据

只写任务结果/摘要到：
- `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-05-result.md`
- `docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-05-last-message.md`
- 如需构建日志放在外部 `D:\Project\CPNetFlux-evidence\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`，不要把大 payload 放仓库。

结果必须包含：实际 worktree 路径、分支、输入 commit、修改文件清单、构建/测试命令和退出码、静态门结果、hash/manifest/summary 证据路径、未运行项、失败/阻塞和给 04 的验收交接。不要 git add/commit，不要 SSH，不要云端实验。

## 停止条件

若需修改白名单外文件、增加外部 schema/key/event/reason/control identity、候选阶段发文件命令或建 data FD、无法证明 index reservation 不变量、资源硬门不可靠、固定源码 parity 不成立，立即停止并报告 BLOCKED，不通过顺手修复绕过。
