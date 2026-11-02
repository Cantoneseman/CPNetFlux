# RDMA 字节流接口统一验收

日期：2026-11-02
代码提交：`a1cda1cc50ee9652e8ff69e5baff5d0bd4062472`
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`

## 目标与边界

本任务将“软件侧按字节流/帧读写，底层可选 TCP 或 RDMA”的想法落实到 CPNetFlux V3。这里的 TCP 风格指 CPNetFlux 内部统一的读写抽象，不是把 RDMA伪装成一个可传给任意 POSIX 程序的 TCP 文件描述符，也不是加 TCP 到 RDMA 的网关。控制面仍为 TCP/TLS；RDMA 数据面使用原生 verbs SEND/RECV。

## 修改内容

- `FramedDataSocket` 已提供统一的 `writeFrameBuffered` 入口：TCP 后端发出同样的连续字节，RDMA 后端使用既有 verbs staging/flush 实现。
- V3 树传输发送路径此前按 `DataTransport` 在上层分别调用 TCP 与 RDMA 写方法；现在统一调用 `writeFrameBuffered`，由 socket 后端自己选择实现。
- 增加 TCP 字节流测试，逐字节确认 header、prefix、payload 经统一 buffered API 后与预期序列一致。
- 更新 `docs/roadmaps/rdma.md`，记录接口抽象和 TCP/RDMA 的边界。

## 验收命令与结果

R770-1/R770-2 使用同一提交、同一构建配置：RelWithDebInfo，RDMA/TLS/测试/ISA-L/io_uring 开启，桌面关闭。

1. 两端均成功执行 `cmake --build build --parallel 4`。
2. 两端均运行：
   ```sh
   CPNETFLUX_TEST_RDMA_BIND=192.168.100.1 ctest --test-dir build --output-on-failure -R 'DataTransportTest|FramedDataSocketTest|cpnetflux_rdma_duplex_smoke'
   ```
   R770-2 将 bind 地址设为 `192.168.100.2`。两端均 15/15 通过，包含原生 RDMA duplex hardware smoke。
3. R770-1 运行 `ctest --test-dir build --output-on-failure -R cpnetflux_tree_pending_window_smoke`，1/1 通过。
4. 固定提交后，两端服务器、树上传/下载客户端、RDMA bench、帧 socket 测试程序 SHA-256 完全相同：
   - server: `9fbc1d086ca38b84df0a3d79952dfe328cd4c806a25c4424d5680c79adaae0e2`
   - upload: `de763565e693f0e3a9ca4b3e01b0def58656d0df62f0b88f3f9617029a5ce9c0`
   - download: `106061df84995a5c99d734ba9c73fa7a57a23414316ecbb24ad0cbca55d19d7a`
   - RDMA bench: `6b3f8f2075af9f54274923be9a8a7c7b97fabbcf439240e733c55d7bbe7166e0`
   - framed socket tests: `b04ab77263047c15388d6a103130fd6a29d12fc4b7ccd000229a9f04be47d384`
5. 两台 R770 实际互传：R770-1 显式绑定 `192.168.100.1`，R770-2 服务端绑定 `192.168.100.2`，客户端/服务端均选择 `data-transport=rdma`。V3 上传和下载各 2 个文件，共 8,421,379 bytes；结果均为 `actual_transport=v3`、`data_transport=rdma`、`result=pass`。源、接收端、下载端逐文件 SHA-256 一致。
6. 路由查询均显示经 `eno17095np0` 发往 RDMA 对端。临时服务以 SIGTERM 停止，测试端口已关闭；既有生产监听 `192.168.100.2:2121` 的 PID 164834 仍在。

## 证据位置

不进 Git 的运行数据：`/home/Su/experiments/CPNetFlux/2026-11-02-rdma-stream-facade/`。包括两端前后快照、V3 汇总与事件日志、payload SHA-256。没有将凭据或运行 payload 放入仓库。

## 结论与限制

CPNetFlux 自身已能让 V3 文件传输逻辑使用统一的 socket 风格接口，同时在配置为 RDMA 时由 verbs 承载数据。此改动减少了应用层对 TCP/RDMA 后端的直接分支，并经双机真实互传验证。它没有改变 V3 协议、没有把 RDMA 变成 TCP、没有提供透明 POSIX `socket()` 兼容，也没有证明性能提升；此次互传规模仅用于功能验收。

下一步若回到 RDMA 路线，应依据已记录的持续负载证据继续；该接口统一本身不要求继续做 RDMA 性能调优。
