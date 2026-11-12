# V3 原生 RDMA 数据通道开发结果

日期：2026-10-30  
任务：`V3-RDMA-TRANSPORT-01`  
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`  
最终提交：`69ade2c7d4f21a912880c304a09780215f250377`

## 本轮解决的问题

V3 原先只有 TCP 数据通道，无法在 R770 两台服务器上通过 RDMA/RoCE 网卡直接传输。本轮给 V3 增加了可显式选择的原生 RDMA 数据通道：控制面协商 `data_transport=rdma`，数据面通过 `rdma_cm` 建立 RC QP，以 verbs SEND/RECV 搬运既有 V3 字节流。TCP 仍是默认值；RDMA 不可用或协商失败时明确报错，不静默退回 TCP。

第一轮真实上传发现初始实现每个 QP 只投递一个接收缓冲区。32 MiB 文件耗时 36.33 秒，吞吐 0.00739 Gbit/s；摘要显示发送线程几乎全程在等数据写入完成。随后把接收侧改成固定 16 槽、每槽 1 MiB 的预投递队列。以匿名控制通道做同样的 32 MiB 单连接上传，耗时降到 0.1128 秒、2.38 Gbit/s，约为首轮的 322 倍。没有采集到硬件 RNR 计数器，因此“RNR 重试”仍是可能原因而非已证实结论。

## 双机验证

- R770-1：`192.168.100.1`，客户端，HCA `mlx5_0`；R770-2：`192.168.100.2`，临时 V3 服务端，HCA `mlx5_0`。两端均为活动 Ethernet/RoCE 端口。
- 两端源码提交均为 `69ade2c7d4f21a912880c304a09780215f250377`，RDMA-enabled CMake 构建成功；服务端和上传、下载客户端二进制 SHA-256 在两端一致。
- 使用一个 32 MiB 文件、单连接、单文件并行度。控制连接启用 TLS 和 token 认证；数据连接使用 RDMA，`data_tls_verified=false`。
- 上传：33,554,432 bytes，0.170283 秒，1.576 Gbit/s，结果 `pass`。
- 下载：33,554,432 bytes，0.164844 秒，1.628 Gbit/s，结果 `pass`。
- 本地源文件、R770-2 上传结果和 R770-1 下载结果 SHA-256 均为 `09fe47bd7245645986941a1da5d12f2701a13d63fa5e3434b1e4b13d92f1a3ff`。
- V3 JSON 摘要两方向都记录 `data_transport=rdma`、`actual_mode=persistent_tree`、`control_tls_verified=true`。因此本次不是仅跑 `ib_write_bw`，而是 CPNetFlux V3 实际走 RDMA 完成了双向传输。

测试文件、摘要和临时服务日志留在两台服务器的 `/tmp/cpnetflux-rdma-v3-20261030/`，payload 未放入仓库。临时服务已停止，复制到 R770-1 的临时 token 文件已删除。

## 验证门禁

- R770-1：RDMA-enabled 全量 CTest，480/480 通过，用时 221.16 秒。
- R770-1：RDMA-disabled 配置重新构建成功；`DataTransportTest` 2/2 通过。
- R770-2：同步到最终提交后 RDMA-enabled 构建成功；`DataTransportTest` 2/2 通过。
- `git -c core.whitespace=cr-at-eol diff --check` 通过。
- GitHub remote `git@github.com:Cantoneseman/CPNetFlux.git` 推送成功；远端分支 SHA 回读为 `69ade2c7d4f21a912880c304a09780215f250377`。

## 性能与边界

硬件 `ib_write_bw -d mlx5_0 -b -F` 基准曾测得双向平均 18,858.33 MiB/s；这是 RDMA_Write 微基准，不能和 CPNetFlux 的 SEND/RECV、文件 I/O、校验及 checkpoint 应用吞吐直接比较。当前 1.58-1.63 Gbit/s 是正确性阶段的单连接应用结果，远未达到 100G 链路能力。

当前每条 RDMA 连接约注册 17 MiB 内存（16 MiB 接收槽加 1 MiB 发送缓冲），连接数增加时内存按连接数增长。发送侧仍一次只发一条消息并同步等待 CQ；CQ 使用轮询。RDMA 数据面不加密，只有控制 TLS 和令牌认证已验证。未测试多连接扩展、双向同时并发、长时间稳定性、断链恢复、不同物理链路，亦未实现 RDMA READ/WRITE 或零拷贝。

## 下一步

先用 perftest/RDMA 计数器和 CPNetFlux 分阶段计时确认应用吞吐差距来源，尤其测量每次 SEND 的完成等待、RNR 计数和文件写入/校验成本。随后以可控的发送窗口、完成批处理和接收槽大小做单变量对照，并与同配置 TCP 跑 32/256/1024 MiB、1/2/4 连接测试。数据面加密与可信网络部署边界需要单独形成安全设计；在这之前不应把当前无 data TLS 的配置用于不可信网络。
