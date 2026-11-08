# V3 Observation Audit 16 结果：详情开关无稳定收益，确认完整任务基线

日期：2026-11-09。输入：`65093281c945eb6924fea4c00a77eedb9c9fe6e6`。
范围与正式实测提交：`25ff8eed56fd88facdfbd4df4341567a23f4019d`，已推送并回读 GitHub SHA。
该提交仅新增任务范围，传输源码和二进制未改；不是新的算法候选。
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`。R770-1 单写，R770-2 固定提交验收。
任务范围：[Observation Audit16](2026-11-09-v3-observation-audit-16.md)。

## 结论

完成五对真实8GiB完整V3文件任务。既有 range/payload 详情观测 ON/OFF 的吞吐中位数为
**27.611519 / 27.858476 Gbps**，中位数之比相差 **+0.894399%**；逐对变化中位数仅
**+0.243070%**，两对退步、三对改善。两组样本标准差 CV 为 **1.856872% / 2.396810%**。
这些结果不支持“关闭这两个详情开关能稳定解决吞吐瓶颈”，不宣称算法提速或修改默认策略。

本轮得到稳定的 detail-off 完整任务基线约 **27.86Gbps**；这是 TCP、驻留 tmpfs 源、
显式实验 mmap 接收、非持久化、TLS-off 的限定配置，不是默认/生产/磁盘性能。
**三阶段工程已收口，30–50Gbps完整任务性能门仍未过，100Gbps未达到。**
不以测量差异重开直接接收、population、sender-inline，也不继续扩线程或队列。

## 对照合同和计时口径

R770-2 -> R770-1，4路 TCP，明确 bind `192.168.100.2/.1`，路由均为 `eno17095np0`。
两端同提交/配置/版本，10个二进制 SHA-256 一致；构建启用 RDMA 不表示本轮使用 verbs。
传输日志没有本轮原生 QP 初始化，系统已有 QP 可能来自保留的生产服务，不声称全机 QP 为零。

- 单个真实8GiB源文件：随机8MiB块重复1024次，compression off，发送前/后全驻留。
  每次输出全新，不预热；queued sender / inline mmap receiver，8个1GiB range。
- 每 lane 最多一个64MiB映射，总上限256MiB；read0/write0、payload预算64MiB、batch1/depth2。
- CRC32C、verified_chunks、manifest every16、range checkpoint16、commit_sync=none、preallocate off。
  scope13本地WorkExecutor修复在两个实验臂中均存在，未删租约、校验、checkpoint或发布。
- ON：`CPNETFLUX_RANGE_TIMING=on`，`CPNETFLUX_RECEIVER_PROFILE=1`；OFF：`off/0`。
  sender CPU / receiver CPU / mapped-write profile 三个新开关始终为0，两臂 JSON 对应值均 null。
- 基本 phase timing 和稀疏协议边界 trace 两臂同开，整任务分母为真实客户端启动至退出的
  单调 wall，包含建连和最终提交。源预读/摘要、独立输出 SHA、实验管理和清理在分母外。
  不把累计 CPU、各 range wall 或网络活跃时间代替完整任务时间。

detail-off **不等于所有观测成本消失**：例如当前通用 `Timer` 构造仍初始化 Clock::now，
且基本 phase timing 仍启用。本轮仅评估上述两个现有开关的实际效果，不能推出所有
时钟/原子统计开销均可忽略，也没有证据把残余观测成本当成主要瓶颈。

## 五对正式数据

顺序：ON/OFF、OFF/ON、ON/OFF、OFF/ON、ON/OFF；所有10个任务均完整通过，无排除样本。

| 对 | detail ON Gbps | detail OFF Gbps | OFF相对ON |
|---|---:|---:|---:|
| 1 | 28.385518 | 28.454514 | +0.243070% |
| 2 | 28.212763 | 27.448864 | -2.707636% |
| 3 | 27.512124 | 26.763799 | -2.719979% |
| 4 | 27.611519 | 27.858476 | +0.894399% |
| 5 | 27.139698 | 28.194461 | +3.886422% |

| 指标，中位数 | detail ON | detail OFF |
|---|---:|---:|
| 整任务吞吐 Gbps | 27.611519 | 27.858476 |
| 吞吐 CV，sample stdev/mean | 1.856872% | 2.396810% |
| 整任务 wall 秒 | 2.488797 | 2.466735 |
| 接收进程 user+sys CPU 秒 | 8.11 | 8.04 |
| 发送进程源状态采样区间 CPU 秒 | 9.93 | 9.76 |
| 双端 CPU 秒/GiB，逐样本求和后取中位 | 2.250000 | 2.238750 |
| 接收进程 user CPU 秒 | 1.67 | 1.61 |
| 接收进程 sys CPU 秒 | 6.48 | 6.43 |
| 接收进程 minor faults | 2111490 | 2111487 |
| 接收进程 major faults | 0 | 0 |

接收 CPU中位变化 -0.863132%，双端 CPU/GiB -0.5%；没有明显的 CPU效率突破。
表内不同列分别取中位，user/sys中位不必恰好相加为总CPU中位；CPU也不能从wall直接相减。
发送CPU来自常驻实验服务在任务前后 /proc/stat 差值，接收来自 /usr/bin/time，
不是与独立SHA混在一起的整个实验脚本CPU。

接收系统态约6.4秒、minor faults约211万的现象仍在。结合scope14/15结果，后续应优先
区分首次写入、内核数据搬运及并发访问的服务成本；目前没有证明某一个是唯一根因。
不能把全部sys时间计作缺页，不能拿scope15的38.35Gbps帧诊断与本轮完整文件率计算收益。

## 正确性、测试和资源

- 两端相关 CTest **各245/245通过**，精确名单为 `related-tests.txt`，未截断含空格名称。
  含关闭观测、dynamic download本地executor的inline/queued、mapped off/unobserved、
  映射故障、lease/attempt、断线/超时/服务重启恢复等；这是相关测试，不是全量649项声明。
  两端 CMake build成功；R1/R2 CTest wall分别102.45/103.30秒。
- 每个任务 actual_transport=v3、data_transport=tcp、8GiB/单文件/4连接，恢复次数0；
  DATA读取/写入均139264次。协议 FILE_RESULT/DirectoryEnd 正常结束，独立输出 SHA 与源一致。
- ON的详情计数为正；OFF的range开关false、payload详情null、相关计数为0，但实际映射
  写入8GiB、map/unmap各128次、结束active=0。十份保存的 manifest 均 committed、
  completed_ranges覆盖0..8GiB，含8192个连续1MiB CRC32C记录；未以null统计代替执行证据。
  最终manifest证明保留的认证进度/提交状态，周期checkpoint故障语义另由既有恢复门覆盖。
- 十次源文件前后全驻留、inode相同，发送进程 read_bytes 增量均0；输出 SHA前也全驻留。
  明确bind、route、双方接口数据量与真实传输一致，所有样本接口error/drop/collision增量0。
- 20份双端内存守护：最低 MemAvailable **19.971825GiB**，门为12GiB；
  最大轮询owned RSS **257.882813MiB**，接收 time最大RSS **259.589844MiB**。
  tmpfs源/输出页单独占用，不包含在该RSS数字中；每端最大一个8GiB payload。
- owned VmSwap全0、host pswpout增量全0；没有清理历史swap或修改系统配置。
  每个输出均SHA后删除，源最终再次SHA后删除。两端本轮tmpfs根不存在、/dev/shm used=0、
  无本轮传输/守护进程；生产PID/start ticks 126766/1191040、164834/1679976不变。
- 原两个October14未跟踪文档保留，没有改其他项目、生产服务、网络、THP或亲和性。

前置依赖核验发现两端历史seed helper不同，本轮没有覆盖旧证据，也没有调用该helper；
生成器直接固定在本轮wrapper，保持上述源形状。正式wrapper/complete-task helper/
baseline helper/memory guard四项依赖摘要核对一致。正式10个样本无失败。
收尾检查器曾因把自身命令当作owned进程、嵌套字符串和主机显示名假设报错；
改为独立脚本、参数边界匹配和明确主机标签后双端核验通过，不改写为传输失败或性能样本。

## 复核、产物和下一步

证据：`/home/Su/experiments/CPNetFlux/2026-11-09-v3-observation-audit-16`。
`observation_matrix_16.py` 是固定配置的复现wrapper，依赖完整scope12任务helper；
`analyze_observation_16.py` 对10份receipt、驻留、实际字节、资源和接口错误逐项断言，
输出 `analysis.json`；`final_resources_16.py R770-1|R770-2` 检查精确清理和生产身份。
`acceptance/input-builds-and-dependencies.json` 保存实测输入，
`acceptance/final-resources-*.json` 保存收尾，`SHA256SUMS` 保存小证据摘要。
复核命令：`python3 <证据目录>/analyze_observation_16.py`。
测试命令：`ctest --test-dir build --tests-from-file <证据目录>/related-tests.txt --output-on-failure -j1`。

范围提交已GitHub备份；结果与路线图提交后，远端SHA回读和R2同步结果另存
`acceptance/github-backup.json`。原始证据存服务器，不冒称已随Git备份；不提交大payload。

1. **scope16测量复核完成**。保留27.86Gbps限定配置的detail-off基线，不推广详情关闭为
   性能优化；不重开scope15集成或三阶段线程/队列扩张。完整文件性能门仍标未通过。
2. 下一独立诊断聚焦非RDMA内核/内存I/O服务率：先用低频perf采样和/proc的缺页、
   上下文切换/迁移等证据，将系统态成本分成页分配/首次写入、TCP复制和源pread/搬运等。
   仍用新8GiB目标与双端内存门；先做采样开销控制，不将profile样本算作正式吞吐。
   此步尚未执行，当前不能把约6.4秒sys或约211万minor faults归为唯一根因。
3. 只有得到明确可消除机制、预期预算足够时，才单独立一个有界实现候选及>=10%的
   完整任务对照门。若没有这样的证据，则暂停大文件候选开发，记录性能门未过，
   再决定其他处理路径或独立的小文件微批研究，不以换题宣布100G问题已解决。
4. RDMA稳定性、持久化磁盘、TLS/反向/长时间、跨域和小文件仍是独立未验收方向。
   100Gbps保留为目标，无证据承诺可达。本轮没有修改运行时源代码。
