# V3-GRIDFTP-87B389-RERUN-01 实验报告

日期：2026-10-29。状态：隔离部署、软件门禁、两机实验、证据审计与临时数据清理完成。

结论：本轮 V3 在密集小文件目录优于 GridFTP；随机/重复大文件和混合目录仍有差距。
相对 10 月 24 日 aa20229 基线，V3 汇总 CRC 的尾部开销显著下降，大文件数据路径也有改善。
这是一轮特定硬件、热缓存、TCP/TLS 条件下的有界性能对比，不代表跨域、原生 RDMA 或生产就绪验收。

## 1. 精确版本、构建和安装

- 用户指定实现提交：`87b389fc34615d1c43ccb36b5064beda2f47d1f6`。
- 权威仓库：深圳 `/root/projects/CPNetFlux`；沿用 `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`。
- GitHub remote：`git@github.com:Cantoneseman/CPNetFlux.git`。本任务只添加任务、结果及精简证据。
- 精确源码归档 SHA-256：`eac263310902ffd48d18bf1cb92c6598ebd5ef0724907275d00504fad47a6ff8`；解压后的 **665 个 Git blob** 逐一对照目标提交，内容一致。
- 在深圳重新从该归档构建 Release，TLS ON，io_uring compiled OFF，desktop OFF，实际传输 I/O 为 POSIX。
- 源码归档不含 `.git`，CMake 的 `build-info.json` 如实标记 `source-archive`；不能把该字段冒称为实现 SHA。精确版本由 Git blob 审计、归档与二进制哈希共同证明。
- 两台服务器的隔离安装目录均为 `/tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01/bin/`；保留本轮服务端和上传端程序，未替换系统安装或桌面服务。

| 程序 | 深圳重建、R730、R750 一致的 SHA-256 |
|---|---|
| cpnetflux-gridftp-server | `336b2f21f610fa25c9c5dfb3afa95ac163e4ffda735a18014e290c69e25194b3` |
| cpnetflux-tree-upload-client | `9708f90109d008cd7c7e57dcdb121abb6210c8f46adad95ee527df6614611e58` |

- 精确源码定向 CTest：11/11；覆盖单次 sendmsg、短写、EINTR、EAGAIN、背压、断连及静态中断恢复。
- 全量 CTest：467 项，466 通过、1 项 io_uring 能力跳过、0 失败，耗时 235.67 秒。Release 构建成功日志未检出 compiler warning。
- 实验分析器单测：6/6；严格正式样本分析与补充断言通过。构建/测试日志随附件保存。

## 2. 网络、认证、资源与计时口径

- 发送端 R730：`192.168.100.1`、`enp130s0np0`；接收端 R750：`192.168.100.2`、`enp177s0np0`。
- 两端协商 100000 Mb/s、MTU 9000，本轮走直连 Ethernet 上的普通 TCP/TLS，不是 libibverbs/RDMA 数据路径。
- 5 秒 iperf3 接收基线：单流 **29.588 Gbps**、8 流 **31.153 Gbps**。它们是本次短测结果，不是标称 100G 的持续极限。
- 两端 NUMA 拓扑已记录；本轮没有 numactl 绑核/绑内存，不声称启用了 NUMA 优化。
- 初始 R730 约 64 GiB、R750 约 69 GiB 可用；每个样本完成 SHA-256 后删除该样本接收目录，不堆积多份大文件。
- GridFTP 客户端 10.10、服务端 13.22；刷新 12 小时 GSI proxy，受保护文件权限 600，服务端证书链验证通过。
- 每个 GridFTP 组先跑 8 MiB 的 GSI/private smoke；每组前检查 proxy 至少余 1 小时，结束有效期仍充足。认证失败未作为性能结果。
- 无清页缓存；正式组发送进程采样的物理读为 0，属于本规模下热缓存实验，不能外推冷盘持续带宽。
- 计时是客户端进程启动到退出的 monotonic wall，包括工具启动及协议收尾。源树 SHA、接收端独立 SHA-256 都在该区间之外，接收端 SHA 时间另列。
- 各组串行、非随机交错执行；每个正式数据集三次，报告中位数与原始最小/最大值。不能用三次样本宣称严格统计显著性。
- 采样周期约 0.5 秒，进程 CPU/物理 I/O 采样值存在边界遗漏；接收进程 CPU 字段是采样下界。

## 3. 输入与功能配置

输入文件数、字节数及四个 SHA-256 树哈希均与 2026-10-24 实验一致，总计 **10,875,830,272 bytes = 10.12890625 GiB**。这是唯一输入规模，重复与筛选会产生更多网络传输字节。

| 数据集 | 内容 |
|---|---|
| dense | 2048 × 64 KiB，128 MiB，32 个目录 |
| large-random | 1 × 4 GiB 确定性伪随机文件 |
| large-repeated | 1 × 4 GiB 重复内容文件 |
| mixed | 1 × 2 GiB + 512 × 8 KiB + 1 空文件，共 514 文件 |

先筛选 V3 4/8/10 路、GridFTP 4/8/10/16 路，每配置在 dense/random 各一次，共 14/14 通过。
以这两个场景进程 wall 的几何平均值选一个统一配置，两者均选 4 路；这只代表已测候选的单次综合选择。

| 系统 | 路数 | dense 秒 | random 秒 | 几何平均秒 |
|---|---:|---:|---:|---:|
| v3 | 4 | 1.290 | 7.887 | 3.190 |
| v3 | 8 | 1.368 | 8.037 | 3.316 |
| v3 | 10 | 1.168 | 8.841 | 3.213 |
| gridftp | 4 | 1.838 | 6.419 | 3.435 |
| gridftp | 8 | 2.869 | 6.351 | 4.268 |
| gridftp | 10 | 3.354 | 6.667 | 4.729 |
| gridftp | 16 | 5.258 | 6.954 | 6.047 |

V3 正式配置：`actual_transport=v3`、`persistent_tree`、dynamic 文件调度、共享工作租约与 range、worker 控制连接复用、tree 数据会话复用；4 路、pending 8、chunk 4 MiB、global/fixed scheduler、CRC32C auto、控制和数据 TLS required、token auth、compression off。
12 个有汇总的正式样本均确认实际 dynamic、控制/数据各建连 4 次、无 fallback、TLS 验证成功、压缩尝试/压缩帧为 0，manifest generation 与 persisted generation 一致。
实测 dense 不切 range，两个 4 GiB 和 mixed 的 range_completed 计数均为 8；小文件保持文件级并发。`control_pipeline_depth=0` 不表示复用关闭，实际是 `shared_persistent_dispatch`。

接收端：`manifest-flush-policy=final_only`、`final-verify-policy=verified_chunks`、`commit-sync-policy=none`、preallocate off、POSIX、nofile 8192。该高速配置不承诺 checkpoint 或最终文件已逐次 fsync。
调度 workitem min/max 512 MiB、capacity 100 Gbps、default RTT budget 160 ms；后者沿用上轮模型参数，不是本次实测 RTT。

GridFTP：`-dcpriv -fast -cc 4 -p 4 -r -cd -rp -rst -vb -dbg`，GSI、server encrypt-data、MODE E 和数据复用；认证交换内容不写入导出的 debug 日志。
未启用 GridFTP 应用层逐文件 checksum；双方都做接收端外部 SHA-256，但两者事务、恢复和校验语义的成本不完全相同。

## 4. 正式性能结果

三组各 4 场景 × 3 次，共 **36/36 通过**：V3 summary on、GridFTP、V3 summary off。no-summary 只去掉 JSON 汇总，不关闭传输校验、复用、调度或阶段计时。

| 场景 | V3 汇总秒 | V3 汇总 Gbps | V3 无汇总秒 | V3 无汇总 Gbps | GridFTP 秒 | GridFTP Gbps |
|---|---:|---:|---:|---:|---:|---:|
| dense | 1.268 | 0.847 | 1.168 | 0.920 | 1.801 | 0.596 |
| large-random | 7.889 | 4.355 | 7.837 | 4.384 | 6.250 | 5.498 |
| large-repeated | 8.139 | 4.222 | 7.988 | 4.301 | 6.517 | 5.272 |
| mixed | 4.227 | 4.072 | 4.126 | 4.172 | 3.867 | 4.451 |

- 保留汇总时：dense 耗时比 GridFTP 少约 29.6%；random/repeated 多约 26.2%/24.9%；mixed 多约 9.3%。
- 不带汇总时：dense 耗时比 GridFTP 少约 35.2%；random/repeated 多约 25.4%/22.6%；mixed 多约 6.7%。

| 系统/组 | 场景 | 三次 wall 最小秒 | 三次 wall 最大秒 | 外部接收 SHA-256 中位秒 |
|---|---|---:|---:|---:|
| formal-gridftp | dense | 1.781 | 1.931 | 0.545 |
| formal-gridftp | large-random | 6.053 | 6.585 | 5.744 |
| formal-gridftp | large-repeated | 6.294 | 6.585 | 5.568 |
| formal-gridftp | mixed | 3.810 | 3.912 | 3.048 |
| formal-v3 | dense | 1.169 | 1.318 | 0.599 |
| formal-v3 | large-random | 7.888 | 8.239 | 5.638 |
| formal-v3 | large-repeated | 7.990 | 8.189 | 5.663 |
| formal-v3 | mixed | 3.975 | 4.275 | 3.014 |
| formal-v3-no-summary | dense | 1.067 | 1.218 | 0.588 |
| formal-v3-no-summary | large-random | 7.339 | 8.140 | 5.695 |
| formal-v3-no-summary | large-repeated | 7.890 | 8.140 | 5.558 |
| formal-v3-no-summary | mixed | 4.126 | 4.127 | 2.984 |

## 5. 阶段耗时与资源

**下表除 scan、core elapsed、summary hash 外，多为文件/range/通道并发累计区间，可能重叠；严禁相加成目录 wall 或计算 wall 占比。** queue wait 也是所有排队文件的累计等待。receiver write 包含协调与状态更新，不是裸磁盘时间。

| 阶段（秒） | dense | random | repeated | mixed |
|---|---:|---:|---:|---:|
| scan | 0.057 | 0.000 | 0.000 | 0.015 |
| control prepare 累计 | 0.246 | 0.261 | 0.255 | 0.260 |
| data connect 累计 | 0.069 | 0.070 | 0.076 | 0.057 |
| queue wait 累计 | 52.647 | 15.302 | 15.797 | 24.643 |
| complete wait 累计 | 1.902 | 0.055 | 0.057 | 0.717 |
| core elapsed | 1.046 | 7.700 | 7.937 | 4.082 |
| sender read 累计 | 0.052 | 1.599 | 1.662 | 0.799 |
| sender payload I/O 累计 | 0.365 | 28.672 | 29.479 | 14.127 |
| sender checksum 累计 | 0.033 | 1.660 | 1.663 | 0.826 |
| sender manifest 累计 | 0.031 | 0.000 | 0.000 | 0.007 |
| receiver write 累计 | 0.126 | 16.686 | 16.884 | 8.563 |
| receiver checksum 累计 | 0.074 | 3.224 | 3.282 | 1.632 |
| receiver manifest 累计 | 1.125 | 0.000 | 0.000 | 0.335 |
| receiver finalize 累计 | 1.116 | 0.004 | 0.003 | 0.329 |
| summary tree hash | 0.084 | 0.173 | 0.172 | 0.102 |

逐文件 first_payload、read、checksum、manifest、payload_io、file_result 的 count/sum/p50/p95/max 见 `sender-stages.csv` 与 `diagnostics.json`，不能把不同场景的累计值直接比较成单文件延迟。

| 场景 | V3 sender CPU 秒（user+sys 中位数之和） | GridFTP sender CPU 秒 | V3 receiver 采样 CPU 下界秒 | V3 receiver 采样最大 FD 中位数 |
|---|---:|---:|---:|---:|
| dense | 1.870 | 1.340 | 1.640 | 19 |
| large-random | 16.140 | 6.040 | 19.800 | 17 |
| large-repeated | 16.320 | 5.950 | 21.080 | 17 |
| mixed | 8.290 | 3.400 | 9.430 | 18 |

GridFTP 没有与 CPNetFlux 完全同名、同边界的内部阶段接口，不能制造一套同粒度数字。其逐文件服务端区间采用并集处理，结果如下；完整每文件分布、CPU/I/O/NIC 采样保留在附件。

| 场景 | GridFTP 文件传输区间并集秒 | wall 减去区间并集秒 |
|---|---:|---:|
| dense | 1.375 | 0.490 |
| large-random | 5.845 | 0.442 |
| large-repeated | 5.902 | 0.445 |
| mixed | 3.423 | 0.489 |

## 6. 与 aa20229 上轮的变化及剩余瓶颈

| 场景 | 上轮 V3 汇总秒 | 本轮 V3 汇总秒 | 上轮/本轮耗时比 | 上轮无汇总秒 | 本轮无汇总秒 |
|---|---:|---:|---:|---:|---:|
| dense | 1.519 | 1.268 | 1.198 | 1.218 | 1.168 |
| large-random | 12.702 | 7.889 | 1.610 | 9.741 | 7.837 |
| large-repeated | 12.851 | 8.139 | 1.579 | 9.545 | 7.988 |
| mixed | 6.833 | 4.227 | 1.617 | 4.979 | 4.126 |

1. CRC 汇总尾部开销明显改善：random 约 3.198 → 0.173 秒，repeated 约 3.399 → 0.172 秒，mixed 约 1.827 → 0.102 秒。全部汇总来自 verified_chunks、hardware，无文件系统重扫 fallback。
2. 大文件数据路径也有改善：random 无汇总进程 wall 约 9.741 → 7.837 秒，但仍慢于 GridFTP 的 6.250 秒。4 GiB random 的 V3 sender user+sys 中位数之和约 16.14 CPU 秒，GridFTP 约 6.04；接收 write/checksum 并发累计仍约 16.69/3.22 秒。
3. 不能把这轮提升归因于 sendmsg：aa20229 到本轮还包含 CRC combine 算子缓存（5352b90）与已验证帧 CRC 复用（2bbe7f2）。本轮 TLS DATA 路径没有使用明文 sendmsg batching，也未做同机 A/B 消融，跨日期差异还包含环境波动。
4. pipeline 预算仍为 131064 bytes，大文件 producer wait 仍频繁；详见 `pipeline-medians.csv`。这提示下游背压/流水线粒度仍值得分析，但等待次数不能直接当成等待耗时，更不能单凭它认定某一把锁或 TLS 的占比。
5. NIC 没有 errors/drops 增量，正式发送端物理读采样为 0，系统总体 iowait 很低，应用吞吐仍远低于 29.6–31.2 Gbps 的短时 TCP 基线。现有证据更支持优先剖析数据路径 CPU、复制/分配、TLS record 处理和共享写协调，而不是继续增加连接数。低总体 CPU 利用率不排除少数热点线程已饱和。
6. 下一步先固定本轮配置采 CPU 栈、线程热点和锁等待，再按单变量验证 pipeline 预算/批量粒度；需要衡量明文 sendmsg 的收益时，应另做 TLS off 的父提交/目标提交成对实验，不能混入本轮安全对照。

## 7. 审计、清理和复现材料

- 正式 36/36、筛选 14/14；每个样本均检查退出码、路径/文件数、字节数与接收端独立 SHA-256。GridFTP 正式逐文件记录覆盖完整、CODE 均为 226。
- GridFTP 全部筛选/正式/冒烟共 15,893 条服务端文件记录；导出仅保留 FILE/START/DATE/NBYTES/CODE/STREAMS，client debug 只留命令名和响应码。
- 原始证据 441 个文件逐项验证大小与 SHA-256；导出前本轮 token 精确字节匹配数、私钥 PEM 标记数均为 0。凭据、payload、产品二进制及完整产品源码未装入数据包或 Git，实验脚本随证据保留。
- 本轮的 34261、32811、34000–34100、35000–35031、36666 端口已关闭，任务进程已结束；未停止其他服务。
- R730 删除本轮约 10.13 GiB 输入、token/proxy/TLS 私钥，释放约 10,884,706,304 bytes；R750 删除本轮凭据和残余接收根目录。样本 payload 已逐次清理，详细最终状态见 cleanup-sender.json / cleanup-receiver.json。
- 两端隔离二进制与证据保留。清理前 runner 的 `payload_retained=true` 是中间状态，最终状态以两份独立 cleanup 记录为准。
- 深圳完整证据根：`/tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01/`；原始脱敏包 `lab-evidence.tgz` SHA-256：`fa04ed82faa397335845fdbab353713f7d4d87afa31b051cfb06d5edcb0ea0bc`。
- Git 精简证据：`docs/experiments/2026-10-29-v3-gridftp-87b389-rerun-01/`。其中 comparison/medians/cases/tuning/stage-medians/resource-medians/pipeline-medians CSV 可直接用于后续瓶颈分析。
- Windows 交付：`D:\读研\干活\算力网项目——存储优化\CPNetFlux\实验报告\2026-10-29-V3-87b389-GridFTP`；含本报告、精简数据、原始脱敏包、构建/测试/分析记录及文件校验清单。

复现与验收命令（源码及所需凭据由管理员在专属目录准备，脚本会拒绝覆盖已完成组）：

```bash
python3 -m unittest discover -s tools/perf/experiments/v3_gridftp_raw_stage_01 -p "test_*.py" -v
python3 /tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01/lab/analyze.py /tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01/lab
python3 /tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01/summarize.py
python3 /tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01/provenance.py
```

未完成项：本轮没有冷盘持续吞吐、NUMA 绑定、原生 verbs、故障注入长稳或父/子提交明文 A/B 性能矩阵；静态断连后的状态更新由定向及全量软件回归覆盖，本轮成功传输样本不作为该故障窗口的物理双机复现证据。
