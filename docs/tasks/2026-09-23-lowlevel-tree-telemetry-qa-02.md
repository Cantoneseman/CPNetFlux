# LOWLEVEL-TREE-TELEMETRY-QA-02

状态：ready，路线 `R2026-09-23.5`
执行角色：04 测试与质量
固定源码输入：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`

## 目标

独立复审 03 的 `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02-result.md`，判断其是否忠实落实 `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`，并足以作为有限 telemetry 实现任务的设计输入。

## 非目标

不改设计、决策、源码、测试、runner、BOARD/ROSTER 或其他角色回执；不创建 worktree，不实现/构建/测试，不 SSH、不运行传输/实验、不清理、不操作 index、不提交 Git。若发现缺口，报告具体阻塞及最小修订，不自行修补。

## 输入

- `docs/DECISIONS/2026-09-23-tree-telemetry-contract.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02-result.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-plan-02.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-01-result.md`
- 相关旧 profiling 决策与 `tools/demo/run_alpha_demo.py` 消费路径（仅为核对兼容）

## 输出

只写：

- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-02-result.md`
- `docs/tasks/2026-09-23-lowlevel-tree-telemetry-qa-02-last-message.md`（仅当 CLI 确实生成）

## 独立验收清单

逐项给 `PASS / PARTIAL / BLOCKED` 并引用文件/段落：

1. v1→v2 是否明确撤销旧 `first_payload <= payload_io`，定义独立合法区间；慢首块例是否数值自洽。
2. `data_channel_finalize`、`transfer_complete_wait`、`manifest_finalize` 是否与源码顺序/责任边界相容；重叠和禁止相加是否明确。
3. append-only start/terminal 配对、孤立 start、坏行/重复/错配/未知 schema、写失败和 evidence 独立输出是否可实现且不改四维传输状态。
4. required/null 矩阵是否一致；run/file/attempt/worker/control/stream/transfer/path、attempt_kind、scope、ID 生命周期是否有自相矛盾或未声明空值。
5. 空文件、全 resume skip、非空 no-missing-range、retry、首块前失败和并发多 stream 状态是否互斥且可审计。
6. download `stream_identity` 的 schema_version、公共字段、span 关联键与 SessionInit 后追加绑定是否闭合；`event` 值和公共字段差异是否明确。
7. uint64 纳秒词法/范围/精度校验是否明确；JSON implementation 对原始数字 token 的限制是否被测试覆盖。
8. `run_alpha_demo.py` 混合旧/新日志的 `error_code` 行为是否可验收。
9. telemetry on/off 与 logger failure injection 的非干扰门禁是否保持独立观察 transfer、integrity、evidence、wire；mock 与真实链路范围是否清楚。
10. 报告、BOARD、任务单间路线版本、授权状态及 01 待复核项是否一致；无测试/实现证据时不得放行产品正确性。

若所有可阻塞实现的条目 PASS，给出 `PASS FOR IMPLEMENTATION DESIGN GATE`，仅表示可另派窄实现任务，不表示实现/测试/性能验收。任何关键 schema、生命周期、错误隔离或 01 未复核项不闭合时应判 PARTIAL/BLOCKED 并禁止实现。

## 派发正文

请独立对照决策与 PLAN-02 结果，不要复述 03 全文。每项列 PASS/PARTIAL/BLOCKED、证据定位和最小需要修订。只写规定的 result/CLI last-message；不改其他文件，不启动实现、构建、测试、SSH 或实验。结束后说明是否允许 00 创建实现任务；本轮不是实现授权。
