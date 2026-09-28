# HIST-FORENSIC-01：旧跨域性能证据取证

- 状态：review
- 路线版本、任务版本：`R2026-09-23.3 / v2`（v2 收紧计时边界验收；v1 未交付结果）
- 发起人：00；执行：02 实验与证据；独立审阅：04
- 目标及理由：从旧本地原始实验数据中定位单文件和目录相对 GridFTP 的性能差距及可支持的瓶颈线索，为底层实现选题提供证据。
- 非目标：不连接云端、不跑新实验、不恢复大 payload、不改源码/runner/原始证据/共享 BOARD，不声称因果或生产 readiness。
- 输入：执行前实查 HEAD/status；旧云端构建身份为 `16b3773 + dirty`。证据根为 `D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full`。
- 必读：本任务、路线决策、旧 `results/`、`RETEST_REPORT_ZH.md`、2026-09-17 两份决策及当前 runner/schema（只读）。
- 写入白名单：仅新增 `docs/tasks/2026-09-23-hist-forensic-01-result.md` 作为独立回执；不要覆盖或修改既有角色总回执。小型脱敏派生表可写 `docs/evidence/HIST-FORENSIC-01/` 并列明来源，不复制大日志/payload。
- 方法：审计 CSV/summary/plan 行数、重复/缺失 key 和关键输入 hash；按 dataset、方向、system、file parallelism、per-file connections、backend、checksum、compression、resume/fresh、repeat 分层；抽样 single/dense/mixed 的命令、client/server 日志和 case 证据；逐项隔离 68 fail_correctness、compression off 污染、blocked io_uring、ENOSPC、缺失 resume。CSV `elapsed_seconds` 先视为 runner wall；只有能从双方原始客户端证据恢复且事件边界可比的 transfer duration 才计算配对 goodput。否则单独报告 runner-wall 比值和边界差异，不将其当成纯传输对照。对每个格检查双方输入内容、实测流数、退出状态和 integrity/evidence；未匹配配置不得混入配对分母。独立验证单文件 SHA 与 GridFTP tree hash 的规范化算法，所有覆盖范围写清。
- 验收：提供 single/dense/mixed 配对结果表、样本数、排除原因；传输时间无法对齐时明确留空/blocked，不用 CSV wall 冒充传输计时；解释 hash 是 CSV 字段还是 payload 重算；至少 3 个有来源的候选问题并明确“直接观察/文档记载/待验证假设”；不得把汇总差距直接等同 syscall/manifest 因果。派生数值可用明确脚本/命令复算；原始数据只读。
- 产物：02 自有回执结果，供 00/01/03/04 选择实现任务。

## 派给角色的消息

续接 ROSTER 中既有 02 聊天，先核对真实本地文件和输入 HEAD。逐格重算，不只复制旧跨配置平均；保留旧源 dirty 状态与未知项。不要新建实验、修改 runner 或 BOARD/ROSTER。最终完整报告写入本任务 result 文件。
