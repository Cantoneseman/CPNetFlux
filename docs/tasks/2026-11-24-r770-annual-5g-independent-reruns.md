# R770 优化年度对比 5 GiB 独立续测

输入为已回读的 cafe2ef120748ae6faae27268abb4edf7f704767。原证据 2026-11-24-r770-annual-optimized-5g-01 保留。正向五对沿用原有效结果。原反向四对和 R2 漏包、缓冲不足各 +1332 的拒收样本留在原窗口。

先跑独立安全组 2026-11-24-r770-annual-5g-security-02，重发短期实验 CA、真实 GSI proxy 和受限授权。使用 8 MiB 认证和功能试传校准证据，然后在 R770-2 → R770-1 用同一份 5 GiB（5,368,709,120 字节）不可压缩文件完成三个交错块。每块各包含 GridFTP 匿名明文、GSI 明文、GSI 私密数据、CPNetFlux token 明文及 token 加控制/数据 TLS。后两个模式用共同的四流 POSIX pwrite、65,532 字节 DATA、CRC32C、无压缩和无应用限速。GridFTP 保留四流、MODE E、1 MiB TCP 缓冲请求和 256 KiB block。

安全正式 15/15 且临时服务、凭据和负载清理后，按至少 6 秒空载窗验证原服务、网卡、路由、资源和累计计数。条件通过才启动 2026-11-24-r770-annual-5g-reverse-02，在 R770-1 → R770-2 用同一 SHA 的 5 GiB 文件从头完成五个交错配对。CPNetFlux 采用已准入的四流 V3 明文下载、inline mmap、256 KiB DATA、1 MiB 缓冲请求、每流 1.5 GB/s 限速和原校验与提交设置。GridFTP 用真实 GSI 明文 profile。新旧反向窗口分别统计。

任何资源、网络或完整性拒收立即停止当前矩阵，保留原计数和样本，保持固定文件大小、路由和 RX/TX 1024/1024。验收每样本的两端控制/数据模式、SHA-256、精确字节数、实际四流、12 GiB 可用内存下限以及零新增 swap、full 内存压力、网卡/HCA 错误。两端运行相同提交、构建配置和二进制 SHA-256。任务耗时为客户端启动至退出，外部校验单列。每次只有一条文件传输，临时目录和凭据按 run 隔离。

非目标为产品改动、NIC 或系统设置变更及其他尺寸。受影响文件仅为 tools/perf/r770_annual_optimized_node.py、tools/perf/r770_annual_optimized_runner.py 与本阶段的合同、结果及索引。R1 唯一 Git 写入者，R2 同步提交并构建验收。
