# CPNetFlux 大文件后续开发计划

日期：2026-11-09。输入提交 `7dbaa06fcc8d70dd5e1f9cdfc9df9e5dca727ea1`。
状态：Byte Work18及Mapped Collapse19诊断已完成，当前LF4 seed+collapse不晋级，下一项LF5搬运成本核对；大帧/CRC运行时预算未证实。历史计划不作为性能结果。
依据：[当前路线](../ROADMAP.md)、[scope17结果](2026-11-09-v3-kernel-service-17-result.md)、
[本轮路线决定](../DECISIONS/2026-11-09-v3-large-file-candidate-screening.md)。

## 2026-11-09 路线合并更新

用户已授权合并路线并开始开发。更新输入`cf7ea20d5c4f12b90c4b145e0af62434ee8e5823`。
此前“本轮只做文档”是初版计划交付范围，已完成，不再限制后续实现；本次首个开发范围见
[Byte Work18](2026-11-09-v3-byte-work-18.md)。执行优先级为：

LF0成本核对与诊断开发 → LF1/LF2大帧（条件）→ LF3 CRC算子（条件）→
LF3C组合验证（条件）→ LF4接收页面管理可行性与独立候选 → LF5数据搬运路径 → S0小文件。
最多两个机制的限制只约束这一轮逐帧成本筛选，不取消用户新要求的页面/搬运研究。
页面和搬运必须各自提出改变实际工作量的新机制、范围和收益预算，不能换名重开旧候选。
小文件是独立研究目标；当前大文件为优先级，不把小文件收益替代大文件缺口。
RDMA仍暂停，未来若恢复先另立完整文件生命周期范围。

## 初版计划交付范围（已完成）

用户要求明确大文件是否还有方向，以及后续详细开发安排。先为新机制设置有限筛选，
不把上轮暂停无预算候选解释为永久停止大文件研究。当前立即转入小文件的优先级由本计划替代；
scope17的实验事实、未过门和旧候选关闭结论保留。

本轮只修改三份文档：本文件、上链路线决定、`docs/ROADMAP.md`。不改源码、默认值、
系统配置、服务、协议上限，不启动性能实验。计划验收：源码事实和文件路径核对、
文档链接及阶段依赖校验、数值推导校验、Git diff检查、明确提交/push/远端SHA回读。

单人串行在R770-1 `/home/Su/projects/CPNetFlux`开发，R770-2只同步固定提交构建验收。
后续每阶段先补该阶段范围、输入SHA、精确测试/实验命令和受影响文件，再写源码。
不使用子代理、并行worktree或桌面；保留两份无关October14未提交文档。

## 已知事实与候选边界

1. 完整任务当前约27–28Gbps，scope17的同轮baseline/profile为27.291/27.296Gbps。
   接收fault调用路径占on-CPU样本51–55%，TCP接收20–22%；不是墙钟比例或收益保证。
2. `include/cpnetflux/core/io/persistent_data_session.h`把`kMaxPayload`限定为65536。
   `src/core/io/persistent_tree_transfer.cpp`的sender把实际读取/分帧长度截断到
   `min(bufferSize, kMaxPayload - 4)`，还受chunk和missing-range边界约束。
   因而只增加CLI bufferSize不会突破65532个逻辑数据字节。
3. 当前1MiB chunk需要17帧，8192个chunk共139264帧，与完整任务计数一致。
   若候选采用256KiB/1MiB**逻辑DATA**上限，理论正常对齐任务分别为32768/8192帧；
   wire payload还需加4字节CRC前缀。它不是简单把现有wire上限改为同一个数字。
4. `src/checksum/crc32c.cpp`已经缓存64个字节幂矩阵；未解决的研究项是每次combine
   按长度位反复应用矩阵，而不是再次缓存已有幂矩阵。scope17 combine Self为接收约7–8%、
   发送约8.5–11.7%，不足以直接推出>=10%整任务收益。
5. `src/core/io/transfer_stages.cpp`的所有权预算目前用65536计算。改变帧上限必须同时
   修正预算和每个实际持有者，禁止只放大协议常量、隐式增加所有连接内存。
6. 大帧不消除首次触页、TCP复制或文件发布成本；CRC优化也不消除这些成本。
   每项仍需证明关键路径收益。初版尚未验证per-file页布局；Byte Work18已在R1证明
   per-mapping MADV_COLLAPSE的16MiB可行性，下一步验证真实窗口和完整生命周期。
   全局THP、mount、NUMA和网络改动不进入本计划。
7. [Memory Attribution04](2026-11-08-v3-memory-attribution-04.md)在旧提交、重复热payload、
   无文件/租约/manifest的诊断中，64KiB V3帧/session加CRC观察到88.514Gbps中位数；
   CV 6.281%未过稳定性门。它反驳“64KiB帧天然只能跑27Gbps”，不是文件吞吐或稳定成绩。
   当时没有证明改帧大小的依据；本计划只允许先检查完整文件特有的逐帧成本，
   不能用帧数、旧诊断或本计划直接宣告A值得实现。

最多筛选两个新机制：A为TCP大负载帧，B为CRC拼接算子。A的两个尺寸是同一机制的初筛，
不能与B一起改后计算收益。不重开population、direct-receive、sender-inline，不继续扩线程/队列。

## LF0：有限预算核对与网络事件关联

目标：决定A是否值得写原型；B是否具备足够预算。先复用scope16/17原始证据和源码，
列出逐帧读取、CRC combine、状态推进、网络I/O和目标写入的实际次数及调用边界。
对可消除的逐帧成本提出保守关键路径预算，禁止把两端累计CPU、Children占比或fault
百分比相加当作墙钟。吞吐+10%需要整任务墙钟至少下降约9.09%，这是必要条件而非证明。
用Memory Attribution04作为反向检查：必须解释完整文件额外的逐帧工作，不能仅凭
同一帧层已有调用次数作结论。若主要耗时仍是与帧数无关的首次缺页/等待，A应跳过。

仅当已有资料不足时增加一轮有明确问题的补测；不得开启1/2/4 lanes、队列、映射大小等
组合扫描。补测优先<=1GiB且保持CRC/新输出；小任务结果不能充当8GiB整任务收益。
新增观测必须默认关闭并验证开销，若没有足够预算则跳过对应候选而非继续找参数。

只读补齐scope17接口+94事件的归因能力：采集自有TCP连接的TCP_INFO/retrans数据、
连接身份/四元组、接口及可用队列计数、时间区间和路由。接口全局计数不当作该连接计数；
没有重现时记录未重现，不能宣布旧事件已解释。没有明确原因不改系统网络或服务。

交付：一页机制/预算表、A/B各自进入或跳过理由、计数和网络事件证据。此阶段仅一轮，
不得以归因名义无限续测。相关落点为既有profile/实验脚本及必要的默认关闭观测，不改传输策略。

## LF1：候选A，TCP大负载帧诊断原型

只有LF0有足够新机制预算时进入。首个代码交付是显式实验模式，默认和旧V3行为保持64KiB wire上限。
原型比较legacy与256KiB、1MiB逻辑DATA上限，不增加lane/depth/总预算，不同时改CRC算法。

目标接口：不可变、按会话绑定的DATA限制；分别校验wire和logical长度，CRC前缀加法检查溢出；
FILE_BEGIN/END/RESULT等控制帧继续使用原限制。64字节帧头和当前schema不盲目改版本号。
实验模式先限定TCP、TLS off、compression off；RDMA/TLS/压缩的不支持组合显式拒绝。
现有默认TLS/RDMA功能不因原型而删除，不能全局放大`kMaxPayload`影响所有传输。

核实过的主要文件：

- `include/cpnetflux/core/io/persistent_data_session.h`
- `src/core/io/persistent_data_session.cpp`
- `src/core/io/persistent_tree_transfer.cpp`（只读复用语义，LF1不集成文件运行时）
- `tools/perf/tcp_frame_bench.cpp`
- `tools/perf/run_tcp_frame_memory.py`
- `tools/test/run_cpnetflux_tcp_frame_bench_smoke.py`
- `tests/unit/persistent_data_session_test.cpp`、`tests/unit/framed_data_socket_test.cpp`

测试先覆盖旧上限、合法尾帧、超长/溢出前置拒绝、截断、错误CRC/offset/identity/attempt、
跨窗口和断开。失败不得认证进度或返回成功；接收长度检查必须在大内存分配之前完成。
用真实驻留源和全新目标，记录实际帧/读写调用、CPU、fault、RSS；若增加文件源输入，
在工具中显式实现并核对，禁止用重复热payload成绩冒充文件路径。

筛选至多两种尺寸、每种三对交错任务，单源/输出<=1GiB；局部服务率中位改善>=15%、
CV<=5%，且CPU/调用数符合机制，才选择最小的通过尺寸进入LF2。这个15%只是原型晋级门。
若两个尺寸均失败或预算不足，关闭A，不集成运行时。不得用筛选样本兼作最终五对验收。

## LF2：候选A的V3文件集成与验收

前置是LF1通过，以及**协议设计门定稿**：显式请求、双方能力/最大尺寸确认、传输类型、
控制帧上限、不支持与不匹配时的错误行为。具体协商字段和控制端源码落点需在该阶段范围中
确认；本计划不假称已有协商实现，也不把未协商的大帧用于真实文件互传。
未请求新能力的peer走既有V3上限；显式请求不支持能力则在payload前失败，不静默改变策略，
不回退到V3以前的执行器或协议。

集成新增落点包括`src/core/io/tree_transfer_client.cpp`、
`include/cpnetflux/config/file_transfer_options.h`、`src/config/file_transfer_options.cpp`、
`include/cpnetflux/core/io/transfer_stages.h`、`src/core/io/transfer_stages.cpp`和LF1会话/传输文件。
协商控制端的精确文件在设计门查明并纳入范围后才允许修改。

保持1MiB chunk、8个1GiB ranges、checkpoint16、工作租约/attempt pin、CRC32C、
verified_chunks和最终发布语义；帧不能越过chunk、missing range或文件边界。
帧变大不等于恢复粒度变大。半帧中断/CRC失败不得把未验证chunk记为完成。
四lane总payload预算仍64MiB，映射仍64MiB/lane、总256MiB；按全部持有者重新核算，
包括队列、生产者/消费者、复用缓存和incoming。预算不满足时payload前拒绝。

先CMake和精确相关CTest：session/frame/checksum、stage预算、inline/queued、mapped、
超时/取消、SIGKILL/resume、checkpoint/发布；保留默认上限及不支持peer负例。
再固定R1提交并push/readback，R2同步，同配置/版本构建并核对传输及相关工具二进制SHA。

完整任务正式验收为新同提交下legacy/候选**五对新样本**，每次真实驻留8GiB源和全新目标、
4路TCP、原scope17其余语义与计时口径，外部SHA仍在吞吐计时之外。两组观测相同，
新策略以外只改一个因素；完整T_ready包含初始化、传输、commit/发布与退出。

保留门：中位task_rate收益>=10%，两组CV<=5%，至少4/5配对改善，CRC/独立SHA、
8192-chunk committed manifest、恢复和资源门通过。新增错误/丢弃计数不得排除样本，
标记网络门未通过并调查；未通过不得称稳定性能验收。通过仅保留显式候选，不改全局默认。
失败关闭A，不能把局部CPU或帧数下降写成完整文件提速。

## LF3：候选B，CRC拼接算子，条件进入

A验收完成或被关闭后，只有LF0预算足够才进入B；不因A失败自动认定B值得实现。
如果A已通过，按A的新帧数及CRC成本重新核算B，不能复用旧139264帧下的预算。
如果A失败，恢复此前已验收的运行时行为后再固定B的输入提交，不把失败策略带入B对照。
目标是对常用rightLength预合成等价CRC线性变换，减少逐次遍历多个长度位的工作。
保持任意长度正确性和通用路径，有界且线程安全；不重复声称首次缓存已有幂矩阵。
不能删CRC、减CRC覆盖、修改线格式或改变chunk/checkpoint。

已核实落点：`src/checksum/crc32c.cpp`、`include/cpnetflux/checksum/crc32c.h`、
`tests/unit/checksum_test.cpp`；如需独立成本工具，扩展`src/apps/cpnetflux_checksum_bench.cpp`。
验证零长度、不同初始CRC、随机分段/拼接、65532和尾64字节、MiB边界、超32位长度的
代数性质、软件/硬件后端一致性、并发与缓存上限。原语bench只证明原语收益。
完整任务沿用LF2的五对、>=10%和资源/完整性门；若预算不足直接跳过，不降低门槛保留候选。

## LF3C：独立对照后的组合验证

A、B分别有可保留收益和正确性证据后，再固定同一提交比较legacy、仅A、仅B、A+B。
组合收益相对最佳单项单独报告，不把单项百分比相加；CPU/帧数变化也单独列出。
实验沿用完整任务、内存和稳定性门，不能用不同提交、不同计时口径或旧样本混算。
若B在A后已无足够预算，跳过组合并记录重叠原因，不为了凑矩阵降低保留门。

## LF4：接收端页面管理

先只读检查两端kernel、tmpfs挂载、THP/shmem策略、smaps实际页型和已有fault记录，
区分新文件页面分配、首次PTE映射和已有页面重复映射。目标是减少实际页面管理工作。
当前R1 shmem_enabled=never，不能承诺MADV_HUGEPAGE会生效；全局THP/共享mount不改。
如新机制确实需要隔离挂载，必须在单独范围中证明隔离和资源边界，不能修改共享挂载。
候选必须用全新目标，必要的预分配、清零、触页和关闭/发布计入完整T_ready。
已关闭population/直接接收保持关闭；把fault移到计时外不算优化。tmpfs收益不能外推磁盘。
本阶段先形成可行性结论，再按LF2同等级完整任务门决定实现/保留，不能无限续测。

[Mapped Collapse19结果](2026-11-09-v3-mapped-collapse-19-result.md)：实际大页有效，但五对基线CV超门，
worker准备/写入/关闭CPU基本不变；whole收益大部分由额外close/unlink解释。当前seed+collapse
不晋级V3，不以降低fault为理由继续调参；转入LF5。

## LF5：数据搬运路径

先按每GiB列源文件→发送缓冲→内核、内核→接收缓冲→目标映射的逻辑复制边界，
把CRC扫描、零页初始化和复制分别计数；不能把逻辑复制边界直接称为实测DRAM流量。
TCP复制规避候选需管理发送完成、部分写、取消/超时、buffer pin和有界回收；
MSG_ZEROCOPY的buffer复用必须等待完成通知，不能只加flag，内核可能退回复制。
原生RDMA后续重启时单独设计注册池复用、CQ批处理、持有者和完整文件生命周期。
每项先独立预算和测试，再完整任务验收；不承诺70–85或100Gbps。

下一项首选核实驻留源只读有界映射是否能消除pread→发送buffer的一遍复制，先查现有路径。
如需局部诊断，仅一轮<=1GiB真实多样字节，对照actual PosixFile::readAt与只读映射消费，
CRC32C、字节数相同，map/首次PTE/CRC/close计入必要服务边界，源准备/目标销毁分列。
运行时前先设计queued消费者持有的窗口pin、部分写/取消/源变更及64MiB/lane预算，
没有可信关键路径预算则停止，不能把微基准CPU相减冒称完整任务收益。

## S0及后续：小文件固定开销，独立研究目标

逐帧筛选及页面/搬运阶段按各自门完成或跳过后，进入小文件基线；不靠无限参数搜索续测。
本项目已有显式pending-window auto（密集小文件选8，默认仍2），它是tuned native的一部分，
不等于成员聚合。不能仅拿窗口1作为新微批对照，也不把旧39–41%的窗口收益冒称本轮成果。

先测4/16/64/256KiB和1K/10K的可行子集，总量<=1GiB，记录cold setup、完整T_ready、
open、成员校验、manifest/checkpoint、commit/发布成本。只有固定成本主导才另立成员微批设计，
按tuned native→固定成员批→adaptive依次对照，不叠加压缩或服务率调度。
成员路径/长度/CRC、失败隔离、成员级恢复和原子发布必须保留。
目标T_ready中位下降>=20%，p95/恢复不恶化、大文件回退<=5%；各项单独报告。
旧manifest/coalescing候选不能换名重开；新设计需要新的机制与成本证据。

## 统一资源、验证与交付规则

- 不用64GiB；LF1<=1GiB，正式文件验收<=8GiB。每端至多一个源或当前输出，
  MemAvailable>=12GiB、owned VmSwap=0、host pswpout增量0，超限即停止。
- 明确绑定192.168.100.2/.1、路由eno17095np0，保留接口/连接证据；TCP不称verbs。
  不动生产服务；每个输出独立校验后精确清理，最终复核源后删除，检查进程/FIFO残留。
- 先写行为回归，再实现核心协议/并发/恢复改动；先本机相关测试再固定提交同步双端。
  常用构建命令`cmake --build build --parallel 4`；每阶段从`ctest --test-dir build -N`
  生成并保存精确相关列表、执行命令和实际case数，不能把零匹配称为通过。
- 运行证据放`/home/Su/experiments/CPNetFlux/<阶段任务名>`；保留配置、输入SHA、
  raw日志、失败、分析脚本、清理结果和SHA清单，禁止大payload/凭据进Git。
- 每轮只交付一个机制的实现/结果；明确源码SHA、同轮baseline/candidate、收益及未过门，
  指定文件提交、推送已核实GitHub remote并回读SHA。无收益的原型不默认集成。

Byte Work18完成LF0局部诊断及页面探针，见[结果](2026-11-09-v3-byte-work-18-result.md)。
下一项是LF4实际有界窗口诊断；A/B保留为预算未证实的暂缓项，不直接放大64KiB常量。
当前没有证据承诺30/50/70–85Gbps；若新同轮baseline仍27.3Gbps，+10%门约为30Gbps，
只作为条件计算。100Gbps有效应用吞吐、1Tbps聚合和生产readiness继续不作承诺。

## 官方参考与本机边界

- [zlib checksum接口](https://www.zlib.net/manual.html#Checksum)：只参考预生成算子的思路；本项目是CRC32C，不能替换为CRC-32。
- [Linux THP](https://docs.kernel.org/admin-guide/mm/transhuge.html)：支持范围需结合本机kernel/mount/实际页型核实。
- [Linux MSG_ZEROCOPY](https://docs.kernel.org/networking/msg_zerocopy.html)：完成通知、buffer生命周期及复制回退是候选设计的一部分。
