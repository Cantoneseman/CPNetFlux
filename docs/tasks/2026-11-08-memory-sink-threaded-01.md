# V3 Memory Sink Threaded 01：单事件循环候选

日期：2026-11-08  
状态：scope ready；尚未修改源码或开始候选实验。

## 假设

现有 memory sink 使用单个非阻塞 epoll 循环处理全部连接；在 4 connections 下只有约 40.564 Gbps，而同一路径 raw TCP 四流约 89.469 Gbps。假设每连接一个阻塞 I/O worker 能减少 epoll 批处理、共享缓冲和连接状态分支的开销，使 sink 稳态吞吐提高，同时不改变 V3 文件路径。

## 范围

- 只修改 cpnetflux-server/client 的 memory sink 诊断路径。
- 新增显式 `--io-mode epoll|threaded`；默认仍为 `epoll`，旧命令行为保持。
- `threaded` 采用每连接一个 worker、每 worker 独立 1 MiB buffer；server 按连接 EOF 收取，client 按既有总量分片发送。
- 不修改 V3 默认文件执行器、三阶段 writer、RDMA、TLS、内核参数、MTU、NUMA、socket buffer 或生产服务。
- 不提交实验日志、payload 或运行时脚本；证据留在 `/home/Su/experiments/CPNetFlux/2026-11-08-memory-sink-threaded-01`。

## 固定实验

- 两端必须先固定同一提交、同一 CMake 配置并核对 `cpnetflux-server`/`cpnetflux-client` SHA-256。
- TCP 服务端绑定 192.168.100.1，客户端连接该地址，接口 eno17095np0；明确记录 route 和端口计数器。
- 总 payload 8 GiB、buffer-size=1 MiB、connections=4；epoll 对照和 threaded 候选各 7 次交错运行。
- 记录 stdout、wall/user/sys/RSS、bytes、route、接口计数器、MemAvailable、进程 VmSwap、pswpout；每次结束清理进程和临时数据。

## 验收门

- 每组 7 次，client/server rc=0，发送/接收字节精确为 8 GiB。
- 两组各自 CV ≤5%；候选中位吞吐至少比 epoll 中位高 10% 才算通过。
- 运行期间两端 MemAvailable ≥12 GiB，owned swap=0，pswpout 增量=0；路由和 eno17095np0 证据完整。
- 候选不改变 epoll 默认；若候选无收益、CV 超门或引入正确性/资源问题，则 no-go，记录结果并停止继续扩大 sink/三阶段并发。

## 验收命令

```bash
cmake --build build -j2 --target cpnetflux-server cpnetflux-client cpnetflux_unit_tests
ctest --test-dir build --output-on-failure -R 'sink|file_transfer_smoke'
sha256sum build/cpnetflux-server build/cpnetflux-client
```

实验只在构建、CTest、双端 SHA-256 和固定配置一致后开始；不使用 dirty 树互传。
