# V3 原生 RDMA 数据通道

任务编号：`V3-RDMA-TRANSPORT-01`

## 范围

- 在 V3 持久目录传输中加入可选的原生 RDMA 数据通道，采用 Linux `rdma_cm` 建连和 RC verbs SEND/RECV 承载现有 V3 字节流。
- 增加 TCP/RDMA 显式配置、V3 控制面协商、服务端 RDMA 被动监听、客户端 RDMA 连接、结果中的实际数据通道标记。
- TCP 保持默认值；服务配置支持显式选择 RDMA。
- R770-1 是权威开发目录；提交后同步相同提交到 R770-2，分别构建后做真实互传和 SHA-256 核验。

## 非目标

- 不实现零拷贝、RDMA READ/WRITE、GPU Direct、kTLS、TLS-over-RDMA 或物理多网卡链路调度。
- 不更改文件 manifest/checkpoint 语义，不改系统网络配置。
- 本阶段不把 TCP 回退伪装成 RDMA；协商失败或本机未编译 RDMA 时应明确失败。
- 本阶段不宣称生产安全、100G 应用吞吐或性能优于 TCP。

## 验收标准

1. RDMA 支持开启和关闭时均可构建；关闭时显式请求 RDMA 应给出不可用错误。
2. 单元测试覆盖传输名解析、客户端/服务端选项、RDMA 与数据 TLS 冲突、无 RDMA 构建行为。
3. 全量 CTest 和服务配置脚本通过。
4. 两台 R770 构建同一 Git 提交；V3 RDMA 上传及下载成功，SHA-256 一致，协商结果明确记录 `data_transport=rdma`。
5. 本轮提交推送到已核实 GitHub remote，并回读远端 SHA。

## 实现边界

数据平面以 RC QP 的 SEND/RECV 搬运 V3 framed 字节流，每方向使用固定 1 MiB 注册缓冲区，逐消息等待完成。它通过 verbs/RoCE 绕过 TCP 数据 socket，但仍会复制到注册缓冲区，属于首个正确性后端，不是零拷贝优化。RDMA 数据通道暂不支持 data TLS；可继续使用 TCP 控制连接 TLS 和 token 认证，只有在可信 RDMA 网络上才应运行当前数据通道。

## 预计改动文件

- `CMakeLists.txt`
- `include/cpnetflux/core/io/data_transport.h`
- `include/cpnetflux/core/io/rdma_data_socket.h`
- `include/cpnetflux/core/io/framed_data_socket.h`
- `include/cpnetflux/config/tree_transfer_options.h`
- `include/cpnetflux/protocol/control/control_options.h`
- `src/core/io/data_transport.cpp`
- `src/core/io/rdma_data_socket.cpp`
- `src/core/io/framed_data_socket.cpp`
- `src/core/io/tree_transfer_client.cpp`
- `src/core/io/persistent_data_session.cpp`
- `src/core/io/persistent_tree_transfer.cpp`
- `src/core/io/persistent_directory_batch.cpp`
- `src/config/tree_transfer_options.cpp`
- `src/protocol/control/control_options.cpp`
- `src/protocol/control/control_server.cpp`
- `packaging/cpnetflux_service.py`
- 相关单元测试和本任务记录
