# V3 RDMA 并发稳定性结果

日期：2026-11-01。任务：`V3-RDMA-STABILITY-01`。
输入：`1fa44e68a3240e20322f0e5537eef9aced5190c3`。
已验收代码：`48fca292e9a2ca5a6235ce7d38e085a0dc3b0b48`。
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`。
状态：统一路线已建立，阶段 A 的并发数据竞争修复及所列用例通过；阶段 B-F 未完成，不能称 RDMA 瓶颈全部排除。

## 实际解决的问题

window>1 的同连接 writer/result-reader 并行时，旧 `readSome()` 会刷新发送暂存槽，和 writer 竞争发送状态。新回归在旧核心上 2.37 秒内检出 `duplex frame ordering/identity mismatch`，其后 shutdown 出现 WR flushed。该复现解释了实际存在的数据竞争，不将原报告的“32 个 spare 不够”或 RNR 猜测当成唯一根因。

修复：读路径不再触碰 send CQ、staged slot 或发送内存；一个 writer 拥有所有发送/flush，新 `flushWrites()` 在请求/响应切换前发布尾部。V3 FILE_END 原本即使用非缓冲写，继续在 writer 线程发布。新旧代码对照未改变 RQ、SQ、MTU、V3 帧、checksum、文件写入和 checkpoint 策略。

新增 duplex bench 和可选硬件 CTest；逐帧改变 payload 并校验身份、offset、长度、全部内容和最后完成标记。未指定本地 RDMA IP 时 CTest 返回 77，明确 skipped。`stats/close` 需 IO 线程结束后调用；一个 reader/一个 writer 可并行，不支持多个 writer 同时操作。

## 路线整合

统一入口：`docs/roadmaps/rdma.md`，用户目录对应 `RDMA路线.md`。原两份 10-31 报告保留为历史，已标明新路线优先。路线修正了错误基线、并发等待归因、帧数/带宽计算、RNR retry=7、接收槽生命周期与 WRITE 持久化假设；撤销固定提速幅度和工期承诺。

顺序为 A 并发稳定性、B 接收资源/建连/持续进度、C CQ/SQ、D 注册 buffer 生命周期、E 应用层、F 有证据才考虑单边 WRITE。AGENTS 和环境文档中已经过时的 TCP-only 说明同步修正；只有显式 RDMA 构建和实际 verbs 证据才能称原生 RDMA，控制面仍单独标注。

## 两端与构建

R770-1/2 均 `/home/Su/projects/CPNetFlux`，先 R770-1 测试并提交/push，再 R770-2 fast-forward 构建。两端 RelWithDebInfo、RDMA/TLS/ISA-L/io_uring/tests ON、desktop OFF；本实验文件后端为默认 POSIX，不使用 io_uring。四项关键二进制 SHA-256 一致：

| 二进制 | SHA-256 |
|---|---|
| server | `d2103806181b989fe3362c3377736610b9ea711f637c3e8b76dfd1e2e9dcda7a` |
| upload | `99836cfb521dfef3559d101f8f6f94eeffc9a061734d9391cdaa72fa08175c68` |
| download | `46e38a8db20a0a64ed2205daf2699a69539d031f54e3140ec995e94e8296ecea` |
| RDMA bench | `9d0e60c5902886500bb0a6f0d70dea983b4728f7e397e1356d40cc27e90cff06` |

## 验证结果

- R770-1：`cmake --build build -j2`；带临时测试 token 和 `CPNETFLUX_TEST_RDMA_BIND=192.168.100.1` 的全量 CTest，482/482 通过，227.38 秒。token 仅存在于测试进程环境，未记录值。
- R770-2：同配置构建成功；RDMA/传输选项/framed socket/pending window 相关 CTest 52/52 通过，8.82 秒；其中硬件 duplex 使用 `.2`，三次通过。本轮没有重复 R770-2 全量测试。
- R770-1 RDMA-disabled：重新配置、构建成功；37 项相关测试通过，1 项硬件测试缺少显式地址按预期 skipped，不记为硬件通过。
- 旧核心配新回归失败；修复并完成 relink 后三次通过。一次在 relink 前的提前测试仍使用旧二进制，保留但不计修复后结果。
- 两节点 full duplex：每方向 3 GiB+17 bytes，49153 帧，全部校验通过；R770-1/2 观察的每方向吞吐分别 11.58/11.43 Gbps，不是每方向 92 Gbps，也不与单向直接比较。

## V3 真实文件互传

R770-1 显式 local bind `192.168.100.1`，对端 `.2`，两端 route 均走 `eno17095np0`/`mlx5_0`。本轮使用独立实验服务 `45220` 和数据端口基点 `45250`；生产 2121 未修改。控制和数据 TLS 都关闭，anonymous，只用于隔离实验网络；本次不对比历史 TLS/token 实验的速度，不声称安全部署验收。

四连接、file parallelism=4、dynamic、tree reuse、pending window=8、4 MiB chunk、64 KiB buffer、CRC32C、compression off、reconnect budget=0。每个样本都检查 summary 为 `actual_transport=v3`、`data_transport=rdma`、恢复次数零，并核对逐文件大小和 SHA-256。

| 用例 | 规模 | Gbps | 完整性 |
|---|---|---:|---|
| dense 上传 1 | 2048 x 64 KiB | 0.3310 | 2048 文件一致，pending 峰值 8 |
| dense 上传 2 | 同上 | 0.3459 | 同上 |
| dense 上传 3 | 同上 | 0.3447 | 同上 |
| mixed 上传 | 2 GiB + 512 x 8 KiB + 空文件，514 文件 | 3.1817 | 全部一致，pending 峰值 8 |
| large-random 上传 | 1 x 4 GiB | 3.2910 | 一致，range handshake 路径，pending 峰值 0 |
| dense 反向下载 | 2048 x 64 KiB | 0.3238 | 2048 文件一致 |

完整性审计只排除与源文件精确对应的 `.cpnetflux.manifest`/`.cpnetflux.download.manifest` 伴随文件；不按模糊后缀忽略用户 payload。首次审计因包含伴随文件失败，修正审计边界后所有真实文件校验通过。

## 新发现：短测不能代表持续 RDMA 性能

同提交、同配置、新建单连接、QD16/RQ2048/MTU1024 的补测：

| 负载 | 服务端 Gbps | elapsed | 接收完成等待 | 传输窗口进程 CPU |
|---|---:|---:|---:|---:|
| 2 GiB | 29.9272 | 0.574054 s | 0.000002189 s | 0.573885 s |
| 3 GiB+17 bytes | 11.1337 | 2.31457 s | 1.45711 s | 0.857198 s |

各一次，完整性通过。接收池约 2.01 GiB，小于该池的短测能掩盖持续供给/反压问题。因此停止将旧 2 GiB/29.7 Gbps 结果解释为“RDMA 接收反压已排除，全部剩余成本是应用 CPU”。本轮修复并发竞争后，该持续吞吐问题仍然存在。

单向 3 GiB 初次服务器等待超过 30 秒 CM accept 默认期限，客户端在监听退出后启动而连接失败；原失败日志单独保留。紧接启动客户端的新端口重试通过，不把前一次调度失败计入有效吞吐。

计时：elapsed 为传输窗口 steady-clock 墙钟；进程 CPU 是 `std::clock` 的该窗口累计 CPU（duplex 含收发两个线程），不是线程墙钟；CQ 等待是嵌套累计墙钟，不能与 elapsed 相加。bench wire_bytes 指应用协议 header+payload，不含完整 RoCE/Ethernet 开销。协商 rnr_retry=7 不表示只重试七次。

## 网络证据与未解决边界

两端 Ethernet netdev errors/dropped 增量为 0。HCA 原始增量保留：R770-1 out_of_buffer +1、rnr_nak_retry_err +2、resp_cqe_error +1680、resp_cqe_flush_error +1664；R770-2 分别 +2、+1、+1882、+1856。期间包含连接建立/传输/关闭，不能将全部增量归因到数据错误或全部解释成正常 teardown，也不能声称 HCA 零错误。下一阶段需分 case、分数据期/关闭期采样；原始 port_data 字段未直接按 IP payload 字节换算。

RQ2048 仍约 2.01 GiB/连接，4 lane 约 8 GiB；MTU1024、全 signaled、SEND/RECV 与复制逻辑保留。本轮没有测试长期 soak/故障注入，也没有实施 SRQ、WRITE、MTU 改动、零拷贝或新的吞吐优化。

## 证据、命令与收尾

两端外部证据根目录：`/home/Su/experiments/CPNetFlux/2026-11-01-rdma-stability/`。
包含 `evidence_driver.py`、before/after snapshots、HCA/interface deltas、构建/CTest 日志、bench 日志、每样本 command/summary/events/time、expected/actual hash 清单和 payload。R770-1/2 的生产服务不变；仅停止了核实为本任务的 server PID 2057802，专用 45220 不再监听。

关键复验命令：

```sh
CPNETFLUX_TEST_RDMA_BIND=192.168.100.1 ctest --test-dir build -R cpnetflux_rdma_duplex_smoke --output-on-failure
# 两节点 bench，先 server 然后及时 client，端口须空闲；单向模式去掉 duplex-。
build/cpnetflux-rdma-socket-bench duplex-server 192.168.100.2 45301 3221225489
build/cpnetflux-rdma-socket-bench duplex-client 192.168.100.2 45301 3221225489 192.168.100.1
```

源码阶段提交 `48fca292...` 已推送到经核实的 `git@github.com:Cantoneseman/CPNetFlux.git` 并回读一致；结果文档修订的最终 SHA 由 Git 记录和最终回读确认。原有两份 worker-state 未跟踪文档完整保留，未提交凭据或 payload。

下一步执行路线 B：先对长传输的 RNR/CQ/progress/消费者停顿与分配/注册/post 开销建立可归因对照，再实现有界接收窗口。采用 2/3/8 GiB、长时间、单/多 lane 对照；不再依靠扩大接收池或只测 2 GiB 宣布道路畅通。
