# 阶段75：固定版本无探针正式比较，随后有条件稳定性

范围：交付C续接。阶段74双端临时RX ring4096的无BPF1GiB准入通过，只作进入正式比较的条件，不是稳定性或唯一因果证明；
固定最终已备份提交和配置，从0执行五对CP/GridFTP，成功才进入后续场景和稳定性。
输入运行时42718ac26be24d53ec17525a7b1b3bcb1e340931；精确输入提交由外部serial-handoff.json固定。
项目根唯一写入者仍root；交接期间root停止源码/Git写入，既有“配置和实验”聊天只写新实验目录，
不得独立改源码、Git index、分支、系统配置或任何旧冻结目录；清理后serial-return再交回root。

## 非目标

不调吞吐参数、不复活RSS/CPU/rate/frame/lane扫描，不用BPF/perf/strace观测实际性能任务。
不跑8/64GiB；已获用户本轮明确授权的临时RX ring4096由74控制器持有，其他RSS/IRQ/RPS/THP/cache/swap/网络/信任不改，不改默认、不重启RDMA/微批/策略记忆。
旧64唯一有效配对不并入新样本，失败不隐藏或重跑挑样。

## 前置和计时

双端同固定提交/配置、构建成功、四二进制SHA一致；保留R1/R2原2121 PID126766/start1191040、PID164834/start1679976。
本轮用192.168.100.2→.1，明确bind、路由和接口计数；TCP/tmpfs非持久化，非verbs。
1GiB准入两端MemAvailable>=12GiB且MemFree>=2.25GiB；owned进程/leaf cgroup swap0、host pswpout增量0。
资源采样必须覆盖实际client/server，峰值每端不超过8GiB，逐任务fresh target和owned清理。
实际cgroup max/high=max则如实说明，不冒称有强制限额。监测不加载重型探针。
Grid前刷新GSI proxy并检查有效期>=1h，确认两端服务、空间、认证；凭据不输出到日志/Git。
双方使用已经冻结的最强合法配置：CP沿用74源端口计划+原63/64应用pacer配置，
Grid沿用64 grid-selection冻结最强配置，记录准确argv/env/版本和认证/校验/准备语义差异，不扫描新组合。
CP四lane、256KiB DATA、1MiB chunk、CRC32C/verified_chunks/checkpoint16、queued/inline+mmap、
64MiB/lane、接收请求1MiB、1.5e9B/s/lane应用pacer；TLS/压缩/kernel pacing/exact-read off、commit_sync=none。
必要native私有tmpfs setup、目录创建、实时RSS/plan预检、client启动到退出都计入完整任务，
双方同一源内容、目标全新、实际页型/驻留记录；source生成/外部SHA/观测/cleanup单列。
报告原始client窗口和完整任务分母，不用应用summary秒数代替完整任务；summary wire统计与TCP字节口径不同。

## A：五对正式主组

1GiB单大文件，最多五对完整交错样本：第1对CP→Grid，第2对Grid→CP，依此交替。
第1个CP即无探针正式核验，不另增加诊断大负载；任一content/resource/network/cleanup硬门失败立即停止后续所有负载。
网络门包括宽窗口及刷新后ethtool/sysfs/HCA errors/drop/out_of_buffer/missed增量均0；不靠细窗漏计数。
每对同一fully-materialized resident源，双方任务外部SHA一致；CP1024 verified chunks。
至少五对全有效、CP和Grid各完整任务吞吐CV<=5%才能发布比值或通过正式主门。
五对内失败/CV超限即NO_GO，不补跑、不筛掉失败样本，原始诊断数字与正式有效结果分开。

## B：A通过才运行场景组

目录dense=2048×64KiB、tiny=10000×4KiB、mixed=256MiB含零长文件/子目录/空目录，至少三次各软件完整任务；
用树哈希/内容和支持的目录语义验证，不能把不支持空目录的对照静默当作完整等价。
反向.1→.2至少三次，使用已核验的反向合法配置和显式bind；没有现成可用反向计划不临时盲扫，记录未完成原因。
安全组分开记录CP控制TLS/数据TLS实际启用、证书校验和Grid数据保护状态；不同保护/校验语义不发布公平速度比。
认证失败不是性能结果。B任一硬门失败即停止，不进入稳定性。
8GiB因当前MemFree门未准入仍NOT_RUN，不以主组1GiB代称完成8GiB M2。

## C：A/B通过才运行稳定性和故障验收

同一固定版本/配置的常驻服务，持续>=1800秒且>=100次完整跨机任务，声明每任务大小、累计字节、频度和并发。
允许有界1GiB及目录任务，逐个fresh target并校验/清理；不积累100个目标，不无限重启服务掩盖泄漏。
全量成功率、文件/树内容、FD/线程/RSS/owned swap/host pswpout趋势、首尾吞吐漂移和完整网络计数。
任何硬门失败停止晋级，声明实际时长/任务数，不能把局部稳定当作通过。
另有data/control断连、取消→恢复、服务重启恢复、TLS、坏数据/坏checkpoint拒绝，不能用loopback旧回归冒充双机新故障验收。
完成后给READY/NOT_READY判定，明确TCP/tmpfs非持久化、真实磁盘、native RDMA和生产安全各边界。

## 输出与停止

实验目录/home/Su/experiments/CPNetFlux/2026-11-12-field-trial-comparison-75，保留原始小证据、精确命令、
输入提交/四hash、全过程门控、失败与cleanup、只读审计和result.md。
交接结束必须停止owned进程/监听/挂载/临时payload，核对原2121身份，写serial-return.json明确释放写入权。
root核对产物后再提交结果、GitHub回读、双端同步和Windows归档。未实测项明确NOT_RUN，整体用户目标保持OPEN直到全验收。

## 阶段75新增明确执行边界

用户已允许双端eno17095np0 RX1024→4096临时调整，并在准入通过后继续正式比较/场景/稳定性。
74准入已通过且实际无BPF；不并入75正式五对。74控制器PID/startticks、guard-spec/lease/phase在74实验根。
90分钟窗口自74 guard-spec.started开始，不能由新合同重置，且74控制器5300秒开始收口。
75从接手到完成最多75分钟；开始前核剩余窗口，完整稳定性必须有>=1800秒实际窗口，不能超时后仍发送。
双端临时RX4096/TX1024只读核验；禁止自行修改ring或延长监督器。源代码/Git唯一写入者root已停止。
既有配置和实验聊天只写75实验目录；额外只允许写74/finish-request.json控制信号，不修改74其他文件。
任一阶段硬门失败或全部完成后，立即写finish-request.json（含reason/status），让74控制器恢复两端RX1024，
再做自己的失败审计/清理/serial-return；不要让网络改动在写报告期间无必要地继续。
74监督器若已回滚或lease异常，立即停止75任务，不在原RX下冒称4096实验。

A先最多五对，严格五对全有效与双方CV<=5%门，不补跑；Grid最强配置与认证/完整性差异照原合同。
B对目录/小文件及TLS采用已经实现且合法的各场景强配置，不强行套用只支持4..8大文件range的mmap实验；
准确记录合法模式差异，不将按类型选择合法既有路径称为新调参或运行时V3降级。
反向源端口计划若需按R2现有RSS key离线生成可使用已有工具，记录必要生成/预检成本；不改RSS、不做带负载扫描。
B任何必测项未完成或失败，不进入C。
C必须常驻同一服务PID/版本连续运行>=1800秒且>=100完整跨机任务，声明字节/频度/并发，
完成一项的内容验证和有界清理后立即下一项，不用人为长sleep填充时长。
源可保持1GiB完全驻留并复用，新目标必须逐任务全新并删除；不积累payload、不每任务重启服务掩盖泄漏。
单个任务及全窗口的FD/thread/RSS/swap/网络门及首尾吞吐漂移完整保留；资源observer的180秒旧租约必须按此窗口有界适配，
不能180秒退出后仍声称覆盖30分钟，也不把采样当严格瞬态峰值。
达到时长与任务数后再做双机data/control断连、取消恢复、服务重启恢复、TLS、坏数据和坏checkpoint拒绝；
这些单独标故障组，不把预期故障算成功吞吐样本，但仍验证内容/资源/恢复事实和网络门。
若90分钟内不足以全部完成，及时停止、回滚并把实际已测/未测分别记清，不用低负载等待凑通过。
结果必须给完整用户目标是否闭环，而不只给A通过。
