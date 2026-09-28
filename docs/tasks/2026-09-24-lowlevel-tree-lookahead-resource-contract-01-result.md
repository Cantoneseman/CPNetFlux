# LOWLEVEL-TREE-LOOKAHEAD-RESOURCE-CONTRACT-01 执行回执

日期：2026-09-24（Asia/Shanghai）
责任：01 架构与需求；验收：00 总指挥
状态：资源上界未获证明；lookahead 保持 fail-closed，性能实验 blocked。

## 输入与范围

- 资料 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`，分支 `main`，ahead 11。起始工作区已有其他角色的 tracked/untracked 文档改动；本角色不触碰。起始 index 为空。
- 固定实现基底：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。审阅对象是 `build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true` 的未提交源码快照（5 个 tracked 修改、3 个 untracked 源文件），证据包 SHA-256 `2AF5C2DC166615FD4F0924BA335408624E8620B437B07B3528A98E08674A07C6`；不称其为完整 Git commit。
- 只新增本回执与 `docs/DECISIONS/2026-09-24-lowlevel-tree-lookahead-resource-contract.md`。未修改源码、测试、任务单、BOARD/ROSTER、云端或 Git index；未提交。
- 读取任务指定的 lookahead revision-02、impl-05、QA-revision-09、BOARD 和快照中的 lookahead header/implementation、tree transfer client 与 unit tests。当前源文件 parity 对比固定基底通过，详见门禁。

## 资源裁定

**已观察事实：**快照的 footprint 统计 `sizeof(Candidate)` 与 Fingerprint 三个字符串 capacity，却没有将 `CandidateRuntime`、`CandidatePreparation`、shared_ptr 控制块、容器节点、重复 fingerprint/options、控制客户端 reply 缓冲及线程栈完整计入。每个候选用 `std::thread`；该数量受 worker/candidate 数量影响，不能从最多两个 pending control 推出线程上界。快照将 TLS/token candidate 延后，但控制客户端仍有动态 reply/read buffer，且依赖分配未有硬上限。`sampleActiveFdCount` 以 0 表示读取失败，单靠该值不能验证 FD headroom。

**架构推理：**现有计量不足以证明实际超过 1/2 MiB，但足以证明它不是完整上界。默认线程栈、控制库/TLS 分配、字符串和对端控制回复均缺少这里可依赖的硬限额；RSS 是运行采样，不能替代逐对象上界。因此将 `reliableCandidateMemory` 保持 false，requested depth=1 仍须 effective depth=0。1 MiB/候选、2 MiB/run 只作为未来候选自有用户态堆的硬上限；线程栈另设显式固定上界，不能混入或隐去。保留 `depth<=1`、每 worker candidate<=1、pending control<=min(worker_count,2)、candidate extra FD<=2、pending data FD=0 及失败/取消回退约束。

## 输出与下一步

决策文件把候选堆、准备线程栈、TLS/control 分配、kernel socket buffer、FD 和进程基线分别核算，并给出 fail-closed FD 公式、未来实现前置条件、计数点、文件白名单、04 质量门和再次申请单机/跨域 A/B 的门槛。此结果不改变研究方向、不授权源码实现或任何实验。

建议 00 暂停 lookahead 性能验证；下一项可先派 02 对已有 dense 12-case 结果做只读逐配置复核，列出 wall、并发阶段及 CPU/FD 证据的实际覆盖和缺失，再由 00 选取后续可测方向。该建议不是新路线批准。

## 实际门禁与未运行项

- 写前：`git rev-parse HEAD` → `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`git diff --cached --name-only` → 空；`git diff --quiet 3b0820d..HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp` → exit 0，固定源码与资料 HEAD 间无差异。
- 写后实际门禁：`git diff --check` → exit 0；Git 仅对预先存在的 tracked 协作文档输出 LF→CRLF 提示；PowerShell 使用 UTF-8 strict decoder 检查两文件、末尾 LF 与所有行尾 → PASS；`git rev-parse HEAD` 仍为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；`git diff --cached --name-only` 为空；复跑 `git diff --quiet 3b0820d..HEAD -- src/core/io/tree_transfer_client.cpp src/core/io/file_transfer_client.cpp src/core/io/file_download_client.cpp` → exit 0。两个新增文件均未暂存。
- CMake/CTest、单测、分配故障注入、传输与性能实验、SSH/云端操作：全部 `NOT_RUN`，符合本任务范围。未把文档门禁表述为实现、测试、传输或性能通过。

## 下一角色

00 验收本裁定并决定是否暂停 lookahead、派发既有数据只读复核或选择其他路线；若未来继续 lookahead，先单独派发有界资源架构/实现方案，经过 04 独立质量审查后再考虑实现和实验。
