# V3 Payload Reuse 01 结果

日期：2026-11-06。输入提交 `6025f54efeb4fe87d69b32b15fcd9513fa431921`；实测实现提交 `7aab4e683e1a029a298731a819d32a6c471750bb`。R770-1 单人串行开发，R770-2 接收固定提交构建验收；管理操作均通过免密 SSH。

## 结论

保留发送端有界 payload 缓冲复用。默认每文件最多 4 个固定帧缓冲，每个容量 65532 字节，合计 262128 字节；1 GiB 实测每次分配 4 次、复用 17404 次。生产线程与 lane 线程合计 CPU 时间中位数从 0.425706 降到 0.329747 秒，下降 22.54%。这是两个发送线程的 CPU 时间，不是整个进程或两端总 CPU。

没有证明端到端吞吐提升。接收文件阶段中位速率为 17.832948 / 17.770356 Gbps，约下降 0.35%；整 CLI task 为 3.103407 / 3.048293 Gbps，约下降 1.78%。保留理由是消除逐帧 payload 分配、降低发送线程 CPU，不能把它写成文件吞吐优化成功或 100G 验收。

## 实现与边界

- 消费者在 writeFrameBuffered 返回且本帧发送/故障处理结束后，才通过下一次 pop 归还 vector；producer 的 push 在原队列锁内取回空闲 vector，无额外锁往返。
- 深度 2 的排队预算仍为 131064 字节。生产者、消费者和空闲缓存分别拥有缓冲；固定帧 SPSC 路径总数最多 depth+2。空闲缓存允许 depth+1 个、最多队列预算加一帧，即默认 196596 字节；不能把空闲缓存上限与排队上限简单相加当成实际峰值。
- 按 vector capacity 计缓存字节，拒绝超预算容量；close/cancel 释放缓存，close 仍排空已排队数据，cancel 唤醒等待者。
- 新增分配次数、复用次数、累计新分配容量、理论所有权预算观测。累计新分配容量不是 RSS 或通用峰值内存；本轮实测没有丢弃后重新分配，数值等于四个 payload 缓冲容量之和。
- 帧协议、CRC、默认队列深度、校验/checkpoint/commit 策略、RDMA 注册池与接收端均未改。压缩和 RDMA 内部缓冲不计入这 262128 字节，仍存在 RDMA 暂存复制。

## 测试与被发现的问题

旧发送实现上，真实 socketpair 文件传输的独立 allocation wrapper 回归测到 64 次全帧分配，未通过 <=4 次门；修改后该测试连续 20 次通过，并校验实际发布内容。wrapper 仅进入 Linux 64-bit 测试二进制，不进入生产程序。

最初将空闲缓存限为 depth 个时，30 轮压力之前的 20 轮中有两轮出现累计第 5 次分配。原因是消费者归还缓冲后可能等待空队列，此时最多 depth+1 个缓冲空闲、另一个归生产者；过小缓存会丢弃仍可复用的缓冲。修正缓存容量后，10 项队列/所有权/关闭/取消测试连续 30 轮通过，生产总预算仍为 depth+2。保留首次失败日志 `acceptance/recycle-repeat.log`，没有将其改写为通过。

R770-1 CMake 构建通过，全量 CTest 542/542 通过，207.92 秒；R770-2 固定提交构建后相关 CTest 146/146 通过，42.27 秒。测试包括缓冲真实分配门、并发变长帧、取消/关闭、持久传输、故障/恢复、压缩、TLS/策略及本机原生 RDMA。完整测试环境显式提供临时测试 token 和本机 RDMA bind，凭据不写入仓库。互传前核对八个二进制 SHA-256、构建配置和版本完全一致。

## 同负载实测

两端 tmpfs，单文件 1 GiB、单 lane、CRC32C、压缩关闭、manifest every_n_chunks/16、commit_sync=none、默认 payload depth=2；verified_chunks 各三次，候选再补一次 full 控制。源为随机 8 MiB 块重复 128 次；同一 inode、同一摘要，压缩关闭。显式绑定 `192.168.100.1` / `192.168.100.2`，路由走 `eno17095np0`，保留 verbs QP 与协议终态证据。

| 指标，中位数 | 基线 6025f54e | 候选 7aab4e68 |
|---|---:|---:|
| 整 CLI task wall，秒 | 2.767905 | 2.817949 |
| 整 CLI task rate，Gbps | 3.103407 | 3.048293 |
| task rate CV，% | 0.842808 | 0.000579 |
| 接收文件阶段 wall，秒 | 0.481689 | 0.483386 |
| 接收文件阶段 rate，Gbps | 17.832948 | 17.770356 |
| data connect，秒 | 2.085900 | 2.112890 |
| producer CPU，秒 | 0.260711 | 0.205403 |
| lane CPU，秒 | 0.164995 | 0.124344 |
| 每次 producer+lane CPU 之和的中位数，秒 | 0.425706 | 0.329747 |
| readAtAll 累计 wall，秒 | 0.125917 | 0.140460 |
| socket_write 累计 wall，秒 | 0.097182 | 0.070550 |
| 发送端 file_result 等待，秒 | 0.220281 | 0.277182 |
| 接收写入累计 wall，秒 | 0.232998 | 0.233865 |
| 接收 checksum 累计 wall，秒 | 0.086304 | 0.087116 |

全部七个正式样本源页在传输前后全驻留、发送进程物理读为零，CRC、独立输出 SHA-256、数据/控制终态与原生 QP 通过，无恢复。SHA 在 CLI 计时之外；每个输出校验后立即删除。full 单次 task wall 为 2.969463 秒、2.892757 Gbps，接收文件阶段 14.138654 Gbps，finalize 0.124298 秒；这只是完整性控制，不构成 full 策略性能对照。

另外保留一个排除样本 `candidate-tmpfs-1g-c1-verified_chunks-d2-01`：在 peer 测试之后测到源页仅 180793/262144 驻留，传输期间进程物理读 348614656 字节；客户端协议成功、缓冲分配门通过，但工作负载门失败，未计入正式性能样本，未声称其外部输出 SHA 已通过。其输出已清理。之后增加计时外源 SHA/预热和 payload 启动前驻留断言，以 `candidate-warm` 新标签测三次，未覆盖失败证据。没有关闭 swap、清系统缓存或修改主机策略。

计时存在嵌套和重叠，不从 task wall 相减累计等待或 I/O 时间。接收文件阶段含提交/结果处理，排除冷建连，但不是线速稳态 payload rate。每次冷建连约 2.1 秒；每端 2048 个接收槽加 32 个 spare、每槽 1052672 字节，合计约 2.04 GiB（摘要 receive_buffer_pool_bytes 仅计 2048 个、约 2.01 GiB），仅说明资源规模，不证明具体因果。本轮为先基线后三次候选的小组对照，测试及源预热发生在两组之间，不是随机配对 A/B，极小 CV 不构成长时稳定性证明。

RNR/out_of_buffer 在七个正式样本均为零；CQ response error/flush 增量仍出现 1/0、65/64 或 129/128，尚未归因。该问题和生产稳定性门继续保留，不因本轮传输成功而关闭。

## 证据、清理与交接

原始证据根：`/home/Su/experiments/CPNetFlux/2026-11-06-v3-payload-reuse-01`。包括 `acceptance/allocation-red.log`、`allocation-repeat-green.log`、`recycle-repeat-green.log`、`ctest-full.log`、`ctest-peer.log`、`matching-builds-*.json`、`comparison.json`，以及 metrics、双端日志/路由/端口计数快照。实验与验收脚本在证据根，不提交大型 payload 或构建产物。

验收命令：`cmake --build build -j6`；证据根的 `validate.py 192.168.100.1 ctest-full.log .` 在 R770-1 完成全量测试；R770-2 使用 `validate.py 192.168.100.2 ctest-peer.log 'Framed|Persistent|Bounded|Checksum|rdma|whole_file|compression|receiver_policy'`。日志名采用独占创建，复核时须指定新文件名。原生实验由 `experiment.py run <commit> <label> <policy> 2 <count>` 固定版本、创建服务、验证并逐次清理；重新运行需要先在独立临时根生成源并保存新摘要，不能假定本轮源仍存在。

最终源 SHA-256 为 `e840f1aae5c59cbaef02e4de9a2045fdfa2e901e53f71d8aab5f8e09e884fdbd`。删除前再次核对，双端 `/dev/shm/cpnetflux-v3-payload-reuse-20261106` 均已删除，R770-2 释放 1 GiB 源空间。清理时磁盘可用约 87.9 / 209.2 GB；生产 PID 126766 / 164834 保留，两端只余原有内核 GSI QP，无本轮实验进程。历史数据、CPSS 和两份旧 untracked October 文档未动。最终 GitHub 备份 SHA、双端文档提交同步、八个摘要和脚本摘要见 `acceptance/github-backup.json` 与外部 `HANDOFF.md`。

下一串行阶段先剖析接收端 frame allocation、CRC/payload 复制与 tmpfs 新页写入，使用真实路径独立回归约束后再修改。随后独立分析每端接收池分配/注册/预投递与连接复用，以及 CQ close/destroy 生命周期。保持 1 GiB 常规实验、不扩队列、不改默认完整性策略，不重启服务、不以当前盘上约 4 Gbps 为软件上限。
