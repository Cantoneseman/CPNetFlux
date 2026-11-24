# R770 大文件瓶颈定位实验 01 结果

日期：2026-11-24  
实验输入运行时提交：`34ded615ea72ea2ee805b08075d39d945288f66d`（`34ded61`）  
实验范围：R770-2 → R770-1，V3 TCP 明文、四条数据流、tmpfs、`RX/TX=1024/1024`

## 结论

接收端写入路径是当前可量化的主要软件成本，但它不是唯一瓶颈。现有 inline mmap 在 512 MiB 对照中已经比 pwrite 更快，下一项工程优化应消除“先读入帧缓冲、再 memcpy 到 mmap 窗口”的重复数据搬运。第一候选是让 plain TCP 的 `readv` 直接把 DATA payload 写入已验证的 mmap 目标窗口，并保留现有缓冲路径作为不适用场景的回退。

关闭 pacing 没有形成可接受的性能候选。它触发接收网卡 `rx_missed_errors` 和 `rx_out_of_buffer`，说明当前 pacing 还承担接收端排队保护作用。第一阶段不改 pacing、网卡 ring、IRQ、RSS、MTU、offload 或系统内核参数。

## 当前证据

### mmap 与 pwrite

已有接收成本实验的 512 MiB 对照结果如下。

| 路径 | 中位任务速率 | 接收进程 CPU 中位数 | 物理写 CPU profile |
|---|---:|---:|---:|
| inline mmap | 28.08 Gbps | 0.476 s | 324.9 ms |
| pwrite | 23.45 Gbps | 0.654 s | 490.3 ms |

mmap 的中位任务时间下降 16.5%，接收进程 CPU 下降 27.3%。mmap profile 仍有约 332 ms 的写入阶段 CPU，说明映射窗口内的 `memcpy` 仍然值得消除。

5 GiB inline mmap 单次内部样本为 34.78 Gbps，5,120 个 chunk 全部校验通过。完整 5 GiB 对照中，CPNetFlux mmap 的双向中位数为 35.39 和 36.60 Gbps，GridFTP GSI 明文为 17.26 和 17.35 Gbps。该结果使用当前 mmap 配置。

旧的 9 GiB pwrite 样本为 7.43 和 8.77 Gbps，并伴随较高 CPU 和重传。它使用不同运行时、写入路径和网络状态，不能与当前 5 GiB mmap 结果混合归因。它只能说明旧 pwrite 版本曾存在明显瓶颈。

### pacing 对照

关闭 pacing 的单个 512 MiB mmap 样本协议摘要和 SHA 校验通过，但因 R1 `rx_missed_errors` 与 `rx_out_of_buffer` 各增加 125 被网络门拒收。1.0 GB/s/流候选在传输前被当前运行时契约拒绝，因为生产配置只允许 `off` 或 `1500000000`。因此本轮没有低 pacing 的性能数据，也没有用失败样本改变基线。

### 帧与网络

已有 256 KiB 与 1 MiB DATA 帧对照中，1 MiB 仅慢 0.60%，候选变异系数 9.21%，并产生更多 RX missed。帧大小不是下一轮主优化方向。当前 5 GiB mmap 样本没有新增换出或网卡错误，R770 主机窗口约为 1.88 CPU s/GiB（双端合计），表明 CPU/接收服务路径仍需要继续拆分。

## 下一项实现

实现一个受限的 direct-to-mmap plain TCP 接收路径。

1. 接收并解析小的 frame header 与 CRC 前缀。
2. 对满足映射边界、明文 TCP、未压缩、非 TLS、非原生 RDMA、mmap 写入的 DATA frame，使用 `readv` 将 payload 直接写入目标映射窗口。
3. 在目标映射区域上计算 CRC32C，CRC 通过后才发布 verified chunk、checkpoint 或完成状态。
4. 对 TLS、原生 RDMA、压缩、pwrite、非 mmap、跨映射窗口和不满足安全条件的 frame 继续使用现有缓冲接收路径。

短读、断连、CRC 失败、映射失败、取消和恢复必须返回失败或可重试状态，不能把未校验数据发布为已验证 chunk。恢复时必须覆盖或重新写入受影响范围。不得新增无界队列、线程或改变协议格式。

主要受影响代码预计为 `src/core/io/framed_data_socket.cpp`、`src/core/io/persistent_data_session.cpp`、`src/core/io/persistent_tree_transfer.cpp` 和 `src/storage/mapped_file_window.cpp`，测试覆盖对应的 unit/CMake/CTest 门禁。

## 验收门

- 单测覆盖短读、CRC 失败、映射窗口边界、取消/恢复和不支持模式回退。
- R770-1 构建、提交后同步 R770-2，提交、配置和三份运行时二进制 SHA-256 一致。
- 512 MiB profile 显示 direct path 生效，逻辑数据复制指标为零或与 direct receive 等价，协议摘要、SHA、chunk 校验、资源和接口错误门全部通过。
- 通过 512 MiB 后再做 1 GiB 五对交错 A/B。候选不得回退当前 mmap 基线，也不能引入 RX/OOB 或异常重传增长。
- 5 GiB 只做内部复核，不进入年度报告正文。

## 证据位置

- 接收成本对照：`/home/Su/experiments/CPNetFlux/2026-11-24-r770-receive-cost-01`
- pwrite 对照及停止证据：`/home/Su/experiments/CPNetFlux/2026-11-24-r770-bottleneck-pwrite-34ded61`
- pacing 关闭停止证据：`/home/Su/experiments/CPNetFlux/2026-11-24-r770-bottleneck-pacing-off-34ded61`
- 低 pacing 契约拒绝：`/home/Su/experiments/CPNetFlux/2026-11-24-r770-bottleneck-pacing-1g-34ded61`

