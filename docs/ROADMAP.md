# CPNetFlux 性能路线图

更新日期：2026-11-05
最近诊断源码：`d4103e85c0837bbe027ca95cbff00a8de3bb4cce`；原 48 样本基线仍按其输入提交解释。
当前开发分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`

## 当前目标

在 R770-1/R770-2 单端口 100GbE/RoCE 上，建立真实文件端到端性能证据，再按剖析结果优化。单端口路线不等于 10 端口 1Tbps 聚合验收。

原基线已采集 48 个样本，42 通过、6 个 64 GiB 下载失败，详见 [基线结果](tasks/2026-11-04-r770-rdma-e2e-baseline-01-result.md)。[V3 缺失块握手修复](tasks/2026-11-04-v3-resume-mask-01-result.md) 和 `fd10e16` 的 CQ 等待修复没有自动使完整文件门通过。

11/5 的[协议尾部诊断](tasks/2026-11-05-r770-rdma-tail-trace-01-result.md)已定位一条确定故障路径：64 GiB 文件发布且外部 SHA-256 正确，range 8 的 FILE_RESULT 两端均成功读写，但服务端在等待 172.614 秒后做 V3 lease finalize 返回 InvalidArgument。payload I/O 会刷新默认 30 秒租约，CommitPending 阶段没有独立预算或续期，最终整文件校验耗时约 149.912 秒，导致成功结果到达时租约已过期；该通道没有发 DirectoryEnd，客户端继续等待。逻辑时钟重现同样的 25 秒成功、172 秒失败。此次是诊断与观测提交，未修改或修复租约行为，64 GiB E2E 仍为 NO-GO。

当前下一步先修复 V3 commit/result 阶段的租约生命周期、任务截止和 peer-close/错误传播，保留 owner/epoch/attempt 与过期回收边界；再显式配置下载接收端的 checkpoint/final-verify 策略，重做完整文件验收和受控对照。批量 checkpoint 16 仅为诊断变量，不能升为默认或视作已获性能收益。RQ/CQ、流水线及微批继续排在正确性门之后。

完整范围、计时口径、阶段验收与性能目标见 [单链路 100G 性能提升计划](tasks/2026-11-04-performance-roadmap.md)。本轮路线决策见 [2026-11-04 性能决策](DECISIONS/2026-11-04-performance-route.md)。下表保留路线制定时的历史证据，当前测量以基线结果为准。

## 当前证据

| 类型 | 已测证据 | 不能推出 |
|---|---|---|
| 原生链路 | `ib_send_bw` 约 92.59 Gbps | CPNetFlux 文件传输达到 92.59 或 100 Gbps |
| CPNetFlux RDMA direct bench | 单连接长传输约 30.35 Gbps；4×64 GiB 组端到端约 82.97 Gbps | 文件系统、manifest 和完整任务时间有相同速率 |
| V3 真实文件 | 早期历史结果约 3.29 Gbps | RNR 修复后的当前文件性能；必须按当前任务复测 |
| 硬件拓扑 | 每台 R770 一张 100GbE/RoCE 端口 | 10×100Gbps 多链路或 1Tbps 扩展 |

稳态 payload rate、含建连/提交的 task rate、每 lane 吞吐分别记录，不合并计时口径。测试需固定提交、双端同配置构建，逐文件校验并保存 route、端口计数、CPU/RSS 和原始日志。

## 串行执行阶段

| 阶段 | 主要工作 | 决策门 |
|---|---|---|
| 0. E2E 基线 | 对照原生带宽、CPNetFlux direct bench、V3 真实文件；1/4 连接、4/64 GiB、至少 3 次重复；核实同一大文件是否真正并行 range | 完整性通过，稳定与 task rate 分开，失败可解释；完成后再定优化项 |
| 1. 瓶颈归因 | 分解建连、注册/清零、RQ/CQ、帧复制、checksum、调度、写入/writeback、manifest/final verify | 主要耗时可被重复样本定位，不凭单一计数器推因果 |
| 2. RDMA 资源与完成队列 | 测接收池预算、RQ/SQ 深度、补投递、CQ 批处理及完成等待；维持槽位所有权和有界反压 | 找到低内存且长传输稳定的窗口；若收益不成立，保留现状 |
| 3. 三阶段流水线 | 按 profile 解耦读/checksum、网络收发、接收校验/写入/提交；比较有界暂存和现有 bounded pipeline | 端到端收益、RSS 预算、恢复与尾包语义均通过 |
| 4. 小文件自适应微批 | 对比优化后的逐文件基线、固定微批、自适应微批；测 4–256 KiB 与 1K–100K 文件规模 | 改善完整 T_ready 和 p95，成员 checksum、原子提交、独立恢复通过 |
| 5. 条件式服务率调度 | 只有观察到 worker 异质性、队列偏斜或拖尾时，比较 SR-Dispatch/BAP | 多次重复收益成立且无公平性、饥饿和恢复回归；否则不实现 |
| 6. 单链路稳定验收 | 冻结代码/配置/数据；双向 large/dense/mixed 重复测与超接收池长传输 | SHA 全过、CV ≤5%、无静默降级和未解释链路错误 |

每次仅改变一个主要因素。流水线队列有界；注册池、网络在途窗口和应用暂存分别记账。扩容缓冲本身不视作吞吐优化。微批需保留逻辑成员索引和成员级校验/恢复。RDMA WRITE、系统 MTU/NUMA 修改及 1Tbps 多端口设计均需独立决策。

## 分阶段目标

目标值用于计划与阶段门，不是当前成绩、工期承诺或保证值。真实文件需同时报告 payload rate 和从任务启动至校验提交完成的 task rate。

| 阶段目标 | 真实文件目标 |
|---|---:|
| 第一工程里程碑 | task rate 10–20 Gbps，且至少为同负载 post-RNR-fix 基线的 2 倍 |
| 中期大文件 | 单文件 4-lane range 并发 task rate 30–50 Gbps |
| 单端口冲刺 | 稳定 task rate 70–85 Gbps |
| 极限探索 | 85–90 Gbps，仅在测量与稳定性支持时 |
| 不承诺 | 应用有效吞吐 100 Gbps；或单端口实验证明 1 Tbps |

## 研究分支边界

开题中的三阶段流水线、SR-Dispatch/BAP、adaptive microbundle 是候选方案。批处理、文件聚合和流水线已有相关研究与工程实现；先完成 profiling 和 tuned-native/fixed 对照，再评估研究增量。小文件 T_ready 优先于只看 Gbps。CPSS(DCC) 独立管理，压缩不进入当前首轮传输热路径对照。

早期 S0-S8、Phase 和迁移路线已清理出当前工作树；相关实验结果若仍在 `docs/experiments`，按其输入提交和测量环境解释，不能自动继承为当前代码的 readiness 证据。
