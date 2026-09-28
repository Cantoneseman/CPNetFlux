# CPNetFlux 100 Mbps 跨域证据驱动性能优化路线

- 状态：当前执行路线。
- 路线版本：`R2026-09-23.3`。
- 输入：用户最新明确目标；本地实时 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`。此 revision 是文档基线，最近代码实现仍为 `3b0820d`。

## 总目标

从 `D:\Project\GridFlux Beta\_analysis\2026-09-16-control-reuse-full\results` 及其原始日志、summary、环境记录读取旧实验，识别 CPNetFlux 单文件和多文件目录传输的实际性能问题；围绕传输热路径、系统调用、数据复制、控制连接/并发调度、文件 I/O 和状态持久化做有针对性的底层或架构级优化。每次只引入可隔离、可回归的一组变化，最终在当前已知深圳—上海 100 Mbps 跨域服务器上用同数据、同方向、匹配并发及安全配置，与真实 GridFTP 做对照验证，尽量达到相当水平。

广州—深圳 100G 链路现阶段不能使用，不作为当前验收目标。100 Mbps 结果只解释这两台服务器和这条链路，不外推到 100G/生产 readiness。

暂定量化目标：单文件与 dense/mixed 目录分别按有效匹配格比较 CPNetFlux/GridFTP 配对中位吞吐，争取不低于 0.90；有效重复的波动和样本分母单独报告。此门槛需 01/04 根据可执行矩阵确认；不得删除 unmatched、失败或 blocked 项来抬高分母。校验档按 none 与真实执行的 CRC32C/verified-chunks 分开报告；none 仅用于吞吐诊断，外部 SHA-256 验收在计时外执行。

## 当前已知证据边界

- 旧重测 218 planned、216 rows，来自云端 `16b3773` 加 78 条 dirty 修改；不是可复建的固定 commit。
- 68 个旧 fail_correctness 的 CSV hash 字段一致，但未重新计算 payload hash；标签主要暴露证据/审计问题，不能据此判定数据损坏或全部功能通过。
- 历史跨配置汇总显示单文件约落后 8–12%、目录约 33–49%；不能单凭汇总断言瓶颈位于网络、控制、系统调用、校验或磁盘。
- 旧 scheduler 结果有 compression off 污染；不得作为策略优劣证据。
- 旧云端 GridFlux-Beta 两处仍是 dirty 历史工作树，保持只读；绝不访问 `/root/projects/CPSS(DCC)`。

## 执行顺序

1. **取证（02 + 00）**：核对旧原始实验关键文件 hash；按方向、dataset、file parallelism、per-file connections、backend、checksum、compression、repeat 分层重算有效吞吐、有效样本数和离散度；审计缺失/重复 case、失败状态、hash 字段、runner/client timer；抽样 client/server 日志，寻找阶段或 CPU/syscall 证据。区分直接观察、旧报告记载和待验证解释。
2. **现场预检（05）**：使用原登记 SSH alias 严格 host-key 校验，只读复核深圳/上海挂载点、磁盘预算、CPU/内存、监听端口、进程/批次、GridFTP 服务和依赖；活动 owner/空间异常未澄清前不写入、不构建、不运行。每个相关挂载点保留至少 10 GiB 加源码/build/payload/evidence 峰值预算。不得触碰历史源码树或受保护项目。
3. **源码映射与设计（03 + 01，04 review）**：针对取证结论映射当前 C++ 热路径；比较候选方案并给出收益机制、风险、回退方式和独立验收。SENDV/CRC/manifest/调度都只能作为候选，先证据后选择。冻结 wire/TLS、checksum/resume、worker 行为边界。
4. **实现及本地验收（03，04）**：使用 `codex/<task-id>` 独立 worktree、固定输入 commit；协议/持久化按 TDD；可用 Linux 工具链下运行相关单测、CTest、transfer/resume/hash smoke。旧 QA 的 B5–B8 runner 门禁限制旧矩阵脚本资格，不泛化阻塞独立本地 C++ 实现。
5. **固定提交远端验证（05 + 02 + 04）**：从已提交明确 commit 生成归档，记录 commit/status/archive SHA-256、两端 build 配置依赖和 binary SHA-256；用独立 run 根目录，不在 `/root/projects/GridFlux-Beta` 开发。任务冻结双向、single/dense/mixed、匹配真实 GridFTP 命令/流数/认证/数据加密等级、重复次数、seed、hash 与 timer。任何进程/端口/资源 gate 不满足即停止。
6. **逐项归因和继续优化（00 + 01 + 02/03/04/05）**：每轮只依据固定构建配对结果调整实现。transfer/integrity/evidence/wire accounting 分列；不外推 100G readiness。

## 当前任务与停止规则

- `HIST-FORENSIC-01`：02 重新审核旧原始数据，输出匹配格表和证据缺口；不运行新实验。
- `ENV-PREFLIGHT-02`：05 只读复核两台服务器是否满足隔离 build 和小型双向 pilot 门槛；不清理、不运行。
- `G100-SENDV-01` 与 `R2026-09-23.2`：被本路线 supersede，停止依赖；sendmsg 只是待评估候选，不预判收益。
- 新提交代码可独立筹备，但实验必须等待历史数据审计、实现验收和最新资源 gate。事实冲突时先更新决策/任务版本，不覆盖历史证据。
