# V3 Kernel Service17 — 完整任务内核服务成本归因

日期：2026-11-09。范围见 [任务合同](2026-11-09-v3-kernel-service-17.md)。
前序输入 `50b7c0aa6780d036e5946b5f3208a0d931bbf728`，范围提交及六次互传输入均为
`025c38a99994dfe39bf3444c60faf77e44b1f933`。
分支 `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`；R770-1 唯一写入者，R770-2 固定提交验收。

## 结论与当前门禁

**完成诊断，没有新的运行时优化或吞吐收益。** 三对完整 V3 TCP 文件任务通过内容、
协议终态和资源门；99Hz 进程级采样通过预设低开销门。当前同轮 baseline/profile
整任务吞吐中位数为 **27.291152 / 27.295766 Gbps**，不可把这点差异作为提速。
三阶段工程收口，30–50 Gbps 完整文件性能门仍未过，100 Gbps 未达到。

接收端 CPU 样本以新文件映射写触发的缺页处理和 TCP 接收路径为主；发送端以驻留
文件 pread 和 TCP 发送搬运为主。队列 futex 的在 CPU 运行占比小，但本次采样不计
阻塞等待，不能据此单独证明等待成本为零。没有足够的机制和关键路径预算支持新的
大文件候选达到 >=10% 整任务收益，因此按范围合同停止继续扩大三阶段和大文件候选。

**网络计数门单列为未清洁：** sample02 的 R770-1 `rx_missed_errors` 增加 94；
其余五次该计数及六次其他所存 error/drop/collision 增量均为零。保留全部六次，
不排除该样本，不把完整性通过等同于网络稳定性通过；该现象是否属于本连接、如何
影响吞吐，现有接口总计数不能证明。两次无此事件的采样也显示相同主要热点。

## 实现和测量范围

运行时、默认参数、协议、校验、租约、恢复和发布逻辑均未改。新增诊断脚本位于仓库外
实验目录：`process_probe_17.py`、`kernel_matrix_17.py`、`profiler_smoke_17.py`、
`analyze_kernel_17.py`、`final_resources_17.py`。

- R770-2 -> R770-1；数据显式绑定 `192.168.100.2` / `192.168.100.1`，接口
  `eno17095np0`，保存路由和接口计数。数据面为 TCP，不称为原生 verbs。
- 真实 8 GiB tmpfs 驻留源、每次新建目标；4 TCP lanes、8 个 1 GiB ranges；
  queued sender / inline 实验 mmap receiver，read0/write0、batch1/depth2。
  源由 8 MiB 随机块重复 1024 次生成并实际写入全部 8 GiB；压缩关闭，不能外推为熵敏感负载。
- payload 预算 64 MiB；映射 64 MiB/lane，总映射上限 256 MiB。CRC32C、
  verified_chunks、checkpoint16、commit_sync=none、preallocate off、TLS/compression off。
- 原有 detail off，新 CPU profile off；基本 phase 和稀疏 trace 在两组均保留。
  V3 WorkExecutor、工作租约和 8192 条认证 chunk 的 committed manifest 均存在。
- 两组都有同一个 100ms /proc 采集器。profile 组仅增加 exact PID/start-tick 范围
  的 99Hz cpu-clock/frame-pointer 采样；新建工作线程继承采样，不做全系统采样。
- 客户端先停在 exec gate，采集器和 perf ready 后统一释放。任务墙钟采用释放到
  客户端进程退出；perf 准备时间不计入两组的吞吐分母。独立 SHA 在被采样进程外。
  `/usr/bin/time` 的墙钟含准备等待，**不能代替任务墙钟**；其 CPU 含小型 Python gate，
  双组口径相同。server CPU 是本任务拥有进程的 CPU 增量。
- profiler 可覆盖 idle setup；不能把采样时间区间跨度当作数据传输墙钟。frame-pointer
  用户栈可能截断；初始客户端 status/numa_maps 来自 exec 前 gate，不能当作数据映射证据。

## 完整任务与开销对照

预先固定三对顺序：base01/sample01、sample02/base02、base03/sample03；无正式样本排除。
每个任务 8 GiB，即 68.719476736 Gbit；下表时间均采用 gate-release-to-exit。

| 运行顺序 | 任务 | 整任务墙钟 s | 整任务 Gbps |
|---|---|---:|---:|
| 1 | base01 | 2.566769460 | 26.772750 |
| 2 | sample01 | 2.517061052 | 27.301474 |
| 3 | sample02 | 2.518939261 | 27.281117 |
| 4 | base02 | 2.467592146 | 27.848799 |
| 5 | base03 | 2.518013058 | 27.291152 |
| 6 | sample03 | 2.517587433 | 27.295766 |

| 指标 | baseline 中位数 | profile 中位数 | profile/base 变化 |
|---|---:|---:|---:|
| 整任务墙钟 s | 2.518013058 | 2.517587433 | -0.016903% |
| 整任务 Gbps | 27.291152 | 27.295766 | 不作为收益 |
| 接收进程 user+sys CPU s | 8.08 | 8.11 | +0.371287% |
| 发送进程 CPU s | 10.06 | 10.05 | -0.099404% |
| 接收 sys CPU s | 6.48 | 6.51 | 描述项 |
| 接收 minor faults | 2113340 | 2113351 | 描述项 |
| 吞吐 sample-stdev CV | 1.970916% | 0.038474% | 双组 <=5% |

三项绝对中位变化均 <=5%，CV 均 <=5%，**低开销门通过**。它只约束增加 perf 的影响，
不单独测定两组共有 /proc 采集器的开销。不得与 scope16 的 27.858476 Gbps 跨轮计算
回退或收益；本轮有共同采集器和明确的新 gate 计时合同，运行时二进制未变。

## 采样质量和热点

两台各自产生、在采集主机本地解码的三份 profile；固定二进制，保存 raw SHA、boot ID、
内核版本、解码器 SHA 和本地数据接口地址。每份小于 0.25 MiB，均达到 >=300 样本。

| 主机/角色 | sample01/02/03 样本数 | lost | 未解析物理叶样本 | 调用链深度中位数 |
|---|---|---|---|---|
| R770-1 接收 | 795 / 781 / 787 | 0 / 0 / 0 | 4 / 1 / 2 | 14 / 14 / 14 |
| R770-2 发送 | 956 / 980 / 952 | 0 / 0 / 0 | 9 / 6 / 6 | 11 / 11 / 11 |

下表是按采样 period 加权的**互斥在 CPU 运行路径占比**，不是墙钟比例；kernel 路径
仅归属一次。有多个 anchor 时单列，不与 Children 百分比相加。不同主机不可相加。

| 互斥类别 | 接收端三次范围 | 发送端三次范围 |
|---|---:|---:|
| kernel `shmem_fault` 调用链 | 29.10–31.95% | 0 |
| kernel 其他 page-fault 调用链 | 22.14–23.30% | 0.20–0.21% |
| kernel TCP I/O 调用链 | 20.10–22.14% | 39.08–42.47% |
| kernel pread 调用链 | 0 | 24.80–26.99% |
| kernel futex 调用链 | 1.64–2.29% | 3.66–5.51% |
| 用户态 CRC 物理叶符号 | 8.07–10.04% | 12.96–15.55% |
| 用户态 memcpy/memmove 物理叶符号 | 4.15–5.76% | 0–0.10% |
| 未解析物理叶 | 0.13–0.50% | 0.61–0.94% |

接收 kernel 物理叶总占比 80.54–82.01%，发送为 71.53–75.21%。接收的 fault
两类合计为 51.33–55.06%；其中 `shmem_fault` 样本的可见用户返回帧全部为
`__memmove_avx512_unaligned_erms`，其他 fault 样本每次仅一个没有该可见返回帧。
因此证据支持映射复制触发缺页的解释，但不能把其全部称为 shmem 分配的精确耗时。
8 GiB/4 KiB 为 2097152 页，接收约 211 万 minor faults 与之量级一致，不是独立因果证明。

物理叶 flat Self 与调用路径归因分开保留。接收 `do_user_addr_fault` Self 为
13.57–14.59%，`shmem_add_to_page_cache` 9.99–10.80%，`_copy_to_iter` 10.37–12.08%；
发送 `_copy_from_iter` 14.08–17.02%，`_copy_to_iter` 13.55–14.54%，
`kernel_init_pages` 7.35–8.16%。发送 stack 把后者定位到 TCP skb page-frag 分配，
不能误认成重新分配已驻留的源页；源进程 `read_bytes` 六次均为零。

`crc32cCombine` 物理叶 Self 为接收 7.04–8.01%、发送 8.47–11.66%。内联
`matrixTimes` 不能再作为额外 Self 相加；现有实现已经缓存 64 个字节幂矩阵，仍逐个
长度位应用矩阵。此处有 CPU 效率研究价值，但没有本轮受控证据表明它在关键路径上
足以支持 >=10% 完整任务收益，未据此改校验实现或语义。

源码核对：`src/storage/mapped_file_window.cpp` 的新窗口为普通 MAP_SHARED mmap，
随后 memcpy 到新输出；没有预热。本轮既不声称 RAM 带宽已饱和，也不声称跨 NUMA
是根因：两端各只有一个 NUMA node，NIC node0；系统 tmpfs THP 全局显示 never。
未改变 THP、sysctl、CPU 绑定、网络或服务。

## 失败、修正和限制

1. 初次 smoke01 在目标已退出后等待 disable ack 超时，perf 实际自动退出 0。
   修正 supervisor 的自然退出处理后，两端各 manual-stop/new-thread 和 natural-exit
   smoke 通过。首次 final resource gate 发现该失败 smoke 留下一个无引用 FIFO，
   检查拥有者、类型和打开者后精确删除，复验通过；失败证据保留。
2. raw perf 默认为 root/0600，最初归档 SCP 无权读取。仅对本任务已检查路径、大小、
   非符号链接的文件改为 Su 所有，保持 0600，之后 sudo perf 解码显式使用 -f。
3. 初次分析器把接口 error 增量视为零而拒绝输出；该假设被 sample02 的 +94 否定。
   改为显式记录网络事件、分别报告内容资源门与网络计数门，没有删样本。
4. 初次把 R2 raw 在 R1 解码，因不同主机内核符号上下文产生不可信 kernel 符号。
   那批派生输出隔离到 `acceptance/invalid-cross-host-symbols`，不进入结论。
   最终每台只在采集主机解码，再汇总 receipt 和 raw SHA；原始采样字节未改。
5. 分析器使用首个非 inline 物理帧做 Self，避免把 inline pseudo-frame 当成额外调用。
   六份 report 的样本总数全部匹配，已解析的 top Self 与 perf report 精确匹配；
   unknown 在 script 合并为 `[unknown]`、report 保留地址，明确不按名字强行匹配。
   `acceptance/analysis-contract.json` 保存检查结果。用户完整调用树仍受 FP 截断限制。

## 验收、资源和备份

- 互传前两端同输入提交、配置、版本和 10 个二进制 SHA-256；7 项脚本依赖 SHA 一致。
  `acceptance/input-builds-and-dependencies.json` 保存清单。
- fresh CTest 两端各 **10/10**，覆盖 mapped/config/fault、profile on/off/unobserved/
  pwrite、本地 V3 executor inline/queued、disabled detail、interrupted resume；
  11.56 / 11.82 s。范围内运行时未变，前轮两端各245/245为复用证据，非本轮重跑。
  实际列表与完整输出保存在实验根 `related-tests.txt` 和 acceptance CTest log。可重放命令：

  ```sh
  ctest --test-dir build --output-on-failure -R '^cpnetflux_(mapped_download_(config|fault)_smoke|mapped_profile_(on|off|unobserved|pwrite)_smoke|dynamic_download_local_executor_(inline|queued)_smoke|range_observability_disabled_smoke|v3_interrupted_resume_smoke)$'
  ```
- 六次均通过 8 ranges、139264 payload read/write calls、CRC、独立 SHA、无恢复、
  V3 协议终态和 8192 条认证 chunk 的 committed manifest。
  源 SHA `44942175bc67bb893589c02d3f41bc91bd308d4ae74ecdb14341302d4fe40d66`。
- 最低 MemAvailable **19.928761 GiB**；owned VmSwap 和 host pswpout 增量均为零；
  接收 time peak RSS 最大 262.122 MiB，采集器的轮询 owned RSS 最大 254.441 MiB。
  RSS 不包含全部 tmpfs 文件容量；两端各至多单个 8 GiB 源/当前输出。
- 所有输出校验后精确清理，R2 源在复核 SHA 后删除；最终两端本任务 tmpfs 根不存在、
  `/dev/shm` 使用为零，无拥有的工作进程、profiler/supervisor 或 FIFO 残留。
  生产 R1 PID/start `126766/1191040`、R2 `164834/1679976` 未变。
- 证据根 `/home/Su/experiments/CPNetFlux/2026-11-09-v3-kernel-service-17`；R1 汇总
  两端 profiles、receipts、routes/counters、smoke、CTest、清理结果和脚本；`analysis.json`
  为最终数值，`SHA256SUMS` 为证据哈希索引。raw/payload 不进入 Git。
- 范围提交已 push/readback。结果提交、GitHub remote branch SHA 回读、R2 fast-forward
  和最终构建/10 二进制一致性以 Git history 与 `acceptance/github-backup.json` 为准。
  未提交的两份 October14 文档保留；没有 agent、worktree、桌面控制或额外写入者。

## 下一步交接

本阶段：完成内核服务成本归因，性能门保持未通过。关闭继续盲调三阶段线程、队列、
映射尺寸和未达门候选的入口；不重开 population/direct-receive/sender-inline。
大文件只有出现可消除机制、保守关键路径收益预算和独立范围时才重启单候选验收。

按原宏观路线进入阶段4的**小文件固定开销基线**，尚未运行或实现微批：

1. 先用总数据 <=1 GiB、受相同内存守护约束的 native per-file 工作负载，覆盖
   4/16/64/256 KiB、1K/10K 规模的可行子集，分别记录 cold setup 和完整 T_ready。
2. 拆分 open/成员校验/manifest/checkpoint/commit/原子发布与 payload 成本；只有固定
   成本占主要部分，才设固定微批候选。保留成员路径、长度、CRC、失败隔离、成员恢复。
3. 固定批相对 tuned native、adaptive 相对固定批分别验收；目标 T_ready 中位数下降
   >=20%，同时检查 p95、恢复成本和大文件回退<=5%。未证实前不叠加自适应调度。
4. 把小文件收益与大文件未达门分别报告；RDMA、磁盘/持久化、反向/TLS 和生产稳定性
   继续作为独立开放门。接口 +94 需要后续只读关联证据，不能直接改系统网络归因。
