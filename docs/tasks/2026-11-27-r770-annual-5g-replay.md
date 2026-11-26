# R770 5 GiB 对比矩阵重跑

## 范围

以当前固定 CPNetFlux 提交，在 R770-2 到 R770-1 完成与 2026-11-24 结果相同的内部 5 GiB 对比矩阵。矩阵由 15 个安全设置样本、正向 5 对和反向 5 对组成，共 35 个正式样本。安全组按 GridFTP anonymous、GridFTP GSI clear、GridFTP GSI private、CPNetFlux token clear、CPNetFlux token 加控制/数据 TLS 的顺序分三轮交错。双向主对比各以五对 GridFTP GSI clear 与 CPNetFlux 明文交错。

CPNetFlux 主对比固定 V3/TCP 四条数据流、接收端 inline mmap、256 KiB DATA、请求 1 MiB 接收缓冲、每流 1.5 GB/s pacing、CRC32C、1 MiB chunk 和每 16 块 checkpoint。安全组 CPNetFlux clear/TLS 沿用报告中的 TLS-compatible pwrite、65,532-byte DATA 和无 application pacing。GridFTP 使用报告中的 GSI 认证、明文数据、MODE E、1 MiB TCP buffer 与 256 KiB block。双端 RX/TX ring 保持 1024/1024。

输入提交为 `ba59f794ad5d4dd75121746ded59d2db584faa63`。新 run ID 为 security `2026-11-27-r770-annual-5g-security-03`、正向 `2026-11-27-r770-annual-5g-main-forward-03`、反向 `2026-11-27-r770-annual-5g-main-reverse-03`。历史 run 目录不复用。

## 非目标

不修改性能门槛或拒收语义，不修改 CPNetFlux 数据路径，不更改 GridFTP 配置、网卡、路由、内核网络参数或 ring，不拼接历史样本，不将内部 5 GiB 结果写入年度报告。

## 验收标准

- 同一固定提交在 R770-1/R770-2 构建通过，server/upload/download 二进制 SHA-256 一致。
- 安全组 15 项、正向主对比 10 项、反向主对比 10 项均有原始终态、模式、完整性、流数、资源和网络证据。失败样本按既有硬门保留并单独标记，不统计为合格样本。
- 每阶段清理临时服务、凭据与 payload，postflight 身份和 NIC 错误增量通过。
- 在本记录追加逐样本结果、汇总统计、证据目录、固定提交 SHA 和未完成项。

## 受影响文件

- `tools/perf/r770_annual_optimized_node.py`
- `tools/perf/r770_annual_optimized_runner.py`
- `tools/perf/test_r770_annual_optimized_runner.py`
- `docs/tasks/2026-11-27-r770-annual-5g-replay.md`

## 当前状态

本记录建立时，R770-1 与 R770-2 均位于输入提交。新增 run 目录均不存在。runner 原有样本顺序、实验配置和拒收门槛保持原样，本次只补齐三个新 5 GiB run ID 及正向阶段映射。

## 2026-11-27 安全组独立重跑

首次 run `2026-11-27-r770-annual-5g-security-03` 的 7 个准入 smoke 均通过。第一个正式 GridFTP GSI clear 5 GiB 样本按原资源门拒收，R770-1 的 `pswpout` 增加 31，`memory.full` PSI 增加 33,970 微秒，`/sys/fs/cgroup/user.slice` 的 `memory.swap.current` 增加 126,976 字节。OOM 与 NIC/HCA 错误增量均为零。拒收结果和双端 socket/resource 证据保留，未计吞吐。临时 payload、凭据和 listener 均已清理，postflight 身份匹配；随后 6 秒空载门通过。

不拼接 `-03` 样本。为独立重跑完整 15 项安全矩阵，新的 security run ID 为 `2026-11-27-r770-annual-5g-security-04`。主对比 forward/reverse run ID 保持 `2026-11-27-r770-annual-5g-main-forward-03` 与 `2026-11-27-r770-annual-5g-main-reverse-03`。门槛、样本顺序、配置和网卡状态不变。

## 2026-11-27 矩阵续采

正向 `main-forward-03` 也在首个 5 GiB GridFTP 样本后被 R770-1 host full-memory PSI 门拒收。完整性 SHA、双端 GridFTP 226 回执及四数据流 socket 观测通过，NIC/OOB 无增量。与 security-03/04 一样，失败样本保留且不计合格吞吐。

按用户要求，后续矩阵将采完全部计划行，逐行保留现有 accepted/rejected 判定；门槛值和单样本拒收语义不变。security 15 行齐全并核验固定提交后进入 reverse，即使有拒收行也不把它们计为合格。新的 run ID 为 security `2026-11-27-r770-annual-5g-security-05`、forward `2026-11-27-r770-annual-5g-main-forward-04`、reverse `2026-11-27-r770-annual-5g-main-reverse-03`。

反向 `main-reverse-03` 的准入 smoke 中 GridFTP 通过，随后 CPNetFlux 128 MiB smoke 按硬门拒收，R1 `rx_missed_errors/rx_out_of_buffer` 增加 133，R2 发送 socket 重传 133 次、20,272 与 172,312 字节分布于两条 lane。阶段未运行正式反向样本。双端临时数据、凭据和 listener 清理成功，postflight 身份匹配，6 秒空载错误计数稳定。

使用独立 reverse `2026-11-27-r770-annual-5g-main-reverse-04` 重新执行准入和正式 10 项。`-03` 失败证据保留且不与新 run 合并。
