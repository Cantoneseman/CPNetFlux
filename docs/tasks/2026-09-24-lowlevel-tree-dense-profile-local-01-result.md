# LOWLEVEL-TREE-DENSE-PROFILE-LOCAL-01 结果

日期：2026-09-24（Asia/Shanghai）  
角色：00 总指挥；环境：WSL2 Ubuntu-22.04 本机 loopback  
结论：**12/12 case 成功且 hash 一致；目录阶段可见的主要固定成本是每文件控制准备、数据连接/首 payload 和 interfile idle。该画像只支持下一项目录架构优化候选，不是跨域性能结论。**

## 输入、范围与证据

固定构建来自 telemetry 候选的 Linux 快照。上传客户端 SHA-256 为 `07ecbd9d3af4c9ae787c4f03df7b582d5f59f8a35acd5213d35b3449117009d4`，下载客户端为 `dfdcb494e40a052686c10a1244bc6b026fa7ceddee60af0630917853564bc7a4`。构建参数为 telemetry 候选、POSIX、TLS ON、io_uring OFF；服务端为隔离本机实例，8 个连接上限，checksum/compression/scheduler off，control reuse worker。

数据集固定为 128 个 1 MiB 文件（128 MiB），seed=`CPNetFlux/dense-128x1MiB/v1`，tree SHA-256=`28d45f2f5c91f6026d129dd34ce35c5dcac3f9f2225fec80c5e4416b78e2ece8`。矩阵是 upload/download × file-parallelism 1/8 × 3 次，共 12 个 case。每个 case 使用独立服务根、目标目录和原始 JSONL；不保留 payload。

证据目录：[LOWLEVEL-TREE-DENSE-PROFILE-LOCAL-01](D:/Project/CPNetFlux-evidence/LOWLEVEL-TREE-DENSE-PROFILE-LOCAL-01)。目录含执行脚本、环境摘要、12 份 JSONL、stdout/stderr/server 日志、summary、cases.csv 和 SHA256SUMS。最初一次 fp8 失败是通用 smoke 夹具固定 `--connections 2` 导致服务端拒绝 8 个 worker；该失败日志保留，随后仅在隔离脚本中将服务端连接上限调为 8 并重新完成完整矩阵，未改仓库源码。

## 结果

| 方向 | fp | n | wall 中位数 | wall 范围 | runner goodput 约 |
|---|---:|---:|---:|---:|---:|
| upload | 1 | 3 | 28.632 s | 27.115–28.632 s | 38.26 Mbps |
| upload | 8 | 3 | 28.631 s | 25.943–28.631 s | 39.24 Mbps |
| download | 1 | 3 | 32.101 s | 31.451–32.101 s | 33.90 Mbps |
| download | 8 | 3 | 33.267 s | 31.840–33.267 s | 32.85 Mbps |

所有 case return code=0、source/destination tree hash 一致、file_count=128、logical_bytes=134217728。这个 loopback 结果显示在该固定本机工作负载下，把 fp1 增到 fp8 没有带来端到端 wall 改善；它不能外推 WAN，也不能与 GridFTP 比较。

## 阶段画像（原始 telemetry 复算）

阶段统计保留 count、sum、median、nearest-rank p95、max；并发阶段 sum 不是 wall 分解。

- upload fp1：每文件 control_prepare median 约 0.0117 s、interfile_idle 约 0.0122 s；stream data_connect 约 0.010 s、first_payload 约 0.030 s、payload_io 约 0.020 s。
- upload fp8：control_prepare median 约 0.1175 s、interfile_idle 约 0.1244 s；data_connect 约 0.1160 s、first_payload 约 0.1375 s、payload_io 约 0.2807 s；p95 分别约 0.3304、0.3917、0.3321、0.4164、0.5829 s。
- download fp1：control_prepare median 约 0.0114 s、interfile_idle 约 0.0108 s；data_connect 约 0.0113 s、first_payload 约 0.0356 s、payload_io 约 0.0261 s。
- download fp8：control_prepare median 约 0.12 s、interfile_idle 约 0.13 s；data_connect 约 0.13 s、first_payload 约 0.20 s、payload_io 约 0.28 s；p95 分别约 0.32、0.36、0.39、0.51、0.56 s。

阶段值在高并行下变大，和本机资源/调度竞争一致，但不能仅凭相关性断言具体因果。结果支持优先调查“每文件控制/数据连接/首 payload 的固定往返如何重叠或摊销”；manifest finalize 也需保留为候选，但本轮不能单独证明它是主因。

## 验收、限制与下一步

本任务未修改源码、未 SSH、未触碰云端、未改 Git index、未执行 GridFTP 对照。它未运行 no-range/resume、数据 TLS、故障注入或 overhead gate；telemetry QA-08 结论仍为 PARTIAL，故不能放行代码合入或性能声明。

下一项实现任务应由 01/04 先冻结契约，再由 03 在独立 worktree 实现**有界的目录 lookahead/preconnect 候选**：只允许在当前 worker 完成前为有限数量下一文件准备控制/数据连接，必须保证取消、失败、resume、连接池上限、manifest 状态和旧路径回退；不改 wire frame。先用与本矩阵相同的 128×1 MiB、fp1/fp8、upload/download 做本地 A/B，只有固定 wall、阶段和 hash 门通过后，才准备 05 的深圳—上海准入与匹配实验。
