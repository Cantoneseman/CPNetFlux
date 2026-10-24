# V3-GRIDFTP-OPTIMIZED-RERUN-01 实验结果

日期：2026-10-24。状态：实验、分析和清理完成；仅为本轮有界性能验收，不是生产就绪或 100G 满速结论。

## 版本与环境

- 用户指定输入：`aa20229e0844aa2a5ec80c5b372d5448931b856d`，构建版本 `0.1.0+git.aa20229e0844`。
- 深圳权威根目录分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`，GitHub remote 为 `git@github.com:Cantoneseman/CPNetFlux.git`；实验期间 runtime、agent、UI、系统安装、路由和防火墙均未修改。根目录两份既有 worker-state untracked 文档保留。
- 使用深圳 Release / TLS ON / io_uring compiled ON / desktop OFF 构建；实际传输文件 I/O 为 POSIX。两端部署隔离实验二进制，不覆盖已安装服务。
- 服务器 SHA-256：`9ea1bbb9457fd64e93308837cdb3489deda1c1d960afca8865a47594cdf5692e`。
- 上传客户端 SHA-256：`9fb16945de6c1a2edf76afd6f77608feeba1e90b33c26926b3e2d9d85ca05c60`。
- R730 发送、R750 接收，地址 `192.168.100.1` → `192.168.100.2`，数据接口 `enp130s0np0` / `enp177s0np0`，MTU 9000，协商速率 100000 Mb/s。
- 使用直连网卡上的普通 TCP；没有原生 verbs，没有跨域。本轮 iperf3 5 秒单流 / 8 流接收为 **32.688 / 28.655 Gbps**，不是长期极限。短测存在 CPU 和调度波动，不能把网卡标称 100Gbps 当有效吞吐。
- GridFTP 客户端 10.10、服务端 13.22。实验前刷新 12 小时 GSI proxy，文件 600；首次正式前剩余 41404 秒，正式组后公开环境采集仍有 11:15:32。GSI/private smoke 和全部正式样本成功。
- 角色检查中的预期非零结果原样保留：R730 无 GridFTP server；R750 不需要客户端 proxy；`globus-url-copy -version` 把版本写到 stderr 并返回 1。ethtool 有非特权 netlink 提示，但已返回 link/speed；这些不等于认证或性能样本失败。

## 数据、配置与边界

输入与 2026-10-22 上轮的文件数、字节数和四个 SHA-256 树哈希完全一致，共 **10,875,830,272 bytes = 10.12890625 GiB**：

| 数据集 | 内容 |
|---|---|
| dense | 2048 × 64 KiB，128 MiB |
| large-random | 1 × 4 GiB 确定性伪随机文件 |
| large-repeated | 1 × 4 GiB 重复内容文件 |
| mixed | 1 × 2 GiB 大文件 + 512 × 8 KiB + 1 空文件，共 514 文件 |

没有清页缓存；发送端正式组采样物理读取均为 0，属于此规模下热缓存实验，不能外推冷盘持续吞吐。源树 SHA 和传输后接收端独立 SHA-256 均不计入进程 wall；接收端校验另列。

先筛选 V3 4/8/10、GridFTP 4/8/10/16 路，在 dense / random 各运行一次。**14/14 筛选样本通过**。V3 dense 最佳单次为 10 路 1.468 秒，但 random 4 路 10.498 秒明显优于 8/10 路 13.146/12.949 秒；统一正式配置选 4 路。GridFTP 两类均 4 路最佳（dense 1.966 秒、random 6.569 秒）。这里只称已测配置中的综合选择，不是穷举所有参数的全局最优。

V3 正式启用 `actual_transport=v3`、`persistent_tree`、dynamic 文件调度、共享工作租约、大文件 range、worker 控制复用、tree 数据会话复用、CRC32C、控制/数据 TLS；4 路、pending 8、chunk 4 MiB、global fixed scheduler。大文件实际 8 个 range，dense 保持文件级并行；每样本控制/数据建连均为 4。旧 `control_pipeline_depth=0` 不表示复用关闭，实际为 `shared_persistent_dispatch`。

接收端使用 `manifest-flush-policy=final_only`、`final-verify-policy=verified_chunks`、`commit-sync-policy=none`、preallocate off、POSIX；nofile 8192。调度 work item min/max 512 MiB、capacity 100 Gbps、default RTT budget 160 ms，后者是沿用上轮的模型参数，不是实测 RTT。`--compression off`，阶段观测组压缩尝试和压缩帧均为 0。本配置不承诺每个 checkpoint 已 fsync。

GridFTP 使用 `-dcpriv -fast -cc 4 -p 4 -r -cd -rp -rst -vb -dbg`，GSI、服务端 encrypt-data、MODE E 和数据复用；debug 流只保存命令名/响应码，GSI 交换内容不落日志。本轮未启用 GridFTP 应用层逐文件 checksum；两者同做外部 SHA-256，但不宣称它们事务、恢复和校验语义成本完全相同。

正式三组各 4 数据集 × 3 次，共 **36/36 成功**：V3 summary on、GridFTP、V3 summary off。每个正式样本均检查退出码、文件数、字节数和接收端 SHA-256 树哈希；GridFTP 每文件 CODE=226 和日志覆盖通过。全部正式样本 NIC errors/drops 增量为 0。

## 性能结果

以下均为三次中位数，单位秒，计时边界为客户端进程启动到退出。no-summary 组只去掉 `--json-summary`，传输策略、阶段计时、日志和外部 SHA 不变；不能用 core elapsed 替代进程 wall。

| 数据集 | V3 summary 秒 | V3 no-summary 秒 | V3 no-summary Gbps | GridFTP 秒 | GridFTP Gbps | V3 no-summary / GridFTP 耗时 |
|---|---:|---:|---:|---:|---:|---:|
| dense | 1.519 | 1.218 | 0.882 | 1.866 | 0.575 | 0.653 |
| random 4 GiB | 12.702 | 9.741 | 3.527 | 6.445 | 5.332 | 1.512 |
| repeated 4 GiB | 12.851 | 9.545 | 3.600 | 6.311 | 5.444 | 1.512 |
| mixed | 6.833 | 4.979 | 3.457 | 3.928 | 4.382 | 1.268 |

dense 无 summary 比 GridFTP 快约 **1.53 倍**；即使保留 summary，也快约 **1.23 倍**。两类大文件无 summary 仍比 GridFTP 多约 **51%** 耗时，mixed 多约 **27%**，不能宣称本版已全面超过 GridFTP。

与上一轮 `55f7a21` 同输入记录对照：

| 数据集 | 上轮 V3 summary 秒 | 本轮秒 | 上轮 V3 no-summary 秒 | 本轮秒 |
|---|---:|---:|---:|---:|
| dense | 22.525 | 1.519 | 19.964 | 1.218 |
| random | 20.419 | 12.702 | 9.390 | 9.741 |
| repeated | 21.276 | 12.851 | 9.291 | 9.545 |
| mixed | 12.749 | 6.833 | 5.780 | 4.979 |

dense 无 summary 记录约快 16.4 倍，mixed 约快 1.16 倍；大文件 no-summary 本轮反而慢约 2.7–3.7%，没有大文件核心加速证据。两轮不在同一天，也不是本轮交错运行旧/新版本的严格单变量消融，因此不能把全部差异唯一归因于某一提交。

V3 summary 自报 core elapsed 中位为 **1.129 / 9.463 / 9.413 / 4.943 秒**；core 后至退出约 **0.426 / 3.239 / 3.438 / 1.890 秒**。两列分别取中位数，和不要求等于进程 wall 的中位数。外部接收端 SHA-256 另需约 dense 0.55–0.59 秒、4 GiB 5.57–5.73 秒、mixed 2.96–3.00 秒，均不隐藏到吞吐中。

## 阶段与剩余瓶颈

下表来自 summary on 组三次中位数。除扫描、summary hash 等明确独立计时外，control/data/complete 和逐文件/range 阶段为并发累计区间，有重叠，**不得相加成目录 wall 或计算 wall 百分比**。receiver write 包含协调器锁、checkpoint 等区间，不是裸磁盘服务时间。

| 秒，部分为并发累计 | dense | random | repeated | mixed |
|---|---:|---:|---:|---:|
| scan | 0.064766 | 0.000126 | 0.000121 | 0.017330 |
| control_prepare 累计 | 0.230121 | 0.263529 | 0.260449 | 0.247636 |
| data_connect 累计 | 0.061117 | 0.057554 | 0.067356 | 0.067898 |
| transfer_complete_wait 累计 | 2.176434 | 0.067057 | 0.069148 | 0.836718 |
| sender payload_io 累计 | 0.403172 | 35.783059 | 35.773917 | 17.311109 |
| sender per-file manifest 累计 | 0.032681 | 0.000197 | 0.000190 | 0.006435 |
| receiver write 累计 | 0.142434 | 22.233185 | 22.169220 | 11.073654 |
| receiver checksum 累计 | 0.113609 | 4.802202 | 4.777557 | 2.411856 |
| receiver finalize 累计 | 1.199681 | 0.003607 | 0.004415 | 0.371179 |
| summary tree hash | 0.359051 | 3.197620 | 3.398818 | 1.827412 |

1. **目录 manifest 的主瓶颈已明显缓解。** dense 状态事件 4097 次，中位快照 129 次，整树持久化写计时约 0.495 秒；mixed 为 1043 事件 / 35 快照。所有最终 generation 与 persisted generation 一致。上轮 dense sender per-file manifest 累计 77.53 秒，本轮约 0.033 秒；新增专门持久化计时还包含快照工作，不能把这两种指标当成完全同口径的直接替换。
2. **已完成文件的 FD 占用改善。** 本轮 dense 接收端采样最大 FD 20，上轮为 2064；不再看到每完成一个文件仍保留一个句柄的现象。本轮正式仍沿用 nofile 8192，不能用本次高 limit 结果代替所有低 limit/长期稳定性验收。
3. **summary 避免了重扫文件，但 CRC 合并仍有成本。** 所有观测样本 `summary_tree_hash_source=verified_chunks`、backend=hardware，无 filesystem fallback；4 GiB hash 合并仍需约 3.2–3.4 秒，上轮软件重扫后置区间约 11.8–12 秒。源码 `tree_hash.cpp` 对每 chunk 调用 `crc32cCombine`，后者每次重建 GF(2) 矩阵，是下一轮缓存/批量合并的明确候选；尚未在本任务做微基准或 CPU 栈归因，不能断言它独占全部时间。
4. **大文件数据路径依然受背压/协调开销约束。** payload pipeline 预算仍是 131064 bytes，random/repeated producer wait 中位 65362/65302 次；receiver write 累计约 22.2 秒，no-summary core 约 9.5 秒。源物理读采样为 0，整体 iowait 很低；这些证据支持优先分析流水线预算、共享写协调和重复 frame/checksum 工作，尚不足以精确分摊锁与 CPU 开销。
5. **本轮没有打满 TCP 基线。** 应用有效吞吐仅约 3.5–5.4 Gbps，低于短时 iperf 的 28.7–32.7 Gbps。增加并发没有解决大文件差距，不能继续把路数当成唯一优化手段。后续应固定版本做 pipeline budget / pending window 单变量实验，采 CPU 栈和锁等待，再决定写协调或 CRC 合并优化。

## 验证、证据与清理

- 精确输入版本 Release 构建成功；runtime 对 aa20229 无差异，构建 `build-info.json` 与二进制 SHA 见 `provenance.json`。
- C++ unit **388/388**；tree upload/resume/data reuse/pending window/manifest coalesce/range checkpoint batch/control reuse 定向 CTest **7/7**；分析器单测 **6/6**。本次实验任务没有重跑全量 CTest，不把定向通过写成全量通过。
- 严格审计 **36/36 正式、14/14 筛选**；源输入与上轮完全一致，GridFTP 逐文件日志完整、全部接收端 SHA-256 一致。
- Git 精简附件位于 `docs/experiments/2026-10-24-v3-gridftp-optimized-rerun-01/`：cases/medians/comparison/tuning/stage-medians/sender-stages、diagnostics、输入哈希、双端公开环境、构建及测试记录、双端清理记录和 evidence manifest。
- 深圳脱敏原始证据目录：`/tmp/cpnetflux-runs/V3-GRIDFTP-OPTIMIZED-RERUN-01/20261024T071000Z-aa20229`。原始归档 `/tmp/cpnetflux-runs/cpnetflux-v3-optimized-aa20229-evidence-20261024.tgz`，SHA-256 `514c6c927999f3c698225546c1c2e1e5f45cfd732fac62fe11aa987e21ec9c5a`；环境/筛选与清理补充归档 SHA 见 provenance。
- 脱敏证据做过本轮 token 精确字节扫描，没有 token 值或 TLS 私钥进入归档/Git。GridFTP 原始 transfer log 归档仅保留 FILE/START/DATE/NBYTES/CODE/STREAMS。
- 实验进程全部结束，34261 / 32811 / 数据端口与 36666 无监听；两端仅删除本任务 payload/接收落地目录、token、proxy 和 TLS 私钥。R730 释放 10,884,706,304 bytes；原有 agent/desktop/其他实验未停止或删除。
- 本任务只提交上述计划/结果/精简附件；输入源码版本仍是 aa20229，后续文档提交不改变本次实验版本。

下一步建议：先优化 verified-chunk CRC 合并报告成本；随后对大文件 pipeline 与写协调做固定输入、单变量测试。本轮不顺带修改 runtime 或扩展新的实验矩阵。
