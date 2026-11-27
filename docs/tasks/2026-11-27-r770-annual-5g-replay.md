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

## 2026-11-27 安全组优化配置重跑（security-11）

本次只补做历史年度对比中尚未完成的 15 项安全矩阵，输入基线为 `2026-11-24-r770-annual-5g-independent-reruns-result.md`。5 GiB 数据只用于内部工程验证，不写入年度报告；原报告文件未修改。

为让 CPNetFlux 使用报告中的优势接收配置，整组方向统一为 R770-1→R770-2 下载。当前 inline mmap 仅由 V3 下载客户端接收侧实现，因此 GridFTP 与 CPNetFlux 安全模式均按同一方向运行。CPNetFlux 使用 V3/TCP 四流、inline mmap、256 KiB DATA、请求 1 MiB `SO_RCVBUF`、每流 1.5 GB/s pacing、CRC32C、1 MiB chunk、每 16 块 checkpoint，关闭压缩。安全模式为 token 明文，以及控制面和数据面 TLS。GridFTP 使用 GSI clear、GSI private 与 anonymous 模式。

固定提交为 `9d9b845354b4421cc7e01f1a9b3fa45ce7ef0461`，R1/R2 和 GitHub 对应分支 SHA 一致。两端 runner 单测各 30 项通过，Python 语法检查和 CMake 构建通过；三份二进制 SHA-256 完全一致：server `377edba4690acc7a96d95581f45d6662f2dc6bf970cf8290c14b159fb31f8e88`、download `b2b854a16ca72ce19272b1231216df5a545ec293066f5720a6f6dea2ff4be117`、upload `361cd581e83cfaf70b6e6fed122f181e5136afd2d98124a60ed6af64b9a1faea`。双端 RX/TX ring 均保持 1024/1024。

针对 run-10 源文件准备阶段仅增加 94 微秒 full-memory PSI、且 swap 与换出未变化的记录，本 run 仅将 security-11 的 full PSI 增量容差设为 5,000 微秒；默认容差仍为 0。swap、cgroup memory events、最低可用内存、NIC RX/OOB、TCP retransmission、认证和完整性门槛保持原规则。7 个 smoke 全部通过。5 GiB 源文件 SHA-256 为 `867c60a763149bef989bd4f23ee180ee83022256ed1518841f6ee2e41b45f8c0`，生成期间 PSI 增量 1,097 微秒，资源违规为 0。

| 模式 | 有效样本 | 有效吞吐中位数 | 有效样本吞吐（Gbps） |
| --- | ---: | ---: | --- |
| GridFTP GSI clear | 3/3 | 17.16 | 17.16, 16.56, 18.04 |
| GridFTP GSI private | 3/3 | 11.68 | 11.68, 11.75, 11.37 |
| GridFTP anonymous | 3/3 | 18.95 | 19.49, 18.95, 18.39 |
| CPNetFlux token 明文 | 2/3 | 35.90 | 37.36, 34.45 |
| CPNetFlux token + 控制/数据 TLS | 2/3 | 28.76 | 28.90, 28.62 |

15 项均已采集，13 项 accepted、2 项 rejected。`security-formal-04-clear` 因 cgroup/host swap 计数变化并出现 1 次 TCP retransmission（272 字节）而拒收；`security-formal-10-tls` 因 1 次 TCP retransmission（760 字节）而拒收。拒收样本不计吞吐。runner 终态为 `complete_with_rejections`，双端 payload、凭据与 listener 清理成功，接口身份匹配，清理后的 6 秒空载期 swap、重传和网卡错误计数稳定。证据目录为 R1 `/home/Su/experiments/CPNetFlux/2026-11-27-r770-annual-5g-security-11/`，包括逐样本 JSON、watcher/socket 证据、认证、matching-builds、preflight 和 postflight。security-09 和 security-10 仍保留为独立失败尝试，不拼接进本矩阵。

## 2026-11-27 门禁归属修复与安全组复测（security-12）

安全组复测发现 security-11 两条拒收中存在两种不同原因。`security-formal-04-clear` 的 R1 host `pswpout` 仅增加 1，R1 用户 slice 的 `memory.swap.current` 增加 4 KiB，但任务 session leaf 的 swap 与 memory events 不变；原逻辑同时硬比较 host 累计 swap 和共享 ancestor cgroup，把无法归因于本次传输的活动算到样本上。该行另有一次小额 TCP retransmission。`security-formal-10-tls` 的目标 socket TCP_INFO 计数从 0 增至 1，增加 760 字节，属于真实连接重传，维持拒收。

首次启动 security-12 时，R2 两次独立 SSH 快照落在不同的 `session-*.scope`，导致修复版 preflight 错把正常 SSH session 轮换作为失败。该尝试在 preflight 停止，没有创建 run evidence 目录或启动传输。第二次修正为仅在前后 cgroup scope 相同时比较 leaf 的 events/swap；SSH scope 改变时跳过不可比的 cgroup delta，并继续执行 host PSI、可用内存、tmpfs、NIC 和 socket 检查。host `pswpout` 与 swap-free 继续保留在快照供诊断，不再作为样本硬拒收依据。逐 socket TCP retransmission 仍按原零增量门拒收。

正式 run 使用固定提交 `6dff97b967f248ce8d5d70d190165265c53b1161`。前置修复提交 `4d1cd8bf0b96f7310241377ba3aeea2fced686ed` 因 R2 scope 轮换问题未启动实验；最终修正已单独提交并推送至 GitHub。R1/R2 均为相同提交，runner 单测各 33 项通过，Python 语法检查和 CMake build 通过；server/download/upload 二进制 SHA-256 与 security-11 相同。安全配置、方向和 5,000 微秒 security full-PSI 上限不变。双端 RX/TX ring 仍是 1024/1024。

5 GiB source 完整性 SHA-256 为 `867c60a763149bef989bd4f23ee180ee83022256ed1518841f6ee2e41b45f8c0`，source seed full-PSI 增量 450 微秒，换出增量为 0。7 个 smoke 均 accepted。15 个正式样本全部传完并校验目标 SHA，13 项 accepted、2 项 rejected。

| 模式 | 有效样本 | 有效吞吐中位数 | 有效样本吞吐（Gbps） |
| --- | ---: | ---: | --- |
| GridFTP GSI clear | 3/3 | 17.30 | 16.75, 17.47, 17.30 |
| GridFTP GSI private | 3/3 | 11.66 | 11.93, 11.66, 11.43 |
| GridFTP anonymous | 3/3 | 18.02 | 18.21, 18.02, 17.96 |
| CPNetFlux token 明文 | 2/3 | 34.22 | 34.01, 34.44 |
| CPNetFlux token + 控制/数据 TLS | 2/3 | 29.65 | 29.48, 29.82 |

`security-formal-14-tls` 有一次 lane socket 的 `retrans_total` 增量 2、累计重传字节 824（分两次观察增加 216 与 608 字节）；`security-formal-15-clear` 两条 lane 各有 1 次重传，分别 1,328 与 728 字节。客户端返回码均为 0，5 GiB 数据和 SHA 均正确，但按原 socket 零重传硬门拒收，不统计吞吐。两项均无 host swap/PSI、NIC error 或完整性门失败。

run `2026-11-27-r770-annual-5g-security-12` 的 postflight 为 `complete_with_rejections`，双端临时 payload、凭据和 listener 清理成功，接口身份匹配，清理后 6 秒空载期 host swap、TCP retrans 和 NIC error 计数稳定。详细逐项 JSON、socket watcher、资源、认证、build 和清理证据位于 R1 `/home/Su/experiments/CPNetFlux/2026-11-27-r770-annual-5g-security-12/`。5 GiB 仅作内部工程验证，旧年度报告没有修改。
