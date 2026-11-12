# Task56：每socket内核TCP pacing候选

日期2026-11-12；输入b26018fa9742cd8fe3fb6eef66d6f73520341b0e。
范围：针对Task55接收聚合错误和Task51逐帧timer等待的实际开销，开发默认关闭的
Linux SO_MAX_PACING_RATE路径；只限制CPNetFlux实际sender数据socket，不改系统qdisc/NIC/sysctl。
使用brainstorming确定有界接口，TDD验证API/失败行为。单人串行、仅R770-1源码写入。
现有工具：专用聊天入口与shell可用但不派单；CLI续接本轮不使用；无子代理/worktree。

事实与假设：Task51四lane4096帧发生3879次timerfd等待，完整任务36.478Gbps；
Task55私有大页实际生效，完整目标任务35.710Gbps，但聚合OOB/missed各+261。
当前7.0.0-38内核tcp.h确认SO_MAX_PACING_RATE支持TCP internal pacing，
当前接口为mq/fq_codel，保持不变。新假设是让内核按TCP发送节奏控制，移除逐帧应用定时等待；
是否减少本轮聚合错误和完整任务耗时仍未知，不声称已找到OOB流级根因。
对比方案：继续调旧timer/rate无新机制依据，淘汰；增线程/队列/修改qdisc违反当前边界；
选择per-socket内核机制，接受TCP congestion/内核定时粒度仍可能限制速率的风险。

接口：CPNETFLUX_TCP_KERNEL_PACING_BYTES_PER_SECOND仅off/unset或2000000000。
固定每lane2e9B/s、最多4lane，总TCP socket上限64Gbps；该固定上限给必要目标准备和
40Gbps目标保留理论空间，低于100G物理链路，不进行rate扫描。
旧应用pacing必须off，两者互斥；仅plain TCP/256KiB DATA/无压缩；TLS/RDMA拒绝。
set/get socket选项必须成功且回读匹配，否则显式失败，不能静默不生效。
off不访问socket选项，不新增线程/fd/payload缓冲；reset恢复原socket上限，
socket close释放全部局部状态。receipt区分“回读配置上限”和实际吞吐，不冒称精确整形。

非目标：不修改V3帧/CRC/lease/checkpoint/恢复/发布语义；
不重开Task55相同候选、旧rate/lane/frame扫描、RDMA、微批和DEFERRED研究。
受影响文件：include/cpnetflux/core/io/framed_data_socket.h、
src/core/io/framed_data_socket.cpp、src/core/io/persistent_tree_transfer.cpp、
tests/unit/framed_data_socket_test.cpp，必要的tools/test局部真实机制smoke及CMake注册、
tools/perf/run_private_tmpfs_v3.py（仅经测试的新机制显式选择和receipt门）、
tools/perf/run_small_file_baseline.py（仅可选sender环境/receipt）、tools/perf/private_tmpfs.py（仅传递新候选显式开关）、
tools/test/run_private_tmpfs_test.py、本合同/对应结果、ROADMAP及导航。
如果完整跨机阶段需要额外driver，先补充具体允许文件及输入版本，不临时放宽旧停止门。

验收：先RED后实现，覆盖默认关闭/回读/恢复/无效参数/非TCP/无效fd/close及move所有权；
相关CMake/CTest、真实V3本机传输/中断恢复/发布；候选启用无应用timer receipt。
提交推送、两端同commit/配置/构建与二进制SHA后才允许新机制唯一一次≤1GiB完整V3。
对新跨机实验另写完整合同，保留必要目标准备计时、内容/资源/网络停止门和清理；
任一失败停止，不追加8GiB矩阵或参数扫描，>=40且全门通过只代表后续正式M2准入。

证据：/home/Su/experiments/CPNetFlux/2026-11-12-v3-kernel-tcp-pacing-56。
内核依据：本机/usr/src/linux-headers-7.0.0-38/include/net/tcp.h第1551行附近；
https://www.man7.org/linux/man-pages/man8/tc-fq.8.html；
https://docs.kernel.org/networking/ip-sysctl.html。

## 固定跨机可行性合同（代码提交前）

只运行一次1GiB源/全新1GiB目标，方向R770-2到R770-1；私有tmpfs 1280MiB、
huge=within_size；源驻留准备与SHA另列，必要目标namespace/挂载准备、目录创建及
client创建到成功退出全部计入完整目标任务。沿用Task55原口径，不隐藏必要准备成本。
四路TCP，各socket固定2,000,000,000B/s ceiling，应用pacer/exact-read/TLS/压缩关闭；
256KiB逻辑DATA、1MiB chunk、CRC32C/verified_chunks、checkpoint16、commit_sync=none，
queued sender/inline实验mmap receiver、64MiB/lane窗口、1MiB接收buffer请求。
不增加线程、队列、payload预算或共享系统设置；保留10ms保护采样和既有低频ss，存在观测开销。

输入必须为本任务已提交并推送的代码，两端同提交/RelWithDebInfo配置/三个二进制SHA。
49337临时服务与自有marker路径唯一；原2121服务PID/start身份应保持。
显式192.168.100.2/.1 bind，保存路由、接口、qdisc和计数证据；不修改网络。
四lane ceiling receipt全部匹配、无应用pacer receipt，只证明socket配置，不能当实际线速。
内容门要求完整V3摘要、1024 chunks、独立source/target SHA、传输后源校验。
实际目标VMA页型须采到ShmemPmdMapped>0，但峰值不能外推全文件覆盖率。
资源门保持两端MemAvailable>=12GiB、owned swap=0、host pswpout增量=0、源全页驻留。
窄窗、settled、整体及发送服务窗口的既有网络错误门保持0；OOB共享设备聚合，
不冒称某流/RQ因果。任何门失败立即停止，无补跑、rate/lane扫描或8GiB样本。
只有完整目标任务>=40Gbps且所有门通过才允许另写正式M2合同；本样本不构成M2、
A/B收益、CV或长期稳定性验收。无论失败/取消均执行源、目标、进程组和私有挂载清理。

调用固定为private_tmpfs.py --enable-private-tmpfs --kernel-tcp-pacing --mode v3，
evidence为本证据根/full-v3；payload-root为/dev/shm/cpnetflux-s0-private55-kernel56-<固定提交短SHA>。
保留旧private55内部标签以复用已验收驱动，不复用Task55实际路径或证据。
工程收口包括最终配置/回归/证据hash冻结、GitHub SHA回读、R2同步及Windows小体积查看副本。
