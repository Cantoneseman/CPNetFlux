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

`security-05` 与 `main-forward-04` 的 5 GiB 矩阵已完整采集，但它们使用提交 `a85c684`。reverse `-03` 在该提交下 smoke 拒收；尝试启动 reverse `-04` 时，提交校验检测到 runner 已更新至 `3571f95`，在创建实验目录和启动传输前停止。因此上述三组不拼接。server/upload/download 二进制哈希在两提交之间保持不变。

为使本轮完整对比落在单一固定提交 `3571f95`，新的正式 run IDs 为 security `2026-11-27-r770-annual-5g-security-06`、forward `2026-11-27-r770-annual-5g-main-forward-05`、reverse `2026-11-27-r770-annual-5g-main-reverse-04`。较早尝试仅作为拒收/诊断证据，正式汇总使用这三个新 ID。

## 2026-11-27 正式二进制矩阵续跑

`security-06` 与 `main-forward-05` 均基于 CPNetFlux 二进制提交 `02f65c39d806708211f325b4420c10364fe66848`。security 15 行、forward 10 行均已完整采集，所有正式样本因 R1 host full-memory PSI 被拒收；个别 CPNetFlux 行另有 R1 RX OOB 与 R2 socket 重传。失败行不纳入合格吞吐统计，双端清理与身份核验通过。

`main-reverse-04` 两端 128 MiB smoke 通过，但 R1 在 5 GiB source seed 过程中触发 host full-memory PSI，种子只生成 5,167,382,528 字节，随后因字节数与预期 5,368,709,120 不符而停止，没有正式反向样本。source 的违规记录、postflight 和清理结果保留。反向完整重跑使用新 ID `2026-11-27-r770-annual-5g-main-reverse-05`，二进制提交仍为 `02f65c39d806708211f325b4420c10364fe66848`。本次 runner 版本只新增 run ID 映射、测试和记录，R1/R2 均同步同一源码提交，传输二进制哈希不变。

run `security-06`、`main-forward-05` 與 `main-reverse-05` 的尝试不能组成固定提交矩阵：R1/R2 在 `d5a5569` 更新了 run ID 映射后，runner 的 build receipt 强制校验当前 HEAD 与请求提交一致，阻止了把 `02f65c39` 作为提交号继续运行。`reverse-05` 在 preflight receipt 阶段停止，没有创建正式实验目录或传输样本。

映射提交后将固定本轮源码版本，再启动三阶段新 run ID。security `2026-11-27-r770-annual-5g-security-07`、forward `2026-11-27-r770-annual-5g-main-forward-06`、reverse `2026-11-27-r770-annual-5g-main-reverse-05`。security-06 与 forward-05 保留为旧提交结果，不纳入该固定提交矩阵。
## 2026-11-27 当前代码完整重测结论

本轮按历史结果文件 `2026-11-24-r770-annual-5g-independent-reruns-result.md` 的矩阵配置复核。计划为安全组 15 项、R770-2→R770-1 正向主对比 10 项、R770-1→R770-2 反向主对比 10 项。主对比使用 CPNetFlux V3/TCP 四流、inline mmap、256 KiB DATA、请求 1 MiB 接收缓冲、每流 1.5 GB/s pacing、CRC32C、1 MiB chunk、每 16 块 checkpoint；GridFTP 使用 GSI clear、MODE E、四流、1 MiB TCP buffer、256 KiB block。安全组 CPNetFlux clear/TLS 使用 pwrite、65,532-byte DATA、无 application pacing。双端 RX/TX ring 全程为 1024/1024，未修改网卡或系统网络参数。

固定源码提交为 `431db9eeb896931e25411c130e5aca3b854e7b5a`。R1/R2 同提交构建通过，runner 回归测试各 28 项通过，`cmake --build build -j2` 两端均无待构建目标。三份运行二进制两端 SHA-256 一致，server `377edba4690acc7a96d95581f45d6662f2dc6bf970cf8290c14b159fb31f8e88`，upload `361cd581e83cfaf70b6e6fed122f181e5136afd2d98124a60ed6af64b9a1faea`，download `b2b854a16ca72ce19272b1231216df5a545ec293066f5720a6f6dea2ff4be117`。

| 阶段 | run ID | 正式传输样本 | accepted | rejected |
| --- | --- | ---: | ---: | ---: |
| 安全矩阵 | `2026-11-27-r770-annual-5g-security-08` | 15 | 0 | 15 |
| 正向主对比 | `2026-11-27-r770-annual-5g-main-forward-06` | 10 | 0 | 10 |
| 反向主对比 | reverse-06、reverse-09 至 reverse-15 | 0 | 0 | 0 |

安全组和正向组每一正式行都触发 R1 host full-memory PSI。个别安全模式另有 R2 TCP 重传，TLS 样本还因观测到 5 条数据 socket 被拒收。正向有一条 CPNetFlux 样本另有 R1 RX OOB +155 与 R2 TCP retrans +155。正向 10 条客户端命令均以 return code 0 结束，但全部被原门拒收，不作为有效成绩。

仅供诊断的正向客户端耗时中位数为 GridFTP 2.480 秒、CPNetFlux 1.164 秒，按 5 GiB 换算约 17.32 与 36.89 Gbps。安全组客户端耗时中位数为 GridFTP anonymous 2.371 秒、GridFTP GSI clear 2.511 秒、GridFTP GSI private 3.696 秒、CPNetFlux token clear 6.347 秒、CPNetFlux token + TLS 6.151 秒。这些值来自被拒收样本，不能作为正式性能结论或写入年度报告。

反向 8 次源文件生成均因 R1 host full-memory PSI 在完整 5 GiB 前中止，正式反向样本为 0。证据显示已写入 4,831,838,208 至 5,301,600,256 字节。reverse-07 与 reverse-08 是临时调用器首次尝试，因漏设入口所需 `umask 077` 导致 GSI 证书权限失败，没有启动传输；后续反向尝试显式设置该 umask。8 次 PSI 停止和 2 次凭据失败均保留独立目录，不拼接样本。所有结束的阶段和尝试都清理了临时 payload、凭据与 listener，双端身份复核通过。

因此本轮未形成完整 35 项合格比较，现有数据不能替代历史报告或进入年度报告。拒收行不与后续样本拼接。证据保存在 R1 `/home/Su/experiments/CPNetFlux/` 下 security-08、main-forward-06 和各 reverse 目录。
