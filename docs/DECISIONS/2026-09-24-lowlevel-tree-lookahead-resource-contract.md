# 2026-09-24 目录 lookahead 资源契约

状态：架构裁定，待 00 总指挥验收。适用路线：`R2026-09-24.15`。本文件只定义资源闸门，不授权实现或性能实验。

## 结论

固定实现快照不能证明 lookahead 候选准备的完整资源上界。保持 `reliableCandidateMemory=false`，显式请求 depth=1 时 `effective depth=0`；lookahead 性能实验继续 blocked。不得通过增大预算、进程 RSS 采样或“通常很小”的经验判断开启功能。

既有 1 MiB/候选、2 MiB/run 保留为未来实现的候选自有用户态堆内存硬上限，不是当前实现已满足的测量值，更不是线程栈、内核 socket buffer 或进程总 RSS 上限。FD 与并发约束继续有效：`depth<=1`、每 worker 至多一个 candidate、pending control `<=min(worker_count,2)`、candidate extra FD `<=2`、pending data FD 恒为 0。任一上界不可可靠计算、分配超预算、取消未收敛或资源计数不可靠时，整个候选回退至 depth=0，并走原文件传输路径。

这是对能力闸门的 fail-closed 裁定；是否停止该优化方向、改选已有证据支持的工作由 00 决定。本任务不批准替代性能方向。

## 证据与推理边界

资料 HEAD 为 `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；实现输入是基于 `3b0820dab6dc149f549bd3e81ef403ea7953c4e9` 的未提交快照，不是完整 Git commit。快照证据包 SHA-256：`2AF5C2DC166615FD4F0924BA335408624E8620B437B07B3528A98E08674A07C6`。

快照中 `tree_transfer_client.cpp:3091-3099` 明确将 `reliableCandidateMemory` 置为 false，并通过 `effectiveDepthAllowed` 得到 depth=0；QA-REVISION-09 也确认显式 depth=1 仍关闭。`tree_lookahead.cpp:10-37` 的 footprint 只把 `Candidate` 大小和三段字符串 capacity 相加；`tree_lookahead.h:38-64` 显示 fingerprint 内含动态字符串，Candidate 再持有 fingerprint。`tree_transfer_client.cpp:1301-1411,1669-1680` 还分配 CandidateRuntime、CandidatePreparation、shared ownership 控制块和 unordered_map 节点，并复制 fingerprint。由此可证现有 footprint 不是候选完整内存账本；不能据此断言实际一定超过 1/2 MiB。

准备路径在 `tree_transfer_client.cpp:1737-1755` 为每个候选启动 `std::thread`，闭包按值捕获完整 options；候选可达 worker 数量，线程数没有受 pending control 上限约束。标准线程栈大小未在该实现设定或计入预算。`tree_transfer_client.cpp:1777-1783` 会延后 token/TLS 候选，但这只是当前分支不做该连接的事实，不是 TLS/TLS 库分配已纳入上限的证明。候选控制客户端的 reply/read buffer 还会动态增长；快照 `tree_transfer_client.cpp:567-597` 的 readLine 追加接收数据，没有本契约可依赖的字节/行数上限。结论是当前资源总上界未知，而不是 RSS 观测不足。

## 预算对象与核算规则

| 对象 | 预算归属 | 约束与当前结论 |
|---|---|---|
| Candidate、Fingerprint、状态/代际/owner、reservation 元数据 | 候选堆预算 | 每个候选合计不超过 1 MiB，run 合计不超过 2 MiB。必须核算实际分配字节；`sizeof + string.capacity()` 只能作为部分下界/估计，不能作为通过证据。 |
| path、endpoint、options 及其复制、字符串容量、容器节点/bucket、shared_ptr 控制块、CandidateRuntime、CandidatePreparation、闭包/捕获 | 候选堆预算 | 全部计入上述 1/2 MiB。未来实现须使用有界固定存储/单一有界 arena，或可证明上界的 allocator；路径和 options 长度也须受限。allocator 元数据、对齐、舍入及失败路径必须在 arena 上限内。 |
| ControlClient、命令/回复行、reply vector/string、resolver/认证辅助对象 | 候选堆预算 | 全部计入。对端可影响的行长度、行数和解析暂存必须有硬上限；超限视为 candidate failed 并回退。快照无足够上界，故当前不得启用。 |
| TLS/OpenSSL 对象及内部堆分配 | 候选堆预算 | 只有经受控 allocator/静态上界核实后才可计入；否则 candidate TLS/token 模式必须在预留任何线程/连接资源前直接降级到普通路径。当前 “not cancellable” 防护不等于有内存上界。 |
| 准备线程栈、线程运行库对象 | 独立的准备执行预算，另计入总资源封套 | 必须限制并发线程数，使用明示且可审计的栈大小/线程池资源，并把栈保留量纳入峰值内存公式。现有每 candidate `std::thread` 使用平台默认栈，数量可随 worker 增长，故不可证明。RSS/虚拟内存采样不能替代栈大小上界。 |
| candidate 新建的 TCP control socket | FD 预算 | pending control `<=min(worker_count,2)`，所有额外 candidate FD 峰值 `<=2`。预连接不建立 data socket；candidate data FD 必须恒为 0。一个 TLS 包装如复用同一底层 fd，不另算 fd，但对象内存仍需计入。 |
| TCP kernel send/receive buffer、系统页/线程调度器开销 | 不计入 1/2 MiB 用户态堆预算 | 不能据此声称主机总内存有硬上限。若未来需要总主机资源保证，须另给 socket buffer/OS 级预算；本任务不扩大到该保证。 |
| 非 lookahead 的 manifest、worker、正常传输连接和进程基线 | 基线资源，不计入候选 heap 上限 | 做 FD 可行性门控时必须显式计入正常传输峰值增量，不能因其属于基线就忽略 RLIMIT 竞争。RSS 可作诊断记录，不作候选预算通过门。 |

1/2 MiB 是封顶值，不允许实现方以观测平均、峰值 RSS 或扩大常数来“证明”预算。允许的未来证明方式应能从有限对象布局和固定容量推导出字节上界，所有候选分配都落在可计量封套内；无法封套化的依赖/路径直接令 candidate 不 eligible。

## FD、并发与 fail-closed 公式

实现需分别计算启动时已打开描述符、后续正常传输最大增量与 candidate 额外描述符。通用门控为：

```text
fd_peak_bound = fd_sample_at_start
               + normal_transfer_fd_increment(worker_count, connections, control_reuse)
               + candidate_pending_control_fds
               + transient_fd_bound
               + safety_margin(32)
require fd_peak_bound <= RLIMIT_NOFILE.soft
```

`candidate_pending_control_fds <= min(worker_count,2)`，且 candidate 新增的所有 FD 合计 `<=2`；`candidate_pending_data_fds == 0`。`normal_transfer_fd_increment` 必须覆盖配置允许的 worker control 与数据连接、retry/handoff 的短暂重叠；不能可靠推导 `transient_fd_bound` 就关闭候选。Linux `getrlimit` 不可用、soft limit 为无限且无法给出替代上限、`/proc/self/fd` 采样失败/为无效值、或预算刚好等于/超过上限时，均 fail closed。当前 `sampleActiveFdCount` 在采样失败时返回 0（快照 `tree_transfer_client.cpp:1914-1928`），因此不能把 0 解释成无 FD；调用方必须区分失败状态。

控制复用配置不改变硬上限。`control_reuse=off` 可以使 candidate 控制连接有潜在价值，但不能获得更宽预算；`worker` 复用路径若不准备 control，则不应创建“metadata-only”异步线程来绕过内存/线程预算。全局 scheduler、worker 数/文件并发超出契约、FD headroom 变化、socket 借用/归还不匹配、重复 lease 或取消后线程仍存活，一律将候选置 failed/cancelled 并回到 depth=0。已开始的原文件传输按既有语义完成或报错，不得因候选失败改变文件完整性、manifest 或 resume 结果。

## 未来实现前必须满足的资源设计

本快照不满足 1/2 MiB 的完整可审计账本，也不满足准备线程栈上界。不得只把 `reliableCandidateMemory` 改为 true。若 00 决定另开资源重设计任务，至少需要：

1. 移除每 candidate 无界 `std::thread` 模式；采用固定数量、固定栈或无额外准备线程的执行结构，并给出 worker 数到准备线程数的静态上界。不得把 `pending control<=2` 当成线程数上界。
2. 将所有候选拥有的对象与字符串纳入 1 MiB/候选、2 MiB/run 的固定 arena/有界存储；options 仅拷贝候选所需的白名单标量，所有动态字符串有长度上限。任何 arena 超限立即取消该候选且不影响正常文件路径。
3. 对 control reply 行数/总字节、命令/解析暂存和 resolver/TLS 库分配给硬上限。不能证明 TLS/token 限额时，明确在 candidate 启动前排除这些模式；当前回退行为保持原样。
4. 对 FD 采样与 `getrlimit` 返回显式成功/失败，按上式预留正常传输峰值、candidate control FD、retry/handoff transient 及 32 个余量。候选数据 FD 仍为零。
5. start 前完整预算成功才可预留；reserve、分配、连接、handoff 任一失败均只影响该候选，回收资源并回退 depth=0。进程结果/现有 summary 应能证明 effective depth 与候选实际状态，但不得新增未经批准的 telemetry/schema 字段。

这些是再次评估 enable 的先决条件，不代表已批准修改。若控制库、系统分配器或 TLS 仍无法落在可证明上界内，则结论继续是停用本功能，而不是降低质量门。

## 实现白名单、计数点与质量门

本任务不授权代码变更。若 00 后续派发重设计，初始审阅白名单仅限 `include/cpnetflux/core/io/tree_lookahead.h`、`src/core/io/tree_lookahead.cpp`、`src/core/io/tree_transfer_client.cpp` 和 `tests/unit/tree_lookahead_test.cpp`；如需控制客户端/TLS、公共 schema、CMake 或新增 allocator 文件，先返回 00 补充任务范围，不得顺手扩大。

实现内计数至少覆盖：candidate heap arena 当前/峰值字节与失败次数；候选及准备线程当前/峰值数量、每线程预设栈字节；control 对象/reply 暂存上限命中；pending/峰值 candidate control FD；candidate data FD（必须恒为 0）；启动 FD、RLIMIT、正常传输峰值预留与最终 headroom；取消到 join/释放完成的状态。计数不写入未知外部字段。RSS/CPU/虚拟内存只作观测项。

04 独立质量门应覆盖边界路径、分配失败注入、超长/多行 control reply、最大 path/endpoint/options、TLS/token 排除、control_reuse off/worker、多 worker、retry/handoff 重叠、取消/断连、RLIMIT 不可用/低 headroom、`/proc` 失败和 fd 泄漏。每个向量证明超界时 effective depth=0、data FD=0、候选状态完整释放，且原始 transfer/hash/manifest/resume 行为不变。质量审查应静态复算所有对象与栈上限并检查实现没有未计动态分配；仅通过单测或观测 RSS 不足以开启。

仅当资源静态上界、fail-closed 故障注入、功能等价和 04 独立审查均通过，源代码被固定为 commit 或内容寻址归档且构建/二进制有对应 hash 后，00 才可考虑单机 A/B。跨域 A/B 还需单机证据稳定、运维重验两端资源和固定构建、以及 00 新任务授权；本文件不批准申请或执行该矩阵。

## 对 QA-REVISION-09 与 dense profile 的闭合

QA-REVISION-09 所述显式 depth=1 当前变成 effective depth=0 与本裁定一致，应继续报告为 lookahead 未启用；不把候选辅助测试通过提升为集成/性能通过。dense profile 的 12 个本地 loopback case 可说明此前测量的端到端结果，但不能证明本快照资源预算，也不能授权 lookahead A/B。已有 fp8 无端到端 wall 收益是留意争用的观测，不是因果结论。

下一步建议 00 暂停 lookahead 实验，并优先让 02 对已有 12-case dense profile 做只读逐配置复核：报告 wall、各并发阶段分布及 CPU/FD 等已有证据的实际覆盖，明确哪些指标缺失；不补造数据、不启动新实验、不据相关性声称因果。之后由 00 依据已观测支持的候选选择下一任务。本建议不自行批准路线。
