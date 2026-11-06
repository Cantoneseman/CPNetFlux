# V3 Receiver Profile 01 结果

日期：2026-11-06。输入 `a3ed7e037fbd4c1794b27efbb6f8309aaeb9983d`，诊断实测提交 `bdc82c13b10806d0a4c0b86c74153bab67639ca0`。R770-1 单人串行通过 SSH 开发，R770-2 固定提交构建验收。根据用户新要求，后续常规软件实验统一使用内存源和目标，带显式内存预算。

## 结论

接收端存在确定的逐帧分配和冗余复制，但本组观测中实际写调用耗时更大。每个 1 GiB 文件有 17408 次帧 payload vector 分配和 17408 次 logical vector 分配；复制逻辑副本 1073811456 字节，移除 CRC 前缀再移动 1073741824 字节。计数来自当前每帧新建 vector 的实际容量与执行点，不是 malloc 全局采样，不含控制帧、解压器和 RDMA 注册池。

三次开启细分观测的中位数：副本复制 23.769 ms、前缀移动 14.395 ms、帧 CRC 20.388 ms，实际 writeAtAll 233.351 ms。接收文件阶段为 487.284 ms。写调用约占这个文件阶段的 47.9%，其内部包括内核拷贝、tmpfs 页分配/记账等；这不是对内核子项的单独测量。下一实验优先比较已有的有界写合并，然后分别实施 logical span 去副本和读帧缓冲复用。

仅替换为内存文件没有使软件达到 100G。本轮接收文件阶段约 17.6-17.7 Gbps，含冷建连的整 CLI task 约 3.05 Gbps；文件阶段含提交/结果处理，不等于网络线速稳态。阶段累计计时嵌套，不能把 copy/shift/CRC 再加到 checksum_seconds，也不能把 writeAtAll 再加到 write_seconds。

## 诊断实现与测试

新增 `CPNETFLUX_RECEIVER_PROFILE=1`，仅在 phase timing 开启时记录 `receiver_payload_observation`，默认关闭并输出 null。分离 DATA 帧容量、logical 副本、前缀移动、解压、帧 CRC、whole-file writeAtAll 的计数/计时；range writer 的实际写入继续使用已有 range observation，不把零计数解释为 range 没有写入。未改变接收算法、CRC、帧格式、默认队列/策略或注册池。

用真实发送帧的独立字节 oracle 检查 CRC/none、压缩/原始、观测 on/off 八种组合，断言复制字节、移动字节、帧/写调用数量及发布内容；旧代码因缺观测字段而失败，新代码 14 项 framed socket 测试通过。R770-1 全量 CTest 542/542 通过，串行 258.61 秒；R770-2 相关 CTest 146/146 通过，串行 49.78 秒。无跳过。两端各三项内存守护测试通过，包含真实 pidfd 终止匹配的子进程、保留相邻路径和拒绝过期进程身份。

`perf stat -e task-clock,page-faults true` 因 perf_event_paranoid=4 被拒绝。未调整系统权限，使用现有阶段接口、进程资源和 /proc 观测，不声称有 perf 火焰图或内核采样归因。

## 内存实验条件

两端 tmpfs，单 lane、单文件 1 GiB，CRC32C、压缩 off、every_n_chunks/16、commit_sync=none、payload depth=2。同一随机 8 MiB 块重复 128 次的源，固定 inode；源/目标均保留真实文件读写、V3 协议、校验和发布流程。数据使用显式 `192.168.100.1` / `192.168.100.2` 绑定，路由为 `eno17095np0`，保留原生 verbs QP 和摘要证据。控制面仍为 TCP。

常规启动门为每端 MemAvailable >=12 GiB、tmpfs 可用 >=2 GiB；一次仅一个源、一个输出。复用的注册资源包含每端 2048+32 个接收槽，每槽 1052672 字节，约 2.04 GiB，另有发送池；数据文件并不是全部内存占用。

新增可复用脚本 `tools/perf/cpnetflux_memory_guard.py`：采样匹配的项目二进制及本轮绝对 payload 路径，合计 RSS >4 GiB 或主机 MemAvailable <6 GiB 时，仅对仍匹配启动身份的 PID 发送 SIGTERM。使用 pidfd 避免检查后 PID 复用；记录 RSS/HWM、VmSwap、locked、CPU ticks、缺页、I/O、主机 swap 和 tmpfs 占用。采样循环睡眠 50 ms，包含扫描开销，并非内核硬配额或严格 50 ms 最大间隔，不能保证捕获所有瞬时峰值。负向检查将最低可用内存设为 64 GiB，正确拒绝并记录 violation，未向任何进程发信号。

## 正式测量

同一提交下交替运行 on/off 各三次，顺序 on/off、off/on、on/off，再补一次 full 控制。每次都检查八个二进制 SHA-256、配置和版本一致。开启/关闭只是诊断开销对照，不是接收算法优化 A/B。

| 指标，中位数 | Profile Off | Profile On |
|---|---:|---:|
| CLI task wall，秒 | 2.817690 | 2.817835 |
| CLI task rate，Gbps | 3.048573 | 3.048416 |
| task rate CV，% | 0.846478 | 0.848840 |
| 接收文件阶段 wall，秒 | 0.484590 | 0.487284 |
| 接收文件阶段 rate，Gbps | 17.726173 | 17.628184 |
| data connect，秒 | 2.088880 | 2.095420 |
| 接收 lane CPU，秒 | 0.482773 | 0.486187 |
| readNext 聚合 wall，秒 | 0.131644 | 0.133186 |
| write 聚合 wall，秒 | 0.232376 | 0.234485 |
| checksum 聚合 wall，秒 | 0.089479 | 0.088763 |

开启组接收文件阶段速率约低 0.55%，lane CPU 约高 0.71%；小样本只能说明本次没有观察到大的诊断扰动，不能证明零开销或精确开销比例。冷建连仍约 2.1 秒。full 控制一次为 2.917877 秒、2.943899 Gbps，finalize 0.124913 秒；不作为重复 full 性能成绩。

| 细分项，Profile On 中位数 | 数量/字节 | 耗时 |
|---|---:|---:|
| DATA 帧缓冲 | 17408 个，累计容量 1073811456 字节 | 未单独隔离分配/清零；位于 readNext 聚合中 |
| logical 新副本 | 17408 个，复制 1073811456 字节 | 23.769 ms |
| CRC 前缀移动 | 17408 次，移动 1073741824 字节 | 14.395 ms |
| 帧 CRC | 1073741824 字节 | 20.388 ms |
| whole-file writeAtAll | 17408 次，1073741824 字节，失败 0 | 233.351 ms |

累计 vector 容量是分配工作量，不是同时占用约 2 GiB 的应用缓冲。RdmaDataSocket::readSome 仍从注册槽 memcpy 到帧缓冲，之后还有上述副本/移动和 pwrite 的内核拷贝，未实现零拷贝。未启用压缩的正式组不包含解压开销；压缩由功能回归覆盖。

七个正式样本全部通过源/目标页驻留、CRC、外部 SHA-256、FILE_RESULT/DirectoryEnd/控制终态和原生 QP 检查，无恢复。发送进程物理读取全部为零。外部 SHA 与计时前源预热在 CLI 计时外，所有目标均为新建空页；没有通过预先写满目标来隐藏首次写入成本。

## 实际内存与链路记录

| 全部七个正式样本 | R770-1 | R770-2 |
|---|---:|---:|
| 实验进程 RSS 采样峰值 | 2.06797 GiB | 2.06792 GiB |
| 主机可用内存采样最低值 | 25.46628 GiB | 25.07247 GiB |
| tmpfs 使用峰值 | 约 1.00004 GiB | 1 GiB |
| 实验进程 VmSwap 采样峰值 | 0 | 0 |
| 主机 swap-out 增量 | 0 | 0 |
| 内存守护 violation | 0 | 0 |

R770-2 有两次主机级 swap-in 增量为 1 和 12 页，其余为零；对应实验进程物理读取仍为零、VmSwap 为零、源页全驻留。全局计数不能归属给本轮 payload，不将已有后台进程的换入当作源数据读盘，也不声称整台机器完全无磁盘活动。日志和元数据仍可写普通文件系统。RSS 与未映射 tmpfs 文件是不同记账项，本轮每端规划占用约 3.1 GiB，而非仅看 1 GiB 文件大小。

全部正式样本 RNR/out_of_buffer 增量为零；CQ response error/flush 仍出现 1/0、65/64、193/192，继续保留生命周期归因和稳定性门，不能因完整传输成功视为无害。

## 新页写入辅助诊断

原生传输结束后，在 R770-1 单独使用一个 1 GiB tmpfs 探针文件，每轮首次写新页再覆盖已驻留页，共三对。每个 GiB 严格按 1 MiB chunk 边界拆成 16x65532+64 字节，17408 次 pwrite，复刻生产帧写入大小；没有网络、CRC 或清单。两阶段前后检查驻留率、进程物理 I/O、内存预算和 SHA，探针 RSS <1 GiB、主机可用内存 >=6 GiB。六次均物理读写为零、无 swap-out、SHA 通过，逐对删除。

首次写中位数 0.225910 秒（38.023735 Gbps 逻辑写服务率），覆盖驻留页 0.153263 秒（56.046899 Gbps）；系统 CPU 为 0.217660 / 0.144197 秒。覆盖约少 32.16% wall，支持调查页建立/记账等内核写成本，而非把所有成本归给用户态复制。Python 调用开销与固定 fresh-then-resident 顺序仍在；该探针不能直接代表文件传输性能，不能从差值推算整个任务加速。初次探针因 findmnt 未使用 -T 在创建 payload 前失败，保留 setup 日志，修正后六次完整门通过。

不能靠将目标预填充移到任务计时外来宣称优化。未来若引入预分配/预触页，必须把准备时间与内存成本计入完整 task。

## 下一串行阶段

1. 先利用已有 `--file-io-buffer-size` 与 `--posix-write-strategy coalesced`，以 direct 为对照，测试 256 KiB / 1 MiB 有界 whole-file 写合并；每 lane 最多增加 1 MiB，仍固定 CRC、1 MiB chunk、checkpoint、commit-sync 和新建目标条件。验证实际写调用数、复制代价、CPU 与完整任务，而不是仅降低调用数。先查真实 chunk flush 边界，不提前承诺收益。
2. 独立改为原始 DATA 的只读 logical span，解析 CRC 前缀而不移动 payload；压缩保留有界自有解码缓冲。CRC 验证先于写入、调用返回前不得回收所有权；用独立 wire/损坏/恢复回归验收。
3. 再独立复用读帧缓冲，限制为每 lane 至多一个最大帧容量，避免逐帧分配/清零；覆盖短帧、控制帧、压缩和取消。RDMA 借用注册槽属于另一个所有权设计，不能混入此步。
4. 冷连接接收池分配/注册/预投递及连接复用、CQ teardown 保持独立任务。维持单 lane、1 GiB 和内存守护；增加并发或数据规模前重新预算。

## 证据与清理

证据根：`/home/Su/experiments/CPNetFlux/2026-11-06-v3-receiver-profile-01`。包括 observation-red/framed-green、全量/对端 CTest、memory-guard-tests/guard-negative、matching-builds、七份 verified metrics、双端内存 JSON/JSONL、源状态、协议/路由/网卡快照、comparison.json 和 write-probe.json。采集/验收/辅助探针脚本随证据保留，原始 payload 不入 Git。

可复核命令：`cmake --build build -j4`、`python3 tools/test/test_cpnetflux_memory_guard.py`；证据根 `validate.py <本机RDMA地址> <新日志名> .` 为串行全量，R770-2 实际使用筛选 `Framed|Persistent|Bounded|Checksum|rdma|whole_file|compression|receiver_policy`。重新实验需创建新证据标签和源，再执行 `experiment.py profile-series <固定SHA>`；守护日志采用独占创建，不覆盖已有记录。

源 SHA-256 `0d1b3d3de59001debf90c4531d8e83bbfdbaec16cab46b5572e196861497a5b3`，inode 13，删除前再次核对。输出、探针及最终源全部清理，双端 `/dev/shm/cpnetflux-v3-receiver-profile-20261106` 均不存在；原生产 PID 126766/164834、内核 GSI QP 及两份历史 untracked October 任务文档保留，无本轮实验进程。最终文档提交、双端同步、八个未变二进制摘要和 GitHub SHA 回读见外部 `acceptance/github-backup.json` 与 `HANDOFF.md`。
