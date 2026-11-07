# V3 TCP Path Isolation 01：结果与交接

日期：2026-11-08  
状态：两轮有界共享 writer pool 候选均 no-go；三阶段写并发扩张暂停。

## 结论

本任务把原始 TCP 链路、V3 TCP 完整任务和 tmpfs 文件路径分开测量。原始 TCP 四流 20 秒窗口中位数为 89.4689 Gbps；V3 四连接 8 GiB 完整任务只有约 15 Gbps，说明瓶颈在 V3 端到端路径，而不是专线或 TCP 多流能力。

已完成两轮且仅两轮候选验证：

- 候选 1：现有 shared ready-writer pool，read=0/write=1。7 对交错样本均通过 V3 终态、CRC32C、独立 SHA-256、路由/计数器、内存、swap 和清理门。基线中位 15.5016 Gbps，候选中位 14.5302 Gbps，CV 分别 1.849% 和 2.469%，成对中位变化 -6.50%。客户端+发送端 CPU 从 32.91 降到 23.71 CPU 秒/GiB，但吞吐下降，no-go。
- 候选 2：同一 pool，read=0/write=2。7 对交错样本全部通过上述门。基线中位 15.6178 Gbps，候选中位 14.9973 Gbps，CV 分别 1.845% 和 3.045%，成对中位变化 -4.73%，no-go。客户端+发送端 CPU 从 32.29 降到 26.75 CPU 秒/GiB，范围写累计计时从 16.27 降到 8.22 秒，但端到端任务仍变慢。
- 推广门要求两组 CV ≤5%、候选中位至少高于基线 10%、无正确性/资源失败。两轮都只满足稳定性和正确性，均未满足吞吐门。默认 read=0/write=0 保留；不再增加 writer、队列或流水线层。

候选 1 的首个旧命名样本 pool-r4w1-pathiso...-01 保留为失败收据并排除：实际 V3 传输和 CRC/终态完成，但旧实验 helper 在低扰动 profile 下错误要求 pool CPU 计时字段，导致独立 SHA 阶段未执行；它不是性能通过样本。

## 实验固定条件

- R770-1 /home/Su/projects/CPNetFlux 与 R770-2 同步提交 aa4dc3e536a5cd259d423cf1e569517cb19b14bd；核心代码未变，二进制和 CMake 配置两端一致。
- TCP 数据通道绑定 192.168.100.1 ↔ 192.168.100.2、接口 eno17095np0；本轮不是原生 verbs。
- 每个样本使用 R2 /dev/shm 中单个驻留 8 GiB 源和 R1 单个新 tmpfs 输出；data_transport=tcp、4 connections、verified_chunks、CRC32C、commit-sync=none。所有正式候选样本独立 SHA 与源一致。
- 正式样本每组 7 个，输出逐样本删除。正式样本最低 MemAvailable 约 20.03 GiB，实验进程 owned swap=0，实验期间 pswpout 增量=0；生产服务 PID 126766/164834 未触碰。
- 证据根目录：/home/Su/experiments/CPNetFlux/2026-11-07-v3-path-isolation-01。原始 metrics/、logs/、snapshots/、acceptance/ 和 checksums/ 留在 R770-1，不进入 Git。

## 存储诊断

单独的 CPNetFlux storage bench 在 R1 tmpfs 上使用 1 GiB 工作文件、1 MiB buffer、POSIX direct、3 次迭代；完整 stdout 保存为 metrics/storage-bench-tmpfs-1g.txt。聚合结果：写 33.0164 Gbps、读 70.8751 Gbps、改写 60.9391 Gbps。工作文件已删除。这是文件系统诊断，不是 V3 网络吞吐或持久化带宽结论。

## 解释与下一步

四流 raw TCP 接近 90 Gbps，而 V3 任务约 15 Gbps；pool=1/2 可显著降低 CPU 和累计写计时，却因接收路径串行化或交接等待损失端到端服务率。因此“降低 writer 并发即可解决 1 lane 比 4 lanes 快”被否证。三阶段架构和写并发不再扩大，保留当前默认实现和全部负结果。

下一步只进入新的独立处理路径：先保留这个任务的 no-go 边界，再设计一个能把网络供数、应用缓冲和接收提交分离的内存 sink/稳态 payload 测量，或者转入小文件自适应微批研究；必须先写新 scope、单因素假设和验收门，不能把它们当作本轮候选的延伸。RDMA verbs 优化仍暂停，不能据本轮结果宣称 100 Gbps 或生产 readiness。

验证记录：实验驱动修正后，候选 2 重新执行 14 个正式样本；结果统计由 *.verified.json 的 whole_task_gbps 计算，完整 stdout 为 logs/pool_crossover2.stdout.txt。代码/文档提交门和远端 SHA 在项目 Git 历史中记录。
