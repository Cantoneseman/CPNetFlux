# R770 年度性能对比口径：1 GiB

状态：年度报告正文采用的公开对比口径。5 GiB 及更大文件只作为内部研发资料。

## 结论

在 R770 内存文件系统、CPNetFlux V3 TCP 明文和 GridFTP GSI 明文的固定条件下，1 GiB 五对交错测试全部通过。CPNetFlux 中位吞吐为 30.8034 Gbps，GridFTP 为 12.0953 Gbps，CPNetFlux 为 GridFTP 的 2.55 倍。CPNetFlux 与 GridFTP 的吞吐变异系数分别为 4.4938% 和 4.2739%。

该结果支持年度报告中的“当前 1 GiB 内存工作集对比”。它不代表磁盘持久化、跨多跳 1 Tbps 汇聚链路、原生 RDMA data plane 或生产规模 readiness。

## 固定条件

- R770-1 与 R770-2，数据地址 `192.168.100.1` 与 `192.168.100.2`
- CPNetFlux V3，四条 TCP 数据流，V3 worker reuse
- 256 KiB DATA，1 MiB resume chunk，每 16 块 checkpoint，CRC32C verified_chunks
- CPNetFlux inline mmap，private huge within_size，tmpfs，压缩关闭，TLS 关闭，commit-sync none
- GridFTP GSI，clear data，`-fast -p4 -cc4 -tcp-bs1048576 -bs262144 -r -cd`
- 两端网卡 ring 为 RX/TX 1024，显式绑定 RDMA 网卡地址
- 每个方向五对交错运行，所有样本完成 SHA、协议终态、网卡错误、资源清理和版本证据门

## 年度表格

| 方向 | CPNetFlux 中位吞吐 | GridFTP 中位吞吐 | 倍数 |
|---|---:|---:|---:|
| R770-2 → R770-1 | 30.8034 Gbps | 12.0953 Gbps | 2.55x |

反向场景的历史 Stage80 记录为 CPNetFlux 19.741570 Gbps、GridFTP 12.415901 Gbps。它与主方向使用不同场景记录，年度正文应保留原始方向和配置说明，不将不同场景的样本拼成一个聚合中位数。

## 证据边界

5 GiB mmap 对照已完成并保留在 `docs/tasks/2026-11-24-r770-annual-5g-independent-reruns-result.md`，该文件标记为内部研发资料。旧 9 GiB pwrite 结果仅用于解释历史瓶颈，不能与当前 mmap 结果直接比较。

