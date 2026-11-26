# R770 年度对比续跑结果

日期：2026-11-26

## 结论

本轮年度对比未完成。新的 security run 在 8 MiB 认证/数据模式准入阶段停止，未进入 1 GiB 正式样本，也未运行正向或反向主矩阵。拒收原因是 R1 的主机级 TCP 重传计数在样本窗口增加 2。该样本 payload 完整且 SHA-256 校验通过，但按传输窗口硬门拒收，不计性能结果。没有新的年度吞吐成绩，也没有生产传输逻辑优化。

## Runner 门禁修正

空载 `idle()` 不再因主机级 `Tcp.RetransSegs` 增量拒收。空载仍检查直连接口/HCA 错误、内存、swap、PSI 和 cgroup 稳定性。传输 `sample()` 保留双端 TCP 重传增量硬拒收。

新增 runner 单测覆盖空载 retrans +1 继续检查其他门禁、空载接口 missed +1 拒收、传输 retrans +1 拒收。R1/R2 的 py_compile 和 runner 单测均通过，单测 11/11。

## 样本与清理

- run ID：`2026-11-26-r770-annual-comfort-security-1g-02`
- 输入提交：`499758e8906fcfb699f3850d581dcbcf016d3b90`
- 该 security 矩阵原定 R2→R1、1 GiB（1,073,741,824 字节）、GridFTP GSI 明文/私密及匿名明文、CPNetFlux token 明文与控制/数据 TLS 五种模式各 3 个正式样本。CPNetFlux 采用 V3/TCP、四连接、inline mmap 接收、direct receive 关闭、256 KiB DATA、请求 1 MiB TCP 接收缓冲、每 lane 1.5 GB/s pacing、CRC32C、1 MiB chunk、每 16 块 checkpoint、关闭压缩。GridFTP 使用固定年度合同中的 GSI profile。双端 RX/TX ring 保持 1024/1024。
- 先前 `smoke-r2-01-grid`：8 MiB，准入接受。该样本仅用于 smoke，不计正式结果。
- `smoke-r2-02-private`：8 MiB，目标 SHA-256 `f81f74d8573dd6dfd8f543981a80b6eec2f3adae51db706a3b5ae4f62c44fe83`，SHA 通过；R1 `Tcp.RetransSegs +2`，R2 为 0；双端 RX missed/OOB、丢包和 CRC 增量为 0；换出增量为 0。该计数是主机级，无法归属到具体数据连接，因此按传输样本硬门拒收，但不能据此认定数据通道本身发生重传。security 矩阵立即停止。
- postflight：双端 payload 和实验凭据均已移除，临时监听为空，接口/服务身份匹配；6 秒空载窗口 RX missed/OOB 与换出增量为 0。常驻 2121 服务仍在。
- 前一次失败证据未改写：`/home/Su/experiments/CPNetFlux/2026-11-26-r770-annual-preflight-abort-01/`

## 构建与测试

R1/R2 同为 RelWithDebInfo，RDMA/TLS 和测试均启用；代码提交与 GitHub 分支回读均为 `499758e8906fcfb699f3850d581dcbcf016d3b90`。server、upload client、download client 的双端 SHA-256 分别为 `3c3e7ac962cc272cfea6087a2ce9153ed36aac8299e434ecc316e1fa3632b777`、`383b501a75ec2395cc8a1c73a90d69533e67be9d09e1d19455afb1ea027f3d83`、`c5660979b9ba167276754ae0dea44b6ad817d5f51f9b08444199aeb22d1f9bce`。

R1 全量 CTest 首轮 865/868 通过。缺少测试专用 token 的两项和 TLS pacing timerfd 断言复核后均通过。R2 全量 CTest 在注入临时测试 token 后 867/868 通过，唯一 TLS pacing timerfd 断言单项复核通过。两端均有 13 项跳过测试，包含 RDMA-only/codec 条件测试。年度传输未因 CTest 首轮失败而放宽门禁。

## 证据

完整 JSON、观察窗口和清理记录位于 `/home/Su/experiments/CPNetFlux/2026-11-26-r770-annual-comfort-security-1g-02/`。具体拒收记录为 `smoke-r2-02-private.json`，收尾记录为 `postflight.json`。本轮 forward/reverse run 目录未创建。

## 受影响文件

`tools/perf/r770_annual_optimized_runner.py`、`tools/perf/test_r770_annual_optimized_runner.py`、`tools/perf/r770_annual_optimized_node.py`、`docs/tasks/2026-11-25-r770-annual-comfort-1g.md`。
