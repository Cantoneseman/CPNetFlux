# V3 大文件性能里程碑35：路线重整与串行开发交接

日期：2026-11-10。输入提交：`d6de583bed7dbec6533220e4af4ff34993207ac9`。
分支：`codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`。
用户已批准：在固定完整性语义和测试条件下，让完整 V3 大文件任务跨过 30 Gbps，并验证向 40–50 Gbps 扩展的机制。
状态：交付1路线与待办已整理并通过文档门禁；后续代码、实验和性能目标均待执行。当前整理者完成提交后交接给既有“开发”聊天，实际备份和派单以回执为准。

## 本次交付1范围、非目标与验收

本轮只重整路线、决策、待办及开发交接，不修改运行时、不运行性能互传、不改系统或服务。
R770-1 `/home/Su/projects/CPNetFlux` 为唯一写入目录；保留两份 October14 未跟踪文档。
受影响文件：
- `README.md`、`docs/README.md`、`docs/ROADMAP.md`；
- `docs/DECISIONS/2026-11-10-v3-performance-milestones.md`（新决定）；
- `docs/DECISIONS/2026-11-09-v3-large-file-candidate-screening.md`、`docs/tasks/2026-11-09-v3-large-file-next-plan.md`（仅标记旧执行约束失效）；
- 本文件 `docs/tasks/2026-11-10-v3-large-file-milestone-35.md`。

交付1验收：核对输入/remote/既有修改；检查以上文件的相对链接、状态/依赖和差异空白；
准确提交上述文件，推送已核实的 CPNetFlux remote 并回读 SHA；保留本地可查阅副本；
当前写入者停止写入后向用户指定的既有聊天发送明确输入提交和剩余任务。
纯文档改动不运行无关 CTest；文档脚本检查及 Git diff 为本轮门禁，不冒称完成代码测试。

## 事实基线与指标

- 最新可比大文件结果仍为 scope17 输入 `025c38a9` 的 27.291 Gbps / 8 GiB 约2.518s；不是本输入提交的新成绩。
- 条件：R770-2→R770-1，真实驻留tmpfs源、全新tmpfs目标、4路TCP、queued发送/inline实验mmap接收、CRC32C/verified_chunks、checkpoint16、TLS/压缩off、commit_sync=none。
- T_ready包含任务初始化、传输、必要校验、checkpoint/发布及终态退出；任务中的准备/清零成本不可移出。外部独立SHA在计时外，保持两臂一致。
- task_rate只用成功且校验通过的有效字节/T_ready；payload_rate另报。CPU/GiB、RSS/池预算、帧数、阻塞/尾部和网络事件为解释指标。
- 不把不同数据量、观测、计时合同、默认校验策略或持久化配置混成一条性能增长曲线。

## 执行顺序与交付

| 交付 | 状态/负责人 | 可审查的输出 | 依赖 |
|---|---|---|---|
| 1 路线和待办重整 | 当前整理者 | 新决定、路线图、本计划、GitHub备份、串行交接 | 本轮完成后交出写入权 |
| 2 完整V3大负载DATA原型 | 待“开发”执行，最高优先级 | 真文件路径、相关测试、同提交A/B及机制解释 | 接手核对；不要求事先证明≥10% |
| 3 下一核心机制选择及实现 | 待“开发”执行 | 根据交付2选择一个机制，记录接口/收益假设并实现验证 | 不能自动转回通用热点审计 |
| 4 集成和里程碑验收 | 待“开发”执行 | 新同提交完整任务结果、能力范围、回退及未达标解释 | 正确性、资源与正式负载条件 |

后续用户说“继续”时推进尚未完成的上述交付，不自动新建一个独立观察项目。
每次只有一个软件机制在开发；允许该机制涉及多个有依赖的文件。用对照分离机制，不要求一次只改一个函数。
默认一轮完整原型加一轮基于证据的修正；修正必须针对具体失败/机制，不能换参数反复筛漂亮样本。
正确性缺陷仍须修复或安全撤出候选，不能以轮次预算为由留下不安全默认行为。
如果两轮后未改善目标指标，交付3必须作出架构选择或明确未决问题，不继续重复相同诊断。
不承诺达到30/50Gbps；未达标必须如实交付实测结果与下一结构性选择。

## 交付2：完整V3 TCP大负载DATA

### 接手实现合同（输入6c153352，开发为唯一写入者）

交付2进行中。显式CLI `--data-frame-bytes 65532|262144`，默认65532；
控制请求 `XCPNETFLUX V3 ... DATA_BYTES=262144`，仅新模式发字段，响应必须
精确包含 `data_bytes=262144` 和 `max_data_bytes=262144` 独立token，否则拒绝。
每次重连重新协商；数据连接在交出运行时前固定逻辑大小，DATA上限=逻辑+4，
其他帧仍65536且FILE_BEGIN仍8192；分配前按类型校验。新模式仅TCP、control/data
TLS关闭、压缩关闭、CRC32C、1MiB chunk、V3 worker复用；不支持组合payload前拒绝。
低层显式大DATA状态机也拒绝跨chunk，恢复mask/attempt仍原校验。
总所有权按lanes*(depth+6)*(logical+4)估算（旧模式保持65536）：queue depth、
producer/consumer/recycled/incoming/decoded/compressed或故障duplicate；新模式无压缩，
仍保守预留。队列shared阶段配置包含帧容量，切换拒绝。超预算在协商/数据accept前
预检，并在实际runtime重查。4 lanes/depth2候选约8MiB，不提高64MiB预算。
映射窗口仍64MiB/lane/256MiB总计；metadata、CRC索引和transport另行统计，不算payload。

精确文件：include/src core/io framed_data_socket、persistent_data_session、
transfer_stages、dynamic_file_queue；src/core/io/persistent_tree_transfer.cpp、
tree_transfer_client.cpp；include/cpnetflux/config/tree_transfer_options.h、
src/config/tree_transfer_options.cpp；src/protocol/control/control_server.cpp；
tests/unit/persistent_data_session_test.cpp、transfer_stages_test.cpp；
tools/test的新真实V3大帧smoke及对应CMake注册；本计划和已有路线/结果。
验收先相关CMake/CTest和小fixture：旧模式、新DATA/控制限额、缺协商拒绝、
截断/CRC/边界、上传下载/恢复与预算；再固定提交双机<=1GiB同合同对照。
交付3/4尚未完成，不以交付2部分实现声称总验收。

2026-11-10 原型实现验收：R1 RelWithDebInfo/RDMA=ON 构建成功；
`ctest --test-dir build --output-on-failure -j4 -R '(PersistentSessionTest|TransferStagesTest|TreeTransferOptions|PersistentRangeWrite|cpnetflux_large_data|cpnetflux_v3_interrupted_resume_smoke|cpnetflux_publication_resume_smoke|cpnetflux_receiver_inline_smoke|cpnetflux_range_observability_smoke|cpnetflux_tree_mode_smoke|FramedDataSocket|cpnetflux_gridftp_.*tls_smoke)'`
150/150 通过，16.96s。新协议旧二进制红灯为504协商拒绝；新实现真实双向、queued/inline、
尾帧/帧数、旧peer拒绝、控制限额、跨chunk、截断、坏CRC不认证、512MiB中断恢复均已覆盖。
证据位于既定实验目录 `{large-data-red,related-tests-r1,build}.log`。
`tools/perf/run_large_data_ab.py` 复用现有资源/进程/清理与计时；固定1GiB、
五对交错65532/262144；源准备进程自身记录cgroup及各祖先的memory限制/事件，
写入/SHA/驻留和每次任务继续执行原门。实际帧数由mmap写调用核对，SHA/checkpoint完整性
保留。8范围各128MiB、4 lanes、原64MiB映射窗口/256MiB总上限；不跑8GiB、不作为M1。
增加公共client_sample可选data_frame_bytes参数，旧调用不变；不复制资源控制框架。

交付2结果（73d77c9，同版本两端）：1GiB五对交错中位27.331→35.821Gbps（+31.06%），
5/5配对改善；帧数17408→4096，接收CPU采样下界中位1.01→0.85s/GiB。
候选CV=5.176%（对照4.408%），未通过5%稳定门；候选r2/r5各增加rx_missed_errors
206/367，不丢弃样本，不归因于软件或网络故障根因。host swap-out和owned swap均0，
SHA/checkpoint/驻留通过；两端payload根已清理。原始证据data-ab，汇总prototype-analysis.json。
源写/SHA进程PID1285270，session-26037.scope及祖先memory.max/high均max，
本scope swap0且host swap-out0，1GiB生命周期驻留正常；不证明8GiB条件恢复。

### 交付3选择：数据接收socket的有界背压缓冲

交付2现存ss快照显示多个发送数据连接rwnd_limited约96%–99%，接收窗口约64–126KiB，
故优先验证接收服务间隙与TCP窗口背压，而非先改CRC算子/缺页路径。
这不是证明丢包根因；候选若仅提高CPU/内存而无可信任务收益则撤出默认候选。
范围：显式`CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES=1048576`，默认off保留内核autotuning；
仅plain TCP数据接收连接、最大4 lanes，单socket kernel返回SO_RCVBUF上限2MiB，
总kernel receive buffer上限8MiB，独立于64MiB用户payload预算。仅在接收开始且payload前
配置；不改sysctl/网卡/控制socket，不扫描大小。报告实际getsockopt值，拒绝不支持组合。
文件：framed_data_socket.h/cpp、persistent_tree_transfer.cpp、tree_transfer_client.cpp、
persistent_data_session_test.cpp、既有large-data smoke和run_large_data_ab.py及本计划。
验收：setter拒绝/读回、旧行为回归、大帧与中断恢复；同提交固定大帧off/1MiB五对1GiB。
所有样本与接口事件保留。比较task_rate/CPU/RSS/窗口；原型一轮加至多一次定向修正，
不因CV偏高重复补样筛选。8GiB M1继续BLOCKED_ENV；RDMA未解禁。

交付3实现：plain TCP接收入口显式设置并getsockopt读回，实际值写lane_endpoints，
默认off不设置SO_RCVBUF（保留内核autotuning）。初测统计在socket释放后取值为0，
已在关闭前保存到lane记录；不改变payload行为。R1同相关命令152/152通过17.78s，
含新buffer smoke与setter单测；红灯为旧程序缺少读回字段。性能对照待固定提交后执行。

已批准开发这个有界原型；scope18/33“缺少保守≥10%预算而不得写运行时”的前置限制失效。
首个实验规格采用256KiB逻辑DATA，另留现有65532B逻辑DATA对照。保持1MiB checkpoint chunk；
理论对齐chunk由17帧变4帧，wire payload须额外容纳4字节CRC。先不扫描1MiB规格、lane数或队列深度。
现有64KiB热buffer诊断能达到高吞吐，因此“帧小天然限速”不是假设；本轮验证真实文件中的多层逐帧服务成本。

在实现前于本节或同阶段设计记录中定稿以下接口，随后直接实现，无需重复请求常规设计确认：
1. 显式请求、双方支持/最大尺寸确认、会话不可变的wire/logical限制；确认实际控制端字段和源码路径。
   显式请求不支持的新模式须在payload前报错。未选择新模式仍用现有V3，禁止V3以前降级。
2. DATA与控制帧限额分开，控制/恢复消息保留已有有界分页；分配前校验类型、长度、溢出和身份。
3. 首轮新模式只用于TCP、TLS off、compression off；不支持组合明确拒绝，保留已有默认功能。
4. 帧不得跨chunk、missing-range、file或attempt边界；半帧失败不得认证chunk。
   保持CRC32C、工作租约、inflight drain、checkpoint、原子发布与成功确认语义。
5. 按生产者、消费者、队列、incoming和复用缓存的实际持有量重新计算预算。
   总payload预算64MiB、映射64MiB/lane/总256MiB，连接数4；超预算启动前拒绝。
6. 原型进入真实V3文件路径，不以又一个仅诊断程序代替本交付。CRC combine算法首轮不改。

已核实的主要落点（实施者应读取当前源码/已有改动再编辑）：
- `include/cpnetflux/core/io/persistent_data_session.h`、`src/core/io/persistent_data_session.cpp`；
- `src/core/io/persistent_tree_transfer.cpp`（发送/接收及内部RangeFileCoordinator）；
- `src/core/io/tree_transfer_client.cpp`、`include/cpnetflux/config/file_transfer_options.h`、`src/config/file_transfer_options.cpp`；
- `include/cpnetflux/core/io/transfer_stages.h`、`src/core/io/transfer_stages.cpp`；
- 按实际设计影响 `include/cpnetflux/core/protocol/frame.h` 及对应codec/控制端文件；先确认路径再纳入本节。
- 测试复用 `tests/unit/{frame,persistent_data_session,persistent_range_write,transfer_stages,tree_transfer_options}_test.cpp` 和现有V3 smoke；必要时更新CMake注册。
- 实验复用 `tools/perf/run_small_file_baseline.py` 的资源/进程/清理设施及已有大文件驱动的合适部分，不复制另一个大框架。

测试关注可观察行为：旧模式兼容；新模式完整文件；不支持peer/组合；控制帧仍有界；
合法尾帧、短读写、截断、坏CRC、错误offset/identity/attempt、跨窗口；分配前拒绝越界；
queued/inline预算；取消/超时不提前复用；中断恢复仅重传缺失chunk；发布/终态正确。
先用实际CMake/CTest注册名选相关测试，记录精确命令和结果，不能以列表不存在为理由编造通过。

## 分层实验与准入规则

1. 正确性与机制层：先小夹具，再≤1GiB真实源/全新目标；内容SHA、实际帧数、CPU/GiB和内存须有证据。
   这一级不要求15%局部收益，也不要求提前推出10%完整任务收益；仅回答机制是否实际生效。
2. 同提交完整任务层：比较相同负载下的现有V3与候选。较小完整任务可以形成自己的成绩，
   但不能取代8GiB里程碑或与scope17直接算增益。初始化/收尾不可因为样本小而排除。
3. 正式里程碑层：恢复受控8GiB条件后，固定代码/配置/计时，至少五对交错A/B，保留全部样本。
   预先冻结分析；记录配对变化、各臂中位数与CV。CV≤5%且至少4/5配对改善作为稳定收益判断的组成部分；
   不能仅凭单个CV阈值证明收益/无效。噪声不清时报告不确定；必要补样须先说明原因、上限并保留旧样本。
4. M1：在上述完整性/环境条件下候选task_rate中位≥30Gbps，且相对同提交对照有可信改善。
   30–50中期门不再被解释为每个小改动必须≥10%；M2逐步验证40–50Gbps的实测可达性。
5. 优化保留按超过噪声的收益、CPU/内存、复杂度及回退综合决定。简单稳定3%–5%可有价值；
   CPU显著降低而任务无收益的机制可保留独立候选，不宣称已提速或自动进入默认。
   默认晋级仍需适用范围/恢复/资源回归。历史性能失败不改判通过。
6. 新接口错误不删除样本，性能/内容/网络门分别报告；历史scope17的+94未解释时不得宣称稳定性总验收。
   现有内存、源驻留、owned swap及host swap-out门保持，不因本决定放松。

## 环境阻塞与交付3/4

scope34证明空闲窗口并不足以保证8GiB受载源驻留。不得再用相同前提盲重试8GiB。
允许一轮有明确问题的资源诊断：先查已保存证据及实际准备进程/cgroup约束；必要有界负载≤1GiB，
覆盖写入/SHA/驻留生命周期。不能用另一个SSH进程的cgroup读数代替归属证据，不改共享swap/THP/挂载/网络。
只有发现并说明条件变化、资源归属与可控准备合同后才安排新的8GiB验收；仍不满足就保持BLOCKED_ENV。
阻塞不影响小夹具、本地正确性及满足原资源门的较小任务开发。S1B默认晋级留待此环境门解决，不抢占主线。

交付3根据大帧真实结果选一个软件核心机制，例如：
- 若剩余逐帧/共享锁CRC工作实质限制服务率，评估等价CRC算子或状态更新工作粒度；
- 若接收页管理/文件生命周期限制服务率，设计改变实际工作量的接收/存储路径；
- 若发送搬运成为限制，再评估已验证所有权模型的有条件组合。
以上是决策选项，不预定根因；不得相加不同端CPU/重叠阶段推算墙钟。
如需新增观测，先写要区分的两个解释和各自下一行动；最多一轮定向采集，不自动串起多轮通用审计。
scope26“更快序列化导致更多快照”的发现作为未来保存机制/增量状态设计输入；不直接删checkpoint/ACK。
小文件固定/自适应微批暂列后续独立目标，不用其收益替代大文件门。

交付3结束必须明确一次RDMA路线评估：当前TCP机制的收益/限制、重新启动原生RDMA需要的资源/正确性工作。
原生RDMA运行时优化仍暂停，本轮不自动解除；如需要改变此范围，报告具体理由和建议，而非无限搁置。
交付4冻结保留机制，重测完整任务/恢复和相关回退；区分显式实验、默认行为、TLS/磁盘/持久化未测范围。
最终交付实测成绩、能力变化、关键障碍、未通过门及下一结构性任务，不用提交数/测试数代替性能进展。

## 开发、提交与交接

每阶段开始记录当前输入SHA、精确文件范围/接口/验收命令；更新本计划即可，不为每次读取创建新阶段。
R1直接开发→相关CMake/CTest→提交明确文件/push/readback→R2同步固定commit并同配置构建/测试→
版本及运行二进制SHA核对→显式bind .2/.1互传。阶段性源码与文档一起提交，禁止git add .。
证据保存在 `/home/Su/experiments/CPNetFlux/2026-11-10-v3-large-file-milestone-35` 或后续明确子目录；
原始payload不入Git，失败报告/清理/路由/接口及输入版本保留。

派单对象：既有“开发”聊天 `01a1aa29-0026-73a0-b83b-f7002310b623`。
交付1开始时已读取其状态：上一轮completed、notLoaded，无正在运行的该聊天轮次。
本轮已核验专用聊天读取/派单工具、SSH shell可用；本机codex.exe已定位，未用CLI续接。
仓库扫描未找到DISPATCH/ROSTER文件，不能冒称遵循不存在的角色表；按用户明确指定的聊天及现行AGENTS串行交接。
派单前当前整理者完成最后一次仓库写入；派单后“开发”为唯一写入者，当前聊天仅作只读核验。
GitHub提交/远端SHA与实际派单回执在聊天及仓库外交付回执记录，不以“文件已更新”冒称任务已启动。
