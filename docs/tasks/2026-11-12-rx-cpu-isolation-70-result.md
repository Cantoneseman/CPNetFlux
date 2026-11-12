# 阶段70：接收物理核隔离未关闭网络门

输入提交3b1ff97d5bf7bbbc6c59773d91a371f4054e68a0；运行时代码42718ac26be24d53ec17525a7b1b3bcb1e340931与阶段69完全相同。双端固定提交构建成功、完整配置及四二进制SHA一致。没有运行时/协议/默认值改动；复用相同源码与二进制的阶段68双端各156项相关回归，未为文档和实验配置重跑无关全套。

## 结果

**唯一一次1GiB：A FAIL_NETWORK；有效性能样本0，比较NO_GO、稳定性NOT_RUN、上机NOT_READY。** R1 rx_out_of_buffer与rx_missed_errors各+328，最终刷新后没有继续增量；R2相应错误与双端HCA错误增量0。文件完整接收、1024个chunk及外部SHA一致：93d95664e4a6cedc56d43ed633f79a2de43d5041114c91c29a60cdfde81e5aeb。资源、实际大页、原生私有目标准备和应用pacer检查通过。

只增加接收客户端taskset，允许CPU0-6,8,10,13-22,24,26,29-31，避开四个RX物理核的所有SMT siblings。实际14次线程采样，最多16线程；所有观测线程mask与last_cpu均符合要求。原父进程仍允许0-31；IRQ/RPS/拓扑/RSS在使用前后未改变。四条实际tuple独立预测及CPU/NAPI仍与RX队列9/27/7/28一致。无负载四线程继承自检通过。

本次排除了“客户端亲和性没有生效”这一执行错误，但没有证明CPU争用根因，也不能由一次+328与69的+132比较好坏。结论仅为本次物理核隔离仍不足以消除网络错误。不会把此mask作为已验证推荐配置，不追加mask/rate/frame/lane扫描。

## 计时与资源边界

client进程（包括taskset、plan读取和加载）0.243918909s，原始35.216354Gbps。native目标setup+mkdir+client 0.257597006s，原始33.346407Gbps。本轮补计local必要RSS/fields/plan/IRQ/RPS/拓扑预检0.005132427s；相加0.262729433s、32.694984Gbps。**均为失败样本诊断数字，不是合格性能、稳定吞吐或严格用户全流程成绩。** 部署版本审核、源准备1.234228s、外部SHA、监测和清理另外记录；原冷生成64.72s单列，不能隐去。源形状与69相同，随机8MiB重复128次且全部物化驻留，并非两轮bitwise相同。

4TCP，.2→.1明确bind，256KiB DATA、1MiB chunk、CRC32C/verified_chunks/checkpoint16、queued sender/inline+mmap receiver、每lane64MiB、接收请求1MiB、每lane1.5e9 B/s应用pacer；kernel pacing/exact-read/TLS/压缩off、commit_sync=none。为TCP/tmpfs非持久化实验，不称verbs或磁盘成绩。native固定二进制及实验hook记录保留。

双端root observer分别172/160次，包含实际client/server；采样包含扫描开销，不是严格50Hz或全部瞬态峰值证明。MemAvailable最低R1 29396783104B /R2 28991545344B，host pswpout增量0，owned进程及leaf cgroup swap0。共享祖先历史swap不冒称0；实际memory.max/high为max，没有有限cgroup强制上限，应用payload/map预算另列。源准备前后也保存双端网络与资源快照并核对无错误增量。所有原始argv、线程亲和性、源/目标SHA、计数、页型和失败保留。

清理已验证：两端70自有源/目标roots、临时挂载、49337隔离监听与活动数据进程均无；原2121 PID/startticks保持。未改全局网络/IRQ/RSS/RPS/THP/cache/swap，未改CPSS或历史资料。8GiB与64GiB未运行。

## 审计、交付与下一步

独立一致性审计34项通过，证明输入、执行条件、失败记录与清理一致，**不代表网络验收通过**。可无写入重放：`python3 /home/Su/experiments/CPNetFlux/2026-11-12-rx-cpu-isolation-70/audit70.py`；只有最初使用--record写入audit.json，冻结后不要带此选项。

[冻结清单](2026-11-12-rx-cpu-isolation-70-freeze.json)记录原始小证据hash，完整证据位于/home/Su/experiments/CPNetFlux/2026-11-12-rx-cpu-isolation-70及Windows归档副本。GitHub仅备份明确提交的报告/清单/导航；不把外部全部原始文件算作GitHub备份。最终提交/远端SHA/两端同步回执另存E70/delivery.json，避免自引用。

[阶段69](2026-11-12-source-plan-network-69-result.md)的四队列分散与本轮物理核隔离均没有关闭网络门；本轮停止继续同类尝试。后续先针对发送准入/实际TCP突发和RX补给/服务时序取得机制证据，再选一个有根因依据的设计；不能只因聚合OOB或平均CPU比例就加入新节流架构。只读链路快照已保留：两端MTU1500、100000Mb/s、RX/TX ring1024（max8192）、adaptive RX/TX on、mq/fq_codel；内核/固件版本不同按事实记录，不直接认定因果，也未擅自修改。

用户完整目标仍未完成：须先解决网络门，再重新五对正式CP/GridFTP、目录/反向/安全组、>=1800秒且>=100任务常驻服务及故障/取消/恢复/TLS/重启/坏数据，最后上机报告。此两次失败不能替代这些验收，也不晋级M2/100Gbps。
