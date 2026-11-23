# R770 CPNetFlux / Globus GridFTP 年度对比任务

日期：2026-11-23
状态：PRE-FLIGHT，等待安全可用的 GSI 凭据与 CA trust directory，尚未启动负载。

## 范围

在当前 R770-1/R770-2 单端口 100GbE 环境下，对 CPNetFlux V3/TCP 和 Globus GridFTP 完成受控文件传输比较。先校准小文件执行、计时与证据采集，再以十进制字节数从 10,000,000,000 开始按 1,000,000,000 递减到 1,000,000,000，为每档创建独立 run ID。每档都要让两种软件各自完整传输一次，并通过双端终态、外部 SHA-256、资源和网络门，才算共同可运行。只有容量/内存门失败可进入下一档；网络错误、数据错误、版本或构建不一致等立即停止。

找到共同通过的最大规格后，按该规格冻结两边各自最高的稳定合规配置，完成上传和下载方向各至少五对交错样本。配置筛选样本与尺寸准入 pilot 不进入正式统计。结果解释面向年度报告读者。

## 非目标

- 不把当前 TCP 实验称为原生 RDMA verbs，不推导 10 条链路的 1 Tbps 结果。
- 不把本任务混入 CPNetFlux 论文 idea、E1 或 E2–E6 研究计划，不据本对比声称研究创新或普遍 HPC 结论。
- 不修改系统级 ring、RSS/IRQ、路由、MTU、THP、swap 或全局服务配置。
- 不提交或记录凭据，不将大 payload、凭据或构建产物加入 Git。
- 不降低资源门槛；任何失败样本保留证据且不进入吞吐统计。

## 验收标准

- R1/R2 输入提交、工作区、版本和构建配置已核验。当前两端和远端均为 2e9c67740985d5f170f24fda490fe23caef06a0b。该提交只更新任务状态文档，runner 和 C++ 测试已在其祖先提交 18635984433bdcccb25c0b9921ab980bb6a6e1c3 验证。实际互传前仍需同提交、同配置构建与相同二进制 SHA-256。
- GSI proxy 可用且有效，临时 GridFTP 服务通过 GSI 控制认证 smoke。GridFTP 数据通道为明文，CPNetFlux 使用记录清楚的认证与校验/提交语义。
- 固定同一不可压缩 payload、同一传输方向、同类 tmpfs 存储及计时合同。数据生成和外部 SHA/清理在 T_ready 之外单列。每个正式样本有独立空目标并经独立 SHA-256 校验。
- 每样本记录 MemAvailable、MemFree、cgroup memory/current/events、tmpfs 用量、swap 与 host pswpout、CPU、TCP retrans、接口/HCA 错误、丢包和实际数据流数。
- 资源门要求 MemAvailable 不低于 12 GiB、pswpout 增量为零、cgroup OOM/事件无新增、tmpfs 空间足够。rx_missed_errors、rx_out_of_buffer、CRC、接口错误或 drops 有任何增量即停止。
- 每方向至少五对交错样本，报告通过样本的中位数、范围、CV、配对速率差/比、T_ready、接口字节率、每 GiB CPU 时间、峰值内存、TCP 重传和流数。
- 年度报告明确 GridFTP 的 GSI 控制认证与明文数据、CPNetFlux 控制认证方式、双方校验和提交差异、拓扑差异及结果适用范围。历史阶段80结果分列，不并入本轮统计。
- 任务范围、结果、runner（若需修改）、证据索引分别归档；仅精确提交本任务文件和必要 runner/报告文件并回读远端 SHA。

## 输入现场

- 初始快照中，R770-1 HEAD 与 origin 为 edd4ad4df7ced563f07fe532124feee865e34d9d，分支 codex/V3-GRIDFTP-OPTIMIZED-RERUN-01，只有两份既有未跟踪 worker-state 文档。R770-2 HEAD 为 1b131bca20c33b17a512ad8840ec50a0a7762807，detached 且工作区干净。
- 准备阶段已把两端同步到当前提交 18635984433bdcccb25c0b9921ab980bb6a6e1c3。两端在 RelWithDebInfo 且相同 CMake 选项下构建通过；相关 27 项 CTest 中 20 项通过、0 失败、7 项因 codec 环境跳过；37 项比较 runner Python 测试全部通过。server、tree upload/download clients 和 unit test 二进制 SHA-256 两端一致，详见实验目录 evidence-index.json。
- Python runner helper 提交为 fc29d58edb20410bbb79f6e69de9b35693cf3309，双端都执行 37 项测试；1863598 收紧了双端网络计数必需条件并补充单端计数缺失测试。两个提交均已推送。CTest 与 Python 测试在 1863598 上重跑通过，详细日志位于 evidence-index.json 所列路径。
- 两台服务器型号均为 PowerEdge R770。接口 eno17095np0，直连地址 192.168.100.1/.2，链路 100 GbE，RX/TX ring 当前 1024/1024。
- 两端物理内存约 32 GB。首次现场 MemAvailable 为 R1 29,227,116 KiB、R2 30,274,496 KiB，约 27.9/28.9 GiB。/dev/shm 约 16 GB。用户 cgroup memory.max 为 max。R1 用户 cgroup 当前约 17.0 GiB，R2 约 1.29 GiB。swap 总量 8 GiB。
- 初始绝对计数 R1 rx_missed_errors/rx_out_of_buffer 为 1829，R2 为 0；netdev errors/drops 和物理 CRC/discard 为 0。样本有效性按传输前后增量判断。
- Globus 客户端 10.13、GridFTP server 13.28。当前没有有效 GSI proxy，默认用户证书目录、CA trust 目录、GridFTP host certificate 与运行中的 Globus 服务均未发现。还未找到将客户端证书 DN 映射至 GridFTP 运行账户 Su 的 gridmap 授权配置。GSI 是正式比较的必要前提，材料配置并通过 smoke 前不生成 payload、不启动负载。

## 预注册配置筛选

配置筛选使用 1 GiB 不可压缩文件，每个候选最多两次，只用于选型。若已有当前现场下合格的等价证据可直接复用，必须核实配置、ring 与资源门。

- CPNetFlux 候选上限 4 个：固定 chunk、checksum 和校验配置下，2-lane pwrite、4-lane pwrite、8-lane pwrite；另测已有的 4-lane private mmap 接收配置（仅在 1 GiB pilot 资源门允许时）。不组合全部参数，不调整系统 ring/RSS。应用 pacing 以现有 4-lane profile 为基线，最多再比较一次无 pacing 变体。
- Globus GridFTP 候选最多 4 个：GSI 控制认证、-nodcau 明文数据、-fast、-p 为 4/8 两档、-tcp-bs 为 1 MiB 基线与至多一次 4 MiB 变体、-bs 262144。该任务是单文件，-cc 只控制多个文件之间的并发，不作为单文件数据流配置；实际流数由日志核验。
- 只有资源、正确性、网络门全通过的候选才可按稳定 T_ready 选最快配置。失败候选完整留档，不换基线或降低门槛。

## 10 GB 至 1 GB 递减规则

候选字节数为 10,000,000,000、9,000,000,000、……、1,000,000,000。每档使用新目录与 run ID，并按顺序分别对两个软件做 admission pilot。每档重新从小 buffer 流式生成同一确定性不可压缩文件。源/目标节点各最多驻留一份完整 payload。完成或失败后核对并清理本档接收目标及较大档源，再确认 tmpfs、MemAvailable/MemFree、cgroup、swap、pswpout 回到可继续的基线。

若样本因 MemAvailable 不足 12 GiB、swap-out、OOM/cgroup、tmpfs 容量或明确内存压力而失败，保存证据并清理后，才可降一档。若网卡/HCA 错误、rx_missed/out_of_buffer、CRC/drop、TCP/协议异常、哈希不符、GSI/认证、版本或构建问题，停止整个递减搜索。1 GB 仍不通过则结束，不再降档。

## 当前决策

本任务是用户明确授权的独立年度报告对比，覆盖 RESEARCH_BASELINE 中“暂不启动新的性能扫描”的旧状态，仅限本任务。2026-11-23 论文 E1 的 56 个有效样本与资源失败的 Stage B pilot 均保持原报告，不混入本任务。当前等待用户在两端安全提供可用的 GSI user certificate/key 与 CA trust directory 路径。
