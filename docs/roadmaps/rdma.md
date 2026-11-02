# CPNetFlux V3 RDMA 开发路线

更新：2026-11-01。输入提交：`1fa44e68a3240e20322f0e5537eef9aced5190c3`。
本路线整合 2026-10-31 的《RDMA瓶颈分析》和《RDMA补全与性能路线》，作为后续执行入口。两份原报告保留为分析历史；与本文件冲突的根因判断、实现示例、工期和吞吐承诺停止作为开发依据。

## 目标与证据边界

目标是建立稳定、资源有界、成本可观测的原生 RDMA 数据通道，使网络传输、内存复制、校验、文件写入和 checkpoint 的成本能够分别验证。不能用扩大缓存掩盖接收速度不足，也不能把原生网卡基准当成 CPNetFlux 文件吞吐。

| 层次 | 已有证据 | 可以说明什么 |
|---|---|---|
| 原生链路 | 历史单 QP/1 MiB 约 92.5 Gbps；10-31 四 QP/64 KiB 约 79.99 Gbps | 不同配置下的链路能力；两者不能直接合并 |
| CPNetFlux direct bench | 历史 2 GiB 单连接约 29.7 Gbps | 包含 V3 帧解析、复制和 payload 检查，不含文件落盘 |
| 10-31 正式文件矩阵 | window=1，12/12 成功并通过逐文件 SHA-256 | dense 中位 0.334，large-random 3.239，large-repeated 3.037，mixed 2.886 Gbps |
| 深窗口 | window=8 有完成超时，dense 曾只完成 137/2048 | 稳定性未通过；根因需要复现，不能仅由错误字符串推定 |
| 资源 | 接收池约 2.01 GiB/连接，四连接 RSS 约 8.24 GiB | 注册和初始化资源成本确实过大 |

10-31 原始结果以 `/home/Su/experiments/CPNetFlux/2026-10-31-v3-rdma-pure/RESULT.md` 为准。报告引言中的“4.38 -> 4.68 Gbps，提升 6.9%”与该正式矩阵不对应，停止作为本路线的性能基线。

## 对原报告的修订

1. 累计 `file_result`/CQ 等待是并发墙钟时间，包含远端处理和调度；不能据此认定发送侧完全没有问题，或把等待比例标成接收端 CPU 占比。
2. `512 MiB / 64 KiB = 8192` 个有效数据帧。`1.818 s / 8192` 约为 222 微秒，这是包含等待的 range 平均时间，不是单帧 CPU 测量。四 lane 的局部速率不能直接与整个文件的端到端吞吐相等。目标 80 Gbps 在四 lane 均分时的 64 KiB 预算约为 26.2 微秒/lane，不能沿用原报告的 11 微秒推导。
3. MTU 1024 可能影响分包开销，但同 MTU 的原生基准已经很高，不能承诺改到 4096 必然提速 10%-20%。不直接照抄在 INIT 状态修改 path_mtu 的代码；RDMA CM、端口和完整路径需共同校验。系统 MTU 修改另列受控实验。
4. RNR retry=7 表示无限重试，不能解释为“只重试七次”。普通 retry exceeded 与 RNR retry exceeded 必须区分；设备累计错误值也不能当成本次增量。
5. 消费者仍在使用的接收槽不能重新交给网卡覆盖。立即补投递必须使用不同且空闲的槽，池耗尽时必须执行有界反压。SRQ 或补充线程本身不能消除消费者过慢的问题。
6. 增大 SGE 只有在 payload 已注册且生命周期覆盖完成事件时才能省拷贝；不是改一个 max_sge 就能获得零拷贝。选择性 signaled 必须一起实现有序批量回收、尾部完成和错误处理。
7. SEND/RECV 也是合法的 RDMA 高吞吐设计，不要求为了“正确使用 RDMA”而改成 WRITE。RDMA WRITE 到内存不等于持久化落盘；mmap/page-cache 方案还需要验证脏页、同步和故障恢复，不能用 pread 获取远端文件页缓存地址。
8. SQ depth、SRQ、NUMA、CQ 批量化均是候选手段；保持可归因的逐项对照。撤销原报告按阶段承诺的 15-25/40/80 Gbps 和固定周数，按验收结果推进。

参考：rdma-core `ibv_post_send(3)` / `ibv_post_recv(3)` 的缓冲区完成生命周期，以及 NVMe RDMA transport specification 对 RNR retry=7 的定义。
- https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_post_send.3
- https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_post_recv.3
- https://nvmexpress.org/wp-content/uploads/NVM-Express-RDMA-Transport-Specification-1.0c-2023.12.27-Ratified.pdf

## 串行执行顺序

| 阶段 | 对应问题 | 设计与开发内容 | 验收后才能进入下一阶段 |
|---|---|---|---|
| A 并发稳定性 | 原 D4，深窗口超时 | 先审计同连接发送/接收所有权和 CQ 生命周期；并发回归复现；修复实际竞态，保留有界队列 | 同连接并发帧序、payload、完成标记正确；dense 2048 文件 window=8 三次成功、SHA 一致、恢复次数零；保留失败样本 |
| B 接收资源与建连 | 原 D3/D8 | 可配置有界接收窗口、注册/分配/post 分段观测；与 A 的稳定基线比较；必要时设计 credit 或独立 progress，随后才评估共享 PD/MR/SRQ | 验证 64/128/256 MiB 档位与 RQ2048 对照；记录 RSS、注册内存、建连墙钟和稳态吞吐；不以总量小于接收池的短测宣称反压已消除 |
| C CQ/SQ 开销 | 原 D5/D6/D7 | 实测 CQ 批量、短自旋与事件等待；按需做选择性 signaled 和有序 slot 回收，SQ 深度按 BDP/处理延迟调节 | 尾包/不足一个批次/多轮回收/错误退出正确；记录 CQE/WQE、空轮询、进程和线程 CPU；同负载无回归 |
| D 注册 buffer 生命周期 | 原 D2 | 应用借用注册 buffer/span，完成前不可复用；先减少发送拼接或接收复制中的一个，再评估 SGE | 明确每字节拷贝次数和 buffer 所有权；异常、取消、重连与 checksum/manifest 语义不变 |
| E 应用处理 | 原 S1-S6 | 根据实测 CPU、锁、写入和 checkpoint 数据分别选择帧放大、分配复用、写入批量与文件流水线 | 每项改动单独对照 direct bench 和四类实际负载，逐文件校验、恢复与崩溃一致性门禁 |
| F 可选单边传输 | 原 R2 WRITE | 只有 SEND/RECV 在 B-E 后仍是主瓶颈时设计 WRITE；协议含受限 rkey 生命周期、generation/attempt、完成通知、撤销与持久化确认 | 故障注入证明旧连接不能写入新任务，内存完成不冒充落盘完成；收益覆盖复杂度 |

MTU/NUMA 作为 B-C 的独立配置对照，不擅自修改系统网络；不和代码改动同一批混测。

## 本轮完成阶段 A

范围：审计 `sendPersistentTree` 的 writer/result-reader 并发与 `RdmaDataSocket` 的隐式发送刷新；增加同连接 duplex 回归，并修复已定位的发送状态竞争。

源码线索：`readSome()` 会调用 `flushStagedSend()`；V3 pending window>1 的结果读取线程可与下一文件的发送线程并行。两线程可能同时修改 staged slot、sendInFlight、send CQ 和 MR 内容。现有“立即补接收槽”已经存在，因此不直接把深窗口失败归因到缺少该机制。

非目标：本阶段不改变 RQ2048、MTU、frame/checkpoint 协议，不加入 SRQ、WRITE、选择性 signaled 或应用层吞吐优化。

预计文件：`rdma_data_socket.{h,cpp}`、`rdma_socket_bench.cpp`、RDMA 回归脚本/CTest、任务证据，以及 AGENTS/ENVIRONMENT 的传输方式事实说明。

验收：先记录旧版行为失败；修复后 duplex 与原单向 bench 通过；window=8 dense 三次通过并校验，随后补 mixed 和大文件边界。无法复现或未通过的项保持未验收，不以增加 timeout 或回退 window=1 替代修复。

2026-11-01 结果：代码固定在 `48fca292e9a2ca5a6235ce7d38e085a0dc3b0b48`。旧读路径隐式刷新发送槽的竞态已修复：reader 只处理接收，writer 用 `flushWrites()` 或非缓冲消息发布尾部。硬件 duplex 回归先观察到旧版帧身份/顺序不匹配，再验证新版三次通过；两节点同时双向各 `3 GiB + 17 bytes` 校验通过。window=8 dense 三次均 2048/2048、pending 高水位 8、恢复零；mixed 514 文件、4 GiB 大文件及反向 dense 下载均通过 SHA-256。R770-1 全量 CTest 482/482 通过，R770-2 相关 52/52 通过；RDMA-off 37 项通过、硬件项按条件 skipped。详细证据见仓库 `docs/tasks/2026-11-01-v3-rdma-stability-01-result.md`。

## 下一阶段 B 的新增依据

同提交、同配置、同单连接/MTU1024/QD16/RQ2048 的 direct bench 补测：

| 有效负载 | 服务端吞吐 | 服务端接收完成等待 | 传输窗口 |
|---|---:|---:|---:|
| 2 GiB | 29.93 Gbps | 约 2.2 微秒 | 0.574 秒 |
| 3 GiB + 17 bytes | 11.13 Gbps | 1.457 秒 | 2.315 秒 |

两例完整性通过；这组结果只做了各一次，尚不是稳定吞吐分布，但足以否定“2 GiB 短测证明持续 RDMA 反压已排除”。约 2.01 GiB 接收池可能掩盖短测中的供给不足。下一阶段必须按 2/3/8 GiB 和更长持续负载复测，分开接收队列补充、CQ 事件、RNR 参数、消费者停顿和连接 teardown，先确定等待来源，再缩减接收内存；不能只把剩余差距归到应用 CPU。

本次两端 Ethernet netdev errors/dropped 增量为零，但 HCA 的 `out_of_buffer`、`rnr_nak_retry_err` 和 completion/flush 计数有非零增量。保存原值和差值，不能声称 HCA 零错误，也不能把所有增量都解释为 teardown。下一阶段为每个 case 拆分数据期与关闭期计数。原始 HCA 字段按设备原始单位保留，不直接当作 IP payload 字节。

## 通用实验门禁

- R770-1 单人串行开发；只提交明确文件并推送 GitHub，回读 SHA。R770-2 只同步固定提交，不独立改源码。
- 两端同配置构建、同源码提交与二进制 SHA-256；先通过相关 CTest，再做互传。
- 显式使用 `192.168.100.1/2` 与 local bind，记录 route、接口和 HCA 计数前后值；区分 TCP、原生 verbs、控制 TLS 和数据 TLS。
- 证据包含命令、配置、版本、方向、数据规模、重复次数、退出码、summary、hash、CPU/RSS、计时语义和失败样本。凭据及大 payload 不入仓库。
- 吞吐按成功传输的有效字节除以实测墙钟；并发计时不得相加当总时长；线程累计墙钟不标为 CPU。
- 对比 direct bench、实际文件、dense/mixed 和超过接收池的大文件，区分初始化与稳态；不能只靠小于接收池的 2 GiB 样本认定道路已经完全畅通。

后续每阶段只依据本阶段实测结果决定实现，不把候选方案或预估收益当成完成事实。

## 2026-11-02 长传输停顿根因与单变量修复

本轮基线 `92382ad7d7a41f4dc22d094850726267be8389b4` 增加可选阶段计时、收发 CQ 等待、CQ error/flush、RQ 与 HCA 计数、关闭屏障。基线 3 GiB 样本出现发送端单次 CQ 等待 2.065 秒、接收端 1.532 秒，RQ-starvation/RNR 相关计数在数据期增长。确认 CM 默认 `min_rnr_timer=0` 对应 655 ms 后，修复提交 `300805c299fd5bca0b4a8d8107159e79556d5214` 在两端连接建立后设置编码 12（0.64 ms RNR retry interval），并由硬件回归查询验证。

固定版本同配置重跑后，单连接 3/8/64 GiB payload 中位约 30.28/30.33/30.35 Gbps；3 GiB CV 从 20.49% 降至 0.29%，8 GiB CV 从 6.38% 降至 0.13%。单连接 2 GiB 无收益（30.19→30.06 Gbps），说明修复针对更易耗尽接收队列的持续负载。4×64 GiB/连接重复三次，组端到端中位 82.97 Gbps、CV 0.32%；同路由原生 perftest 三组均值 92.59 Gbps，语义不同，不作为协议等价比较。

RNR/无缓冲计数在短 retry interval 下仍增长，说明接收队列短时空档仍存在；有效数据期间没有 CQ work-completion 错误，QP teardown 时的 flush 单独统计。每连接 RSS 约 2.1 GiB、缓冲分配/清零约 1 秒、64 GiB 发送端累计 CQ 等待约 6.74 秒仍待处理。下一项应分别细化 SQ 完成回收与 RQ 补投递，不把问题混成泛化的 RDMA 开销。
