# V3 Mapped Cost 14 结果

日期：2026-11-09。输入提交：`84143373b2e22a9876ad3d7dfc194535e55b8692`。
源码提交：`9d2189b4fc6c4facff491fa52b425519b621e817`，沿用
`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`；R770-1 单人串行开发，R770-2 只同步构建验收。

## 结论与进度

本轮完成真实映射写路径的 CPU/缺页细分和三组匹配的完整文件观察者对照。
**没有推出新的运行时吞吐候选，也没有新的吞吐提升通过验收。**
三阶段工程已完成，当前仍处于大文件接收处理路径的性能门；30–50、70–85、100 Gbps
目标均未通过。CPU 下降、热页诊断和局部写入成绩都不能替代完整文件验收。

现在可以排除“主要开销在 mmap/munmap 管理”这个优先假设：完整 8 GiB 任务中，新增
三个 CPU 包装区间的中位数分别是 map 0.000710 s、unmap 0.147002 s、copy 4.617723 s。
copy 约占这些区间之和的 96.90%，约占接收 lane CPU 的 58.42%。copy 是包围 memcpy
调用的区间，**包含其触发的缺页、页分配等成本，不是纯 memcpy 指令成本**；本轮不能
据此定位到具体内核函数，也不能把某个累计 CPU 秒数直接从任务 wall 中扣除。

同一 live mapping 的本地冷/热对照进一步支持调查首次触页与复制路径，但不足以证明
仅消除复制就能达到某个吞吐。scope10 的提前 population 负结果继续关闭；不再叠加
worker/队列，不改变 THP、NUMA/CPU 亲和性或系统网络设置。

## 实现

- 实际 `MappedFileWindow` 增加默认关闭的 map/unmap/copy wall 和
  `CLOCK_THREAD_CPUTIME_ID` CPU。关闭时没有这些 clock/getrusage 调用；复制直接走原 memcpy。
- `CPNETFLUX_MAPPED_WRITE_PROFILE=1` 仅在实验 mmap 后端且 range observation 有效时开启。
  其他情况 JSON `mapped_write.profile=null`，不制造零值“观测”。
- 每次复制周围用 `RUSAGE_THREAD` 读取 minor/major fault 增量；计时/计数错误显式导出。
  getrusage 本身在 copy CPU 包装区间外，仍计入完整任务。缺页计数含括号内仪器代码，
  CPU 时钟不是 getrusage 的细粒度 CPU 差分。复制次数/字节表示尝试，不给失败任务认证。
- JSON `cpnetflux_mapped_write_profile_v1` 写明边界：map/copy 嵌套于 physical write，
  最后一次 unmap 可能发生在 flush，不把它全部误算成 physical write 的子区间。
- `cpnetflux-mapped-cost-bench` 直接调用同一个生产写类。四 worker/每个 64 MiB 窗口，
  先对新 sparse 页写，再不释放映射进行覆盖写；每次内容不同，分别通过 pread 逐字节校验。
  最终 unmap 单独记账。工具最多 8 GiB、四个活动窗口，不是另写一个映射实现。
- 无运行时协议、CRC/租约、checkpoint、恢复、发布、默认后端或并发配置变化。

## 先红后绿与功能门

旧 `84143373` 二进制在真实 V3 下载中按预期失败：
`AssertionError: missing mapped write profile contract`；原始失败日志保留。
新增单元覆盖关闭计数、冷/热内容、跨窗口拆分、越界/空写、mmap 失败和幂等 close；
真实 loopback 覆盖开启/关闭/观测关闭/非 mmap 的可空 JSON、完整性及本地执行器租约。
诊断 smoke 还覆盖六个错误参数和两种 worker 配置的字节/生命周期/清理。

- R1 聚焦 12/12；R1 相关 **226/226**，102.12 s。
- R2 相同相关 **226/226**，103.07 s。
- 包含写入故障、范围生命周期、压缩、观测契约、中断/服务重启/超时恢复。
- 两端同源码提交、CMake 配置/版本、原有八个二进制 SHA-256；新增诊断工具摘要也一致。
  传输前脚本逐任务重新核对原有八个程序，未使用 dirty 源码互传。
- 首次额外摘要记录命令未执行：嵌套 SSH 继承 bash-s 的标准输入，消费了后续命令。
  已用独立命令和 `stdin=DEVNULL`/`ssh -n` 补验，原八程序门一直有效。
  后续编排不把带 SSH 子进程的脚本与依赖它的命令串在同一 stdin 流中。

## 六次完整 V3 文件任务

R770-2 `192.168.100.2` -> R770-1 `192.168.100.1`，显式 local bind，接口
`eno17095np0`。8 GiB 驻留 tmpfs 源、新建目标；四 TCP lanes、八个 1 GiB ranges；
queued sender、inline/64 MiB mmap receiver、read0/write0、depth2、65536 字节帧缓冲；
CRC32C、verified_chunks、checkpoint16、preallocate off、TLS off、commit_sync none。
源是一个随机 8 MiB 块重复 1024 次，压缩关闭。不是持久化或安全生产成绩。

既有 range/payload detail 观测两组都开，scope11 sender 和 scope13 receiver CPU 时钟
两组都关；仅新增 mapped profiler 切换。三对次序 0/1、1/0、0/1。
任务时钟包含建连到进程完成，独立 SHA 在该时钟外；不能把该结果称为默认 lean 性能。

| mapped profiler | 三次完整任务 Gbps | 中位 Gbps | CV | 任务 wall 中位 s | 接收进程 CPU 中位 s |
| --- | --- | ---: | ---: | ---: | ---: |
| off | 27.932493 / 27.321662 / 28.272432 | 27.932493 | 1.412844% | 2.460198 | 8.07 |
| on | 26.969732 / 27.647526 / 27.764409 | 27.647526 | 1.275756% | 2.485556 | 8.21 |

观察者 task wall 中位差 **+1.030717%**、接收进程 CPU **+1.734820%**；仅是这三对
中的测量差异，不能宣称零开销或统计上精确的固定成本。关闭仍是默认。
本轮 off 27.932 Gbps 与 scope13 off 27.602 Gbps 不是跨版本随机配对，不构成提速证明。

每个 profile-on 完整任务：139264 次 copy、8589934592 字节；128 次 map/unmap，
完成时 active mapping 为零；copy 内 minor fault **2097152**、major **0**、观测错误 **0**。
接收进程总 minor fault 中位 2111499。计数与 8 GiB/4 KiB 对齐是关联证据，不能据此
把全部 CPU 唯一分配给缺页处理。

## 生产写类的本地同映射对照

三次每阶段 8 GiB、四 worker、窗口 64 MiB；每个窗口 cold 写、pread 校验、同映射 warm
覆盖、再次 pread 校验、unmap。每阶段均为 139264 次 copy，最终写入量合计 16 GiB，
文件占用最多 8 GiB；两次写、所有校验、全部准备和回收都保留在 whole-diagnostic 计时内。

| 区间 | 累计 copy CPU 中位 s | copy wall 中位 s | copy minor fault | copy major fault |
| --- | ---: | ---: | ---: | ---: |
| cold 新页首次写 | 4.925466 | 4.952359 | 2097152 | 0 |
| 同一映射 warm 覆盖 | 1.935209 | 1.961376 | 0 | 0 |

warm copy CPU 约少 60.71%；含验证的整体诊断 wall 中位 2.673201 s、进程 CPU 中位
10.370773 s。并发累计区间不能与整体 wall 相加。warm 之前已写入、校验，文件页/PTE、
缓存和可能的放置状态都不同；不是纯缺页差分，也不是可以省略首次写的传输方案。
没有预热任何真实传输目标，不报告 warm 的换算带宽为文件性能。

## 完整性、资源和证据

六次任务的字节、V3 FILE_RESULT/DirectoryEnd、独立 SHA、源前后全驻留、路由/NIC
计数、资源及逐次输出清理均通过；发送进程物理 read_bytes 都为 0。没有原生数据 QP，
这是 RDMA 网卡上的 TCP，不称为 verbs。实验输出、sidecar 和最后一个源均已清理。

六个互传及三个本地诊断的最低 MemAvailable **19.974567 GiB**；最大轮询 owned RSS
**268.214844 MiB**，另计 tmpfs 文件页；owned VmSwap 和主机 pswpout 增量均 **0**。
两端最终 `/dev/shm` 归零、无本轮传输残留，生产 PID 126766/164834 及 October 30
启动时间保留；两份 October14 untracked 文件保留。没有操作其他项目或历史数据。

证据根：`/home/Su/experiments/CPNetFlux/2026-11-09-v3-mapped-cost-14`。
主要产物：`mapped-analysis.json`、`matrix.log`、`metrics/*.verified.json`、
`metrics/local-diagnostic-*`、`acceptance/*`、`R770-{1,2}/logs` 和 snapshots；
`provenance.json` 固定依赖/库摘要，`evidence-sha256.json` 校验原始证据。
payload、凭据、构建产物和大实验日志不提交 Git。

## 下一步与停止边界

本轮不启动“扩大映射/减少 mmap 次数”候选：实际映射管理 CPU 不是当前首要成本；
不重开 scope10 population、scope12 inline sender，也不再扩大三阶段结构。

下一独立 scope 先写明一个候选的设计和正确性契约：**TCP 帧负载能否直接接收至有界
映射窗口，减少接收缓冲到目标映射的一次额外复制**。先检查真实帧解码/CRC 前缀、
offset 和 range 边界，再定义写 lease/pin 的获得/释放时机、跨窗口、短读/截断/CRC 失败、
取消/失联和 SIGBUS 隔离；未经 CRC/租约认证的临时字节不能更新 checkpoint 或发布。
保持 V3 唯一路径、64 MiB 窗口和原预算；压缩/TLS 等不支持组合必须明示边界，不能暗中
跳过校验或回退旧协议。首次触页、接收、校验、回收成本全部计时，不能拿 warm 写替代。

先做使用真实 TCP/framing 的有界可行性对照；局部无收益即关闭，不急于侵入 V3。
只有机制和故障门通过，才实现显式实验入口，再以五对完整 8 GiB 文件、>=10% 中位收益、
CV<=5%、同版本配置/摘要及完整资源/清理门决定保留。**没有证据承诺 30/70/100 Gbps。**
这只是一项待验证假设，不记作已实现、已派发或已通过性能门；后续没有稳定收益就关闭
该方向，保留现有结果，再单独决定其他处理路径/小文件微批，不能用它替代大文件目标。

复现相关功能门：

```sh
cmake -S . -B build
cmake --build build -j 6
ctest --test-dir build --output-on-failure -R 'Persistent|BoundedPayload|TransferStages|WorkExecutor|DynamicFileQueue|HotPathCompression|sender_(cpu|inline|execution)|receiver_cpu|dynamic_download_local_executor|range_.*observability|range_observation_export|mapped_download|MappedFileWindowTest|mapped_profile|mapped_cost|tree_resume|v3_.*resume'
```

本地诊断和完整任务必须分别通过对应 runner 的空根、固定提交、可用内存、空间及重复
文件名检查；不要直接重跑现有 evidence 目录覆盖历史。最终提交/远端 SHA 回读记录在
`backup-readback.json`，两端最终清理记录为 `cleanup-R770-{1,2}.json`。
