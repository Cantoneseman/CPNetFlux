# 阶段69：端口计划的完整文件网络验收及条件式正式比较

输入：阶段68实现42718ac26be24d53ec17525a7b1b3bcb1e340931及随后仅文档的已备份交付提交（串行交接记录给出精确SHA）。阶段67实现与早期失败、阶段68修复回归分别冻结。R1单写，双端相同固定提交/配置/四hash后才能互传。用户完整目标仍是保持现有性能→关闭开发设计→与既有配置和实验聊天正式GridFTP对比→稳定性→现场候选。

阶段A，唯一一次1GiB CP完整文件网络可行性：用阶段64冻结的最强实验配置（4TCP、1MiB chunk/256KiB DATA、CRC32C/verified_chunks、checkpoint16、queued发送、inline+mmap接收、私有tmpfs huge=within_size、每lane1.5e9 B/s应用pacer、exact-read、TLS/compression off、非持久化commit、payload/map64MiB每lane、rxrequest1MiB），只增加--tcp-data-source-port-plan。具体argv/env从原冻结合同核对，禁止凭本文省略flag。R2驻留tmpfs源→R1全新tmpfs目标，显式bind.2/.1；使用阶段67完整512端口表receiver-full.plan，目标q9/27/7/28，RSS/fields必须在使用前后读取核对fingerprint。计划是显式预置配置，冷生成64.72s单列；重复任务只在快照完全一致时复用，任务内读取/验证、加载等必要成本不得隐藏，报告清楚冷配置成本与所测任务边界。

源/目标仅1GiB，不启动新8GiB/64GiB；必须沿用阶段64资源门：准备前两端MemAvailable>=12GiB且MemFree>=2.25GiB，运行时MemAvailable>=12GiB、host pswpout无增量、owned进程/cgroup swap0，保留适用RSS/cgroup上限与特权monitor覆盖门。新payload每端上限8GiB且本次实际<=1GiB，原始数据不覆盖。两端路由/实际tuple/CPU-NAPI/接口及mlx5/HCA错误计数、内容hash、完整任务时间、源准备/目标必要成本和清理证据齐全；不能仅监测wire窄窗。禁止全局RSS/ring/IRQ/THP/cache/swap/网络调整，不动原2121服务或其他项目。

任何内容/资源/网络错误立即停止本stage剩余负载，保留最终宽窗口及刷新计数，不用重跑挑好样本。只有软件端口计划、实际4路队列映射及完整文件门全通过，才允许阶段B。该单样本不关闭OOB总体因果，也不形成稳定吞吐/收益结论。低于40Gbps本身不算本次比较失败（沿用阶段64合同），M2不晋级。

阶段B，重新正式GridFTP比较：从0开始五对同规模交错CP/Grid，不混入阶段64旧版本的唯一有效对。两者使用已有合法最强配置；Grid p4是阶段64已验证最快合法候选，p32触发OOB不得再盲跑。先刷新GSI proxy并核对期限、两端原服务/实验服务身份、空间和路由；认证失败不计性能。相同数据、内存后端、新目标、内容校验及统一完整任务计时，关闭/开启安全模式的结果分开；不得拿不同安全/校验/准备范围作同等结论。每个完整pair都满足资源/错误/清理门，记录median/CV与每次差异；不足5对则报告NO-GO而不报正式倍数。新增目录/反向/安全场景只能在既有阶段64矩阵合同资源允许且前门通过时串行执行，不隐式放大数据。

阶段C，只有正式比较合格后稳定性：至少1800秒且100任务、任务大小/累计字节/频度/并发明确，复用配置不是复用目标，保留成功率、内容、资源/FD/线程/RSS/swap趋势、双端网络错误、data/control恢复、取消/恢复及TLS验证；任何失败记录并停止晋级。完成后生成上机报告，区分TCP/tmpfs非持久化成绩与真实磁盘/原生RDMA/生产安全。没有完成实测不得标READY。

执行与交接：现有“配置和实验”thread01a1a253-6ddf-7f71-bfd7-0eec5550a10f在root停止写入后串行续接。新实验脚本/证据只放独立/home/Su/experiments/CPNetFlux/2026-11-12-source-plan-network-69，不修改旧冻结证据，不操作Git index或修改源码；如发现源码问题停止负载、保留证据并报告，交还root后再修改。根目录只在一个聊天持有写入权时修改。串行交接给出commit/配置/二进制/原服务身份与本轮真实turn/session，禁止只写待派单就当完成。

约定DISPATCH.md/ROSTER本轮在R1已跟踪文件及Windows项目深度2范围未发现；已知准确thread ID和专用调用入口可用，按用户明确既有聊天合作授权串行续接，不推断为工具缺失。此记录不允许并行源码写入或创建新聊天。
