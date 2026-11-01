# V3 RDMA 并发稳定性

日期：2026-11-01。任务：`V3-RDMA-STABILITY-01`。
输入提交：`1fa44e68a3240e20322f0e5537eef9aced5190c3`。
执行：当前总指挥，在 R770-1 `/home/Su/projects/CPNetFlux` 串行开发；R770-2 同路径同步构建。
统一路线：`docs/roadmaps/rdma.md`（对应用户路线目录的 `RDMA路线.md`）。

## 范围、非目标与文件

处理 window>1 时同连接 writer/result-reader 的发送暂存状态竞态；补同连接 duplex 硬件回归；澄清原报告中未经证实的归因和过时 TCP-only 环境描述。

不改变 RQ2048、MTU、帧大小、checkpoint 或 manifest，不引入 SRQ、credit 协议、选择性 signaled、RDMA WRITE 或零拷贝。不改系统网络，不替换或停止现有 2121 服务。

受影响文件：`AGENTS.md`、`docs/coordination/ENVIRONMENT.md`、`docs/roadmaps/rdma.md`、本任务文件、`rdma_data_socket.{h,cpp}`、`tools/perf/rdma_socket_bench.cpp`、`tools/test/run_cpnetflux_rdma_duplex_smoke.py`、`CMakeLists.txt`。

## 设计与根因

`sendPersistentTree` 在 pending window>1 时，用 writer 继续发送下一文件，同时 result-reader 接收上一个文件结果。旧 `RdmaDataSocket::readSome` 隐式调用 `flushStagedSend`，因而两个线程同时操作发送暂存槽、在途计数及 send CQ，违反单 writer 所有权。

修改后读路径只处理 RQ/receive CQ。发送线程负责所有 write/flush；新增 `flushWrites()` 用于请求/响应切换前发布不足整槽的暂存尾部。非缓冲 `writeAll`/`writeFrame` 仍先刷新暂存数据，V3 FILE_END 已通过该路径发布。保持一个 writer 与一个 reader 可并发；不支持多个并发 writer 或多个 reader；stats/close/析构需在 IO 线程退出后调用。

这不是用更长 timeout 或 window=1 规避问题，也没有扩大接收池。原先“缺少立即补接收槽导致 RNR”的报告推断未被本任务直接采纳。

## 行为回归

duplex bench 两端同时执行一个 writer 和一个 reader。writer 发 64 KiB 帧并逐帧改变 payload；reader 检查 header 身份、offset、长度、全部 payload 和结尾完成标记。默认每方向 `64 MiB + 17 bytes`，覆盖跨槽和非整帧尾部；重复三次。

`CPNETFLUX_TEST_RDMA_BIND` 指定本机 RDMA IPv4 地址时，CTest 自动运行同主机 HCA 回归；未设置时返回 77，CTest 标为 skipped，不冒充通过。两节点 duplex 测试另用显式 peer/local bind 命令运行。

旧核心实现配新回归在 2.37 秒内失败：`duplex frame ordering/identity mismatch`，随后退出产生 `Work Request Flushed Error`。完成新构建后，同回归三次通过（CTest 6.73 秒）。中途一次提前于最终 relink 的运行仍使用旧二进制，不能当作修复后结果。

## 验收与证据

1. R770-1 全量 CTest；临时测试 token 只注入进程环境，不记录值。`CPNETFLUX_TEST_RDMA_BIND=192.168.100.1` 明确启用硬件回归。
2. RDMA-disabled 构建及相关数据传输/选项测试；默认硬件回归缺少地址时必须 skipped。
3. R770-1 先提交固定代码，再同步 R770-2，同配置构建并核对 server/upload/download/bench 的二进制 SHA-256。
4. 经 `192.168.100.1/2` 双向 duplex；记录路由、网卡/HCA 计数前后值。
5. 四通道 window=8 dense 2048 文件三次成功、逐文件 SHA-256 一致、恢复次数零；补 mixed/大文件边界，不以累计等待推算单帧 CPU。
6. GitHub 推送并回读远端 SHA；保留原有两份未跟踪任务文档。

证据目录：两端 `/home/Su/experiments/CPNetFlux/2026-11-01-rdma-stability/`。大 payload、完整运行日志、hash 清单留在实验目录，不进入 Git。命令和结果在本任务结果文件补录；未满足的项保持未验收。

## 下一步

阶段 A 验收后进入路线 B：先分段测量分配、MR 注册和接收 post，再设计有界可配置接收窗口；用超过接收池容量的大文件验证稳态，不将 2 GiB 短测当成反压彻底排除的证据。SRQ/独立 progress/credit 按实际需要选择。
