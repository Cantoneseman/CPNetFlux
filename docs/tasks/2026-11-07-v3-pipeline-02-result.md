# V3 流水线 02：交接调度与 writer CPU 归因结果

日期：2026-11-07。输入 f7dc827ce2a8476f00a785e3075ea5221fc82523。权威开发/证据均在 R770-1；R770-2 只同步固定提交、构建和验收。

## 交付与结论

完成有界队列唤醒对照、真实 pwrite CPU 归因，以及 V3 tree-download 预分配入口与成本观测。**没有证实新的净吞吐收益，整体流水线性能门仍未通过。** 保持 AvailableCapacity 唤醒、队列深度 2、预分配 off。EmptyQueue 唤醒只作为显式诊断选项，不推荐开启。

源码提交：

- c572b39cfaffaa9d3341d362056396b14f5db024：内部唤醒策略/诊断选择、并发与取消测试，供同提交 A/B。
- c876d642abaf15de22da647d6b5ac4bc87de0c23：依据负结果恢复 AvailableCapacity 默认。
- 545b92984fd3172582e48349e798673392fcb98b：cpnetflux-tree-download-client --preallocate off|full，接通既有 V3 FileTransaction，默认 off。上传接收端由 server 配置，客户端拒绝下载专用选项。摘要报告配置，whole-file timing 报告实际 wrapper 尝试/请求字节/耗时/CPU；sender/range 未观测值为 null。分配成本包含在 file wall、lane CPU 和完整 task 内。

以上阶段均显式提交、推送核实的 GitHub remote 并回读 SHA；最终文档提交与备份回执见证据根 HANDOFF/backup JSON。payload、日志、凭据和构建产物不进 Git。

## 归因依据

三次 f7dc827 锚点：每 GiB producer wait 为 11800/12501/12403，writer 约 0.404 CPU 秒，manifest 仅约 5 ms。perf 因 perf_event_paranoid=4 拒绝，没有改 sysctl/权限。

独立 strace 记录 reader 26472 / writer 14794 次 futex entry、writer 17408 次 pwrite64。原汇总仅完整行耗时、不含 resumed 行，不能用其累计值解释全部 CPU/墙钟。c876d642 的 client-only pwrite preload 探针记录 17408 次/1 GiB、约 0.361 CPU 秒；writer 线程 user/sys 约 0.042/0.371 秒。探针时钟开销计入，只支持实际 kernel 写路径占主要 writer CPU 的近似归因。两种诊断都通过输出校验，但排除出性能组。

## 同提交交叉实验

仅在各自组内比较。1 GiB 同源、单 lane、tmpfs 源/目标、compression off、CRC32C、verified_chunks、every16 manifest、commit-sync none、direct 写、depth 2；有效 identity chunk 1 MiB、17408 DATA 帧。原生 RDMA 仅是固定载体，本轮不改 RDMA。每次先核对双端同 commit/config/version/八个二进制 SHA-256、显式 bind、路由和计数。

| 策略 | 次数 | task 秒 | task Gbps | 文件秒 | 文件 Gbps | 接收 lane+writer CPU 秒/GiB | producer waits |
|---|---:|---:|---:|---:|---:|---:|---:|
| 容量可用唤醒（c572b39） | 4 | 1.535310 | 5.595 | 0.406178 | 21.148 | 0.701401 | 12370.5 |
| 排空后唤醒（c572b39） | 4 | 1.581561 | 5.432 | 0.436157 | 19.695 | 0.731030 | 2977.5 |
| 预分配 off（545b929） | 3 | 1.537421 | 5.587 | 0.406388 | 21.137 | 0.700495 | 12510.0 |
| 预分配 full（545b929） | 3 | 1.636732 | 5.248 | 0.489903 | 17.534 | 0.689031 | 8676.0 |

各指标分别取中位数。唤醒四对 AB/BA/AB/BA，预分配三对 AB/BA/AB。同组均使用 blocking wait + deadline timer；早期锚点 timeout-poll task 计时有量化误差，不进入正式表。

排空唤醒：producer waits 减少约 75.9%，进程 voluntary switch 中位数 12531.5→3823，但文件吞吐下降约 6.9%、接收线程 CPU 增加约 4.2%。拒绝该性能假设，恢复默认。

预分配：writer write wall 中位数 0.359085→0.193840 秒，但额外全文件分配中位数 0.255268 秒/0.255199 CPU 秒，文件 wall 0.406388→0.489903 秒。CPU 总量仅小幅变化，文件/完整 task 吞吐下降约 17.0%/6.1%。提前准备损失重叠，不能只展示 write_seconds 下降。保留显式能力、默认 off，不推广为盘上结论。

14 个正式样本均通过应用校验、独立完整 SHA、终态、原生 QP、预算和清理。源 SHA-256 为 5b53fc30ea3168dca63211cc135946cc9d8a5a0f08e8c03cbc57f1d9d4825f23；外部 SHA 在 task 外，建连、文件校验/提交在 task 内。两组源码不同，不能混作候选前后成绩。

## 测试、资源与清理

- 队列/非法策略先 RED 后 GREEN；15 个定向测试重复 30 次全通过（450 次），并发 buffer ownership stress 覆盖两策略。
- framed wire/allocation 2 测试重复 20 次通过；41 个 TreeTransferOptions 测试通过。
- 新预分配参数测试先以 unknown option 失败，接通后通过；真实 V3 receiver-policy smoke 8 用例通过，覆盖非整块 payload、空文件、实际分配尝试/大小、耗时位于总量内、上传未观测与 range 默认。
- 最终源码 545b929：R770-1 全量 **557/557**，255.28 秒；R770-2 相关 **152/152**，38.05 秒，无失败/跳过。临时测试 token 仅在进程环境，不进命令/日志/仓库。
- 六个预分配样本两端 guard 全通过：最低 MemAvailable 24.99 GiB，任务 RSS 峰值 2.068 GiB/端；每端一个 1 GiB tmpfs 源或目标，零 owned swap/host swapout，源物理读为 0。RSS 与 tmpfs 分开报告。
- 每次输出在 finally 清理；最后核对源 inode/长度/完整 SHA 后删除。两端本轮 tmpfs 根均移除，生产 PID 126766/164834 仍存活。证据与轻量 sidecar 位于实验目录。
- 补入口前一个 harness 尝试因未知 --preallocate 在传输前失败，guard 未观察到 receiver，保留 failed 记录并排除出正式样本/内存通过率。首次新统计编译有字符串引号错误，修复后全构建通过；离线分析字段替换错误已修复，原始样本未修改。

## 边界与下一步

CQ response/flush 增量仍存在。预分配 off 三样本接收端 resp_cqe_error/flush 为 257/256，full 为 193/192，发送端两组均 129/128；RNR/out_of_buffer 本组为 0，但未解释 CQ 或旧 peer-mapping。RDMA 优化继续暂停，稳定性门未通过。

下一轮评估**保留 payload 所有权的无额外复制批量写入**：确认连续 offset 多帧能否组成 bounded iovec，将实际写入与 verified chunk 记账解耦；仅真实写成功才推进 checkpoint/lease/FILE_END。先测非连续 offset、部分写、EINTR、错误、断连排空与恢复，再与 direct pwrite 同提交固定源对照。初始沿用 depth 2/现有 buffer ownership 上限，iovec 管理与记录成本全部计入 CPU/task。此前带复制 coalesced 负结果仍有效，不能称为已验证无复制方案。无法保持契约时先收敛设计，不强改协议。

未实现独立可调三阶段全局 worker 池；未完成持久化、100 Gbps、1 Tbps 或生产 readiness 验收。

证据根：/home/Su/experiments/CPNetFlux/2026-11-07-v3-pipeline-03。
主要文件：acceptance/wake-comparison.json、allocation-comparison.json、pwrite-probe.json、formal-hca-deltas.json、allocation-integrity-memory.json、CTest 日志、逐样本 metrics/route/QP/guard/cleanup receipts、HANDOFF/备份回读。
