# PERF-QA-VECTOR-01：B8 规格向量的纯函数复核

- 状态：done，纯函数向量通过；B8 系统级验证仍 partial
- 路线版本、任务版本：`R2026-09-23.1 / v1`
- 发起人：00 总指挥；执行角色：04 测试与质量；验收角色：00
- 目标：只复核 02 `PERF-TOOL-PLAN-03 v3` 给出的 canonical UTF-8/JSON、非 ASCII argv、secret redaction、manifest hash 和两条 ledger hash 向量，记录可复现的纯函数结果，细化 B8 的 partial。
- 非目标：不运行项目 Python、CMake/CTest、runner、传输、故障注入或云端动作；不改 runner、C++、schema、测试、任务板；不声明 validator 已实现、不改变 01 timer 语义。
- 输入 commit：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；路线：`R2026-09-23.1`。若输入变化则停止并报告 superseded。
- 允许修改：只追加 `docs/coordination/receipts/04-quality.md` 的 `PERF-QA-VECTOR-01 v1` 小节。

## 执行范围

1. 使用 PowerShell/.NET `SHA256` 与明确 UTF-8 bytes，逐项复核 02 回执中的 5 个 expected digest：non-ASCII argv、redacted argv、manifest、ledger event 1、ledger event 2。
2. 记录 canonical bytes 是否与回执完全一致；对 argv 参数重排、manifest 单字节改写、ledger payload 改写/错误 prev hash 做纯字符串/哈希不相等断言。若没有可执行 validator，不把这些断言写成系统拒绝已通过。
3. 检查 secret 原文没有出现在 redacted canonical bytes；不得把测试 secret 写入回执、日志或仓库文件。

## 验收

- 只追加回执，逐项列 expected/actual、退出码和 PASS/FAIL；任何向量不一致则停止并报告。
- 明确这只是纯函数/规格向量复核；项目测试、故障注入、timer event 和实现覆盖保持 NOT_RUN。
- HEAD 不变，`git diff --check` 通过，暂存区为空；不修改 BOARD/ROSTER。

## 执行回执

- 实际执行：已由既有 04 质量聊天完成，CLI session `30363` 正常终态，退出码 0。
- 结果：5 个 expected/actual SHA-256 向量全部匹配；canonical UTF-8、secret redaction、无 BOM，以及 argv/manifest/ledger 篡改不相等断言均通过。
- 输入 HEAD 与结束 HEAD 均为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；暂存区为空，`git diff --check` 通过。
- 未运行：项目 validator、Python/CMake/CTest、runner、故障注入、timer/ledger 恢复、SSH、构建、传输、实验和清理。B8 纯函数复核不等同于系统验收。
