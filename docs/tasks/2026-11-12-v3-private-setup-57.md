# Task57：必要目标准备启动路径

日期2026-11-12，输入aa2ed81d45bd1f8d7b0f98a51e8772715b9e1182。
上轮进展为PROGRESS：Task56代码/双端回归/固定实机样本/交付冻结均已完成。
当前根目录仅有两份既存未跟踪Task01b文档，保留；无活动实验进程，不派单。
本轮专用聊天/shell/CLI可执行文件均已检查存在，但只使用SSH shell单人串行。
沿用systematic-debugging、executing-plans、TDD与verification-before-completion。

事实：Task56唯一1GiB完整目标35.387828Gbps；client187.952ms、
必要namespace/挂载54.108ms、目录0.677ms；mount子区间2.797ms。
40Gbps的1GiB预算214.748ms，仍需减少27.989ms实际工作，尚无收益保证。

范围第一步：无payload、固定五组配对启动审计，比较sudo/unshare执行/bin/true与
执行当前private_tmpfs.py --help的完整启动成本；保存逐样本/中位数、版本、资源和清理证据。
它只检验重复Python启动的可减上界，不证明文件传输收益；不测网、不改系统或源码行为。
若成本证据足够，再补充一种明确实现的接口、所有权、失败清理与测试要求后动代码。
可能方案为小型native namespace/mount supervisor，保留原Python默认路径用于对照，
所有payload/SSH/SHA仍以Su执行；不得提前准备/预热目标或移出计时。
若Python启动不足以解释所需预算，不直接实施复杂helper或重跑原Task56。

非目标：本轮不改V3帧/CRC/租约/调度/原生RDMA，不增线程/缓冲/网卡配置；
不重开旧rate/lane/frame扫描、population/direct-receive/queued-mmap或DEFERRED研究。
原Task56候选停止门仍有效；新跨机样本必须先有新机制合同和固定双端提交构建/SHA。

当前受影响文件仅本合同、证据根内审计脚本和记录；后续实现文件与验收另行追加明确列表。
验收：读取实际输出/退出码，核实源码未变、/dev/shm无残留、全局mount/THP不变，
审计不足也如实记录；若实现则先失败测试，相关CMake/CTest，提交推送并读回SHA。
证据根：/home/Su/experiments/CPNetFlux/2026-11-12-v3-private-setup-57。

## 审计结论与实现决定

五组blocking wait配对：native_exit中位7.999966ms，Python --help中位53.162415ms，
配对差中位46.085099ms；host mount/THP与pswpout未变。早期subprocess.run(timeout)
样本被父进程轮询回退放大到63.7ms，原始数据保留但不用于决策。
这支持删除一次root Python启动的机制假设，不能直接预测完整任务收益。
比较：lazy import仍保留解释器/CLI启动，收益上限不明；持久root daemon增状态与特权生命周期，
不采用。选择显式native supervisor，独立小型Linux可执行文件，不链接传输core；默认保留Python。
它改进的是实验内存文件后端的必要任务启动，不减少每字节传输CPU，不外推100G能力。

新增--native-setup只选一次原生准备路径，原--enable-private-tmpfs仍必需。
Su父进程从mkdir前启动同一计时；sudo启动原生helper，helper完成身份/path/inode/evidence验证、
unshare(CLONE_NEWNS)、mount(/,MS_REC|MS_PRIVATE)、1280MiB tmpfs/huge=within_size及实际mountinfo读取，
写相同mount-ready.json后才启动原来的Su Python工作负载。所有必要检查照计，不提前触页/清零。
helper只允许固定V3/probe模式，固定脚本与解释器，不接受任意命令；不设setuid、不装daemon。
设置子进程subreaper；真实/有效/saved uid/gid降为Su并清空附加组后exec；root不执行payload/SSH/SHA。
JSON/日志以exclusive/no-follow创建且fchown Su，拒绝既存symlink；不删除任意payload。
正常/错误/超时/取消均回收leader与被收养后代，再卸载；禁止懒卸载掩盖活跃进程。
取消文件或SIGTERM触发TERM，最多45秒后KILL；失败清理必须记录失败，不报pass。
自有payload内容由现有driver清理，父进程只在inode/owner/empty匹配后rmdir。

明确受影响文件：tools/perf/private_tmpfs_launcher.cpp（新增）、tools/perf/private_tmpfs.py、
tools/perf/run_private_tmpfs_v3.py（仅新增准备模式证据门）、CMakeLists.txt、
tools/test/run_private_tmpfs_test.py、tools/test/run_private_setup_smoke.py（新增），
本合同/结果及docs/ROADMAP.md、docs/README.md、README.md导航。
新增setup-only模式用于无payload真实挂载闭环，身份/namespace/卸载证据完整保留。

验收先写RED：候选默认off、显式native选路与参数传递、非任意命令、身份/路径/inode拒绝；
真实native mount闭环正常/失败/超时/取消/外部取消/后代回收、权限与host状态恢复。
新增smoke以64MiB页型probe和128MiB本机V3确认机制；不作为跨机性能成绩。
固定五对setup-only旧/新挂载闭环仅比较target_setup_seconds，要求每对减少且中位节省>=28ms，
所有清理/身份/host门通过才写新跨机合同；不过则停止，不做无界重复审计。
随后相关CMake/CTest、提交推送/固定双端配置与三个传输二进制+helper SHA后，
最多一次新1GiB四lane V3，保留Task56全部网络/资源/校验/必要准备计时门；不得原样重跑旧候选。
新8GiB仍须>=40且全门通过后另立合同，本轮不自动启动；原生RDMA/DEFERRED继续暂停。

监督器源码复核发现外层取消等待仍为45秒；native内层TERM grace固定30秒，
随后最多5秒收养后代回收，给外层45秒强杀前保留余量，不延长外层旧路径。
此为上述“最多45秒后KILL”的具体实现；正常退出仍立即回收，不改变性能计时。

## 本机结果及固定跨机合同

首次五对setup-only全部节省，配对节省中位45.121748ms；旧准备中位53.978672ms，
native中位8.031810ms。该组在最终“拒绝已有挂载”防护补齐前执行，只作机制证据；
最终输入提交另做一次固定五对，不重复调参。新增已有挂载回归先失败（进入了重叠挂载后才报错），
再将mountinfo拒绝检查移至unshare前；完整原生生命周期与11项相关CTest通过。
早期所有失败和等待计时偏差原始证据保留，不修改成pass。

跨机仅在最终五对准备门和双端构建/11项相关CTest/四个二进制SHA一致后运行一次1GiB。
固定R770-2→R770-1、192.168.100.2/.1显式bind、eno17095np0，四lane V3 TCP，
256KiB逻辑DATA、1MiB chunk、CRC32C/verified_chunks、checkpoint16、commit_sync=none，
queued sender/inline实验mmap、64MiB/lane窗口、64MiB payload预算、1MiB SO_RCVBUF请求。
内核TCP ceiling固定2e9B/s/lane；应用pacer/exact-read/TLS/压缩关闭，不扫描任何参数。
唯一差异是必要准备改为原生syscalls；私有tmpfs仍1280MiB、huge=within_size、Su执行负载。
明确新机制是后端启动工作减少，不是把Task56原样重跑或把准备提前到计时外。

分母保持mkdir前计时→私有namespace/身份/path/inode/已有挂载/evidence校验→新目标挂载及实际策略证明，
加目标目录创建及client创建到成功退出；源驻留准备、外部SHA/观测、清理单列，和Task56相同。
内容、1024 verified chunks、实际目标VMA大页、源全页驻留、两端MemAvailable>=12GiB、
自有swap0/host pswpout增量0、窄/settled/整体/发送服务网络错误0，以及清理门全部保留。
必须有native_setup与四lane kernel ceiling receipt证明，不能以读取配置当实际发送速率。
任一门失败或完整目标<40Gbps停止本候选追加，不补跑/扫描/8GiB。
全部通过且>=40仅允许另写正式M2合同，不把一次样本当M2/CV/稳定性。
证据full-v3，payload-root为/dev/shm/cpnetflux-s0-private55-native57-<最终实现短SHA>。
原2121服务、全局mount/THP、NIC/qdisc不变；无论失败/取消都执行私有卸载及源目标回收。

## 回归停止与并发日志修复范围

740396b两端构建相同，但R2相关10/11，原kernel pacing smoke在合并stdout/stderr中
解析JSON失败（column131）。未启动跨机payload；原始失败日志已保留，不能算通过。
追加允许修改tools/test/run_kernel_tcp_pacing_smoke.py以保留失败原始日志，
最多五次本机小规模诊断取证，非跨机性能样本；先解释日志混写再修复。
已读源码：control_server.cpp用osyncstream(cout)写完整timing，pacing用fprintf(stderr)，
两个stream锁不同但合并到同一个日志文件，存在观测记录互插的风险，尚待原始行确认。
如确认，同步日志修复只涉及src/core/io/persistent_tree_transfer.cpp及必要的
src/protocol/control/control_server.cpp或共享输出工具/相应回归；不改变传输控制或速率。
修复后必须重新提交推送并同步双端构建/SHA、重跑失败项与相关验收，不能沿用740396b传输。

原始复现已保存：第4次本机download的kernel receipt在字符131处被完整timing记录插入，
两个不同stream输出锁的归因获得实际坏行支持。修复为kernel/application两类sender receipt
先在1024B有界元数据buffer格式化，再统一使用与control timing相同的osyncstream(cout)。
不更改协议trace或任意其他日志；新smoke同时从客户端stdout/stderr收集并可保存原始证据。
回执不是每payload热循环的一部分；失败/缺失回执依旧不能过机制门。
此修复改变运行时对象，旧Task56二进制回归不自动覆盖新提交，需补跑相关259类回归。

运行时首次修复的R1相关259/260，旧单测要求应用pacer回执保留stderr；没有修改该验收。
调整为共享detail/diagnostic_output.h对所有参与的结构化记录持锁写入并flush，
保留原stdout/stderr归属：发送端pacing/协议trace仍stderr，timing/sender-work仍stdout。
追加允许该header、tests/unit/framed_data_socket_test.cpp及control_server.cpp必要调用点；
8个并发线程合并两stream输出128条32KiB记录，完整逐条集合为独立oracle，覆盖长记录互插。
协议trace只改输出串行化，不改字段/开关；不将日志接口变化当性能优化。
原R2 10/11与R1 259/260失败保留，修复后重新验收。

## 剩余交付C收口合同

范围：冻结Task57已提交实现118211f及helper来源740396b的实际配置、双端回归与唯一1GiB
可行性证据；离线复算完整任务和准入判定；复核双端现场、明确默认/实验/生产边界及回退；
只提交本次文档，推送GitHub回读SHA，R2同步同文档提交；Windows只复制小报告和清单。
非目标：不再改运行时/测试/CMake，不追加payload、参数扫描或8GiB正式矩阵，不将一次40Gbps
准入或工程交付包装为正式M2，不重启RDMA/DEFERRED项。
受影响文件：本合同、2026-11-12-v3-private-setup-57-result.md及-freeze.json、
README.md、docs/README.md、docs/ROADMAP.md；实验根新增verify-delivery.py、
delivery-acceptance.json、最终双端回执和delivery.json，不提交大日志/构建产物/凭据。
验收：原证据hash、源对象/四二进制/配置匹配、两端261/261及各10次pacing smoke、
8种native生命周期/身份/失败清理和五对准备门通过，完整任务复算40.865344828Gbps、
所有准入门true且M2仍false；本地链接与git diff --check通过；最终两端同提交、清理无残留、
GitHub SHA回读一致及Windows小文件hash匹配。仅文档变化不重跑未改变的完整回归。

交付C离线结果：304个原始证据文件SHA核对、194个文档本地链接、源对象/四二进制/配置、
双端261/261与各10次smoke、native生命周期与五对准备门均通过；
从成功字节与必要准备/目录/client计时复算40.865344828Gbps，全部准入门true，
M2仍false。本轮收口未增加payload，失败/取消的wrapper 124与子进程SIGTERM 143、
卸载前私有probe.bin和最终host路径删除分别按真实证据判定。
最终GitHub回读与双端同步另记实验根delivery.json；下一开发为有界8GiB能力及正式M2合同。
