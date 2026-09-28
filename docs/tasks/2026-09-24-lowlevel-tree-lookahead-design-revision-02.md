# LOWLEVEL-TREE-LOOKAHEAD-DESIGN-REVISION-02：闭合下载时序与遥测契约

- 路线/任务版本：R2026-09-24.10 / v1
- 责任：01 架构与需求；独立复审：04 测试与质量；验收：00 总指挥
- 固定输入：实现基线 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9`；资料 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；设计结果 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-01-result.md`；04 审查 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-qa-01-result.md`。

## 目标

修订有界目录 lookahead 设计，使 03 可以在后续独立 worktree 实现一个可回退、可机械验收的 plan-only 控制候选 slice，同时不改变当前传输协议语义。修订必须把 04 指出的两个 Critical 阻塞和资源/白名单缺口收成单一契约。

## 必须闭合的决策

1. 下载候选阶段一律不发送远端文件级 `SIZE`/`MDTM`/`EPSV`/`REST`/`RETR`，不制造任何文件副作用；候选只保存本地 manifest 及方向/端点/配置指纹，handoff 后严格按现有 `processDownloadFile` 顺序重新执行元数据检查。删除原文允许下载 preflight SIZE/MDTM 的表述。
2. 设计不新增 `lookahead` summary 对象、schema 字段或 JSONL 事件类型。第一 slice 只使用已有可选事件/summary 载体和已冻结的 reason 值；若现有载体无法表达命中/未命中，则只在实现内部计数并保持旧输出，不能写未知键。明确 `control_id` 为唯一已有控制连接标识，不引入 `control_lease_id`。
3. 拆分 `metadata_reserved`、`control_pending`、`control_ready`、`in_use`、`cancelled`、`failed` 状态，定义同一 file/generation 的原子 reservation、owner、lease 单次交接、stale/cancel race 与 double-use 断言。
4. 写出逐文件实现白名单、禁止修改文件/接口，以及硬停止条件：任何未来 data FD、早发 EPSV/REST/STOR/RETR、manifest/checksum/resume/scheduler/compression/IO backend 变化、未知 telemetry key、预算超限或 lease 不匹配都必须关闭候选并回退 depth=0。
5. 把 `control_reuse=off`、多 worker、服务端 control 上限、TLS/登录拒绝、取消/断连纳入最小验收向量；固定 `fd_extra_peak<=2`、pending data FD=0、候选内存预算和 `getrlimit` 不可用的回退规则。CPU/RSS 若不能在规格中定阈值，标为观测项而不是通过门。

## 非目标

不改源码、wire frame、manifest/checksum/resume、scheduler/compression、IO backend、默认 control reuse、单文件 API；不预建未来 data socket；不启动构建、测试、SSH 或实验；不建立 worktree；不更新 BOARD/ROSTER。

## 交付与验收

- 只写 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-design-revision-02-result.md` 与 CLI 摘要。
- 结果须逐条引用本任务的 5 项闭合决策，并列出旧设计中被删除/替换的矛盾句子。
- 必须包含纯函数/状态机验收表、上传/下载同一时序图或等价伪代码、资源/FD/RSS 采样位置、逐文件白名单和硬停止条件。
- 只读复核 `git rev-parse HEAD`、源码 parity、`git diff --check`、空暂存区；不得声称实现或性能通过。
- 若无法在不新增 telemetry schema 的前提下表达命中/未命中，明确选择“只保留内部计数，外部输出不变”，不得自行扩展 schema。

完成后等待 04 独立复审；本任务结果不授权 03 直接写代码。
