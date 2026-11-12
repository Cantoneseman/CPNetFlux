# 阶段71：TCP提交粒度与RX补给机制取证

输入提交：4c07af9664a900664dcef3c273ae3df3f8312138；运行时代码保持42718ac。

## 范围、非目标与假设

阶段69四队列分流、70接收物理核隔离均不足以关闭网络错误门，停止同类配置扫描。
本轮由总指挥单人串行实现有界观测：R2 net_dev_start_xmit的目标TCP提交长度/GSO/时序，
R1 typed fentry/fexit 的mlx5e_post_rx_mpwqes队列布局、软件记账WQE数、UMR状态、补给调用间隔/耗时。
先dry-run与至多4KiB显式bind TCP校准；验证过滤、字节记账、四队列覆盖与可清理性后，
才允许一次1GiB原配置机制诊断。该1GiB是获取此前没有的内核路径证据，不是第三个优化候选；
不追加任何frame/rate/lane/affinity扫描。

待检验假设：应用每帧一次准入是否仍对应明显成批的内核TX提交，以及接收补给时序是否出现服务空档。
未观测前不认定突发/UMR/IRQ/固件为根因。相对时间只在各自主机内解释。

非目标：不修改产品运行时/协议/默认配置；不改全局网络、ring、RSS、IRQ、RPS、THP、缓存、swap、信任或凭据；
不优化RDMA/微批/策略记忆，不运行8/64GiB，不重做五对或稳定性。保留2121原服务及无关dirty文件。

## 验收与边界

1. BPF按本机BTF类型编译并成功附着，无unsafe动作；有限时长、maps与日志预算、无丢事件/解析错误；
   校准准确过滤目标tuple，payload至多4KiB，四队列映射与独立RSS预测一致，内容SHA一致。
2. 有条件的唯一1GiB前：两端同固定已提交版本/配置/四二进制hash、MemAvailable>=12GiB/MemFree>=2.25GiB、
   显式.2->.1 bind及路由、原有源驻留/新目标、CRC/verified chunks、root资源与完整接口计数门。
   应用pacer1.5e9B/s/lane、4lane、256KiB DATA、1MiB chunk、TLS/压缩/kernel pacing/exact-read off、commit_sync=none。
   不沿用70绑核候选。全程pswpout增量0、owned进程/leaf cgroup swap0，超界停止并清理。
3. trace是驱动提交/软件RX记账，不能称线速包时刻、精确硬件剩余容量或跨机单向时延；
   tracing开销未消除，因此所得吞吐不计入正式成绩，观察到的丢包也不能单独证明无观测时因果。
4. 网络非零则A仍失败，B/C不运行；即使网络为零，诊断也不替代新五对和>=1800s且>=100任务常驻稳定性。
5. 清理只针对本轮拥有的进程/listeners/fixtures；冻结小证据清单、离线审计、报告、GitHub备份和远端SHA回读。

## 受影响文件与命令

本范围文件、阶段71结果/冻结清单、README与docs导航/路线状态（只在有新事实时更新）。
观测脚本与原始证据放/home/Su/experiments/CPNetFlux/2026-11-12-tcp-rx-service-71，
不把大payload、构建产物、凭据或Linux源码整包提交Git。没有产品源码改动。
验证命令记录于evidence command receipts；bpftrace --dry-run、bounded calibration、离线audit、
两端CMake构建与四二进制hash核对；相同产品源码及二进制的阶段68各156项回归明确标记复用。

## 退出与后续

若typed探针或校准不可用/覆盖不足，停止大负载并记录工具缺口。
若诊断仍不能区分TX成批提交与RX服务问题，留下明确缺口，不凭聚合OOB增加新节流架构。
只有新证据支持的单一机制才制定后续实现；用户完整目标保持开放。
