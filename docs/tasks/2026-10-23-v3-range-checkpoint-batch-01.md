# V3 range checkpoint 合并
输入提交：c9b96331eefa1b04037513658e0578559f5c5f3a。
范围：只减少 range 路径已验证块的 per-file manifest 原子保存频率；逐块数据 flush、CRC、租约、提交与 V3 格式保持。1/4/16 块单变量实验；100 ms 最早脏块定时触发；范围结果、连接正常停止/错误及文件提交强制补齐。后台失败保持粘性并禁止成功。
非目标：本轮不拆 Entry 锁、不增大 pipeline、不改网卡/TLS/CRC、不宣称断电安全或最终硬件达标。
设计：每批 context 共用一个惰性定时写者；任务使用 weak Entry 与 epoch，队列锁不跨 Entry 锁。Entry 锁仍串行保护 writer/session；后台使用自有统计并在前台边界归并，绝不保留 lane 栈指针。range 批量模式显式延后 session 的重复自动保存。数据仍逐块 flush 后才加入可恢复记录。强杀丢失未落盘记录，恢复只信磁盘 checkpoint。
受影响：persistent_tree_transfer.cpp/.h；TransferSession/DownloadSession；persistent_data_session_test.cpp；range observation smoke/perf harness；CMakeLists.txt；本任务证据及性能路线。
验收：先用现有二进制验证新行为测试为 RED；阈值、空闲定时、范围结果强制 flush、断开、保存失败粘性、双向 SIGKILL 恢复与独立 SHA；专项与完整 CTest；512 MiB TLS/CRC 四路 >=5 组交错 batch1/4/16，记录真实保存数、wall 和恢复窗口。仅稳定 >=10% 收益才采用新默认；100 ms 是触发时间，非慢盘完成承诺。
交付：深圳原目录单人修改；固定提交同步上海并构建核对二进制；GitHub push 回读；更新 Windows 性能优化路线.md。

## 结果与默认决策

运行代码提交：5acedb64dfd9482f33517c47f8bde2880d2d7ef3（已 push/readback）。
实现与恢复验收通过；默认调整 **NO-GO**：保留 batch=1，不把减少保存次数直接称为性能瓶颈已解决。
可用 CPNETFLUX_RANGE_CHECKPOINT_BATCH=1|4|16 做后续受控实验；无配置仍走逐块策略，非法值在创建 range 输出前拒绝。没有协议或 checkpoint 格式迁移。

每块依然数据写入/flush → CRC verified record；批量策略显式接管 session autosave。
最早脏块等待 100 ms 时由同一 context 的唯一 worker 触发；无后续包也会保存。
队列只保存 weak Entry/epoch；实际 serialize/rename 仍在 Entry 锁内，本轮不解除共享 writer 串行化。
range 完成/错误和连接退出强制 flush；后台失败保持粘性，修复路径后也不能回成功。
返回错误时析构补写不覆盖原错误；正常成功结果之前已显式检查 flush 状态。
后台统计归属于接下来的接收边界，background_checkpoint_cpu_seconds 与 lane CPU 分开。
不同线程累计计时会重叠，不能相加当 wall。

进程崩溃最多丢失未保存记录的一个批次窗口：正常未遇保存错误时至多 batch 个已验证块（包括阈值触发保存尚未完成的窗口），
4 MiB 块下 batch4/16 最多约 16/64 MiB 已验证数据重传窗口；还可能需要重传正在接收的未验证块。
100 ms 是触发等待上限，不承诺调度/慢盘操作完成时限；无 fsync 时不宣称断电安全。
上传/下载各测试 batch4/16：真实 fork 接收进程 SIGKILL，
磁盘仅有 4/16 个已验证块，另 3 块数据已写入但没有落盘记录；重启只复用磁盘记录，
实际 payload 精确等于 total - saved*chunk，最终字节全量一致。

### 性能：512 MiB、TLS/CRC、4 lanes、每候选 5 组交错配对

| 候选 | 对照进程 wall 中位 | 候选中位 | 改善 | manifest 保存中位 |
|---|---:|---:|---:|---:|
| 4 块 | 1.440674 s | 1.411763 s | 2.01% | 131 → 39 |
| 16 块 | 1.524425 s | 1.504412 s | 1.31% | 131 → 17 |

20 次全部独立 SHA-256 通过；两个候选均不足 ≥10% 且配对结果正负交错，不改默认。
batch4 的对照有 3.460 s 长尾，单对改善 64% 不能算稳定提升。
序列化字节中位约 41.8 万 → 13.1 万/6.5 万字节；逐块 writer flush 仍为 128 次。
batch4 组 checkpoint 累计中位 0.562→0.408 s，Entry hold 0.896→0.865 s；
batch16 组为 0.635→0.432 s、0.973→0.954 s。
manifest rename 累计中位只从 0.547→0.518 s、0.626→0.602 s，未按次数同比下降；
checkpoint 子计时不包括 commit 中的终态保存，故不得要求其大于全部 manifest rename 时间。
这些是累计墙钟子区间而非 CPU 时间。慢 rename 的环境/存储原因尚未确定，
也未证明实际吞吐差距都来自 Entry 锁；下一阶段需要隔离 writer 与 checkpoint 后再验证。

此机器仅 2 vCPU/3495 MiB RAM，本矩阵沿用 512 MiB 以避免 4 GiB 输入超过缓存容量；
最终 R730/R750 两种 4 GiB/小文件/mixed 的 GridFTP 对照仍归阶段 8。

### 验收命令与原始证据

- RED：旧二进制运行 python3 tools/test/run_cpnetflux_range_observability_smoke.py --build-dir /tmp/cpnetflux-pre-checkpoint-c9b9633 --checkpoint-batch 16，
  预期失败为 checkpoint still saved per chunk,35。
- cmake --build /tmp/cpnetflux-build-fd-release-01 -j2。
- ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j1 -R 'RangeCheckpointBatchTest|PersistentTreeRangeTest|cpnetflux_range_.*smoke'：
  深圳/上海均 18 项通过。
- CPNETFLUX_RANGE_CHECKPOINT_BATCH=16 CPNETFLUX_TEST_TOKEN=cpnetflux-test-token-only ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j1：
  深圳完整 440 项，无失败、1 项 io_uring 环境 skip，146.84 s。
- python3 tools/perf/run_cpnetflux_range_observation.py --build-dir /tmp/cpnetflux-build-fd-release-01 --evidence-dir /tmp/cpnetflux-checkpoint-perf-512m-01 --experiment checkpoint。
- 元数据/每 lane 实测/测试日志/跨机命令与临时 driver 源码：
  docs/tasks/2026-10-23-v3-range-checkpoint-batch-01-evidence.json；
  原始实验 /tmp/cpnetflux-checkpoint-perf-512m-01/result.json。
- /tmp/cpnetflux-checkpoint-{build-final,targeted,unit,ctest,perf-01}.log；
  上海 /tmp/cpnetflux-checkpoint-peer-{build,targeted}.log。

### 两服务器验收

传输开始前两端均为运行源码 5acedb64，上海原 625 个源文件逐个 SHA 核对无修改才同步；
新归档 SHA-256 cd72815164c2b16ff150259f9d101dde2d52b598456e5eeccd1ff8fed2841928。
两端分别构建，关键二进制 SHA-256 相同：

- upload：b2ea06292ff11d769cc121c946a3c4d19ae56fefc9b1e487b1dfab7fd745629c
- download：92f2597a833dce470b0b28ba507bdb37d7a6ad94e36ebf3be0024084f11aa310
- server：2e8b27f22905404fd52a4dbe5b8582faee20cf41e642a088693a62b974e16797

临时上海服务 47.116.174.181:24600，data base24610；深圳直接公网连接，未借 SSH 传 payload。
80 MiB 文件四路 V3、token auth、control/data TLS required、CRC32C Auto、compression off。
上传接收 batch16，下载接收 batch4：均成功，5 个 range/direction；
进程 wall 10.081/7.280 s 仅为功能验收记录，不是性能对比样本。
三处独立 SHA-256（深圳源、上海上传成品、深圳下载成品）
均 7a5392fa60c43fc45702188d3a30576905496bd78c373503038dd271adcdc88b。
接收端观察到保存 21/20 次及非零后台 CPU，说明低速跨网时定时保存实际触发。
临时 PID403780 已停止，24600/24610 窗口不再监听；原2121服务仍在。
仅删除本任务生成的 source/download/peer payload、私钥/token/证书；
perf512MiB source 也已删除，日志/manifest/JSON 保留；未清理别的项目。

## 下一步

当前剩 3 个执行门：阶段6 range独立writer/offset并行写、阶段7有证据的CPU热点优化、
阶段8最终硬件配对验收。阶段5是实现/正确性通过、默认性能候选 no-go；
不能将它标成最终性能目标完成。阶段6先用测试证明非重叠 range 真正可同时写，
保留 lease/attempt 屏障、独立 checkpoint 串行化，实测满足标准才采用。
