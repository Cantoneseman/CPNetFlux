# V3 mlx5 rx_out_of_buffer 源码归因核验48结果

日期：2026-11-11
输入提交：efeb656ac366e2a8739c2f82ee9312af94b9de64
状态：DONE / READ-ONLY DIAGNOSTIC；无传输样本、无性能结论。

## 结论

R1 安装的 mlx5 驱动把 rx_out_of_buffer 作为设备 Q counter 导出，并在驱动更新
时累加所查询的 shared-device Q counter。它不是逐 Linux RX queue 计数；对应源码
没有 rxN_out_of_buffer 的导出。R1 已检查的 ethtool、mlx5 debugfs、devlink
health 和 tracepoint 也没有提供这项计数的逐 RQ 归属。

因此，Task46 的接口计数 rx_out_of_buffer +237 仍不能归因到某个 RX queue、
TCP lane 或 CPNetFlux 执行线程。Task47 的 rx13/rx16 page-pool empty/slow 增量
仅说明对应队列走过慢分配路径，不能解释该接口计数。大文件性能候选和 Task46
profile 矩阵继续暂停；已有正式 M1-44 的 8 GiB TCP/tmpfs 中位吞吐仍为
34.159 Gbps，本任务没有新增吞吐结果。

## 精确源码

R1 当前内核为 7.0.0-38-generic，安装的 linux-modules-7.0.0-38-generic
包版本为 7.0.0-38.38；mlx5_core 模块版本同内核，srcversion 为
ECBFB3189DA3C10DEA882D3，固件为 16.35.8002。

核对的 Ubuntu 源码标签为 Ubuntu-7.0.0-38.38，peeled commit 为
740b32353f43e9b00d0350d6fa7b29a6667deabd。
[对应 en_stats.c 源码](https://git.launchpad.net/~ubuntu-kernel/ubuntu/+source/linux/+git/resolute/plain/drivers/net/ethernet/mellanox/mlx5/core/en_stats.c?h=Ubuntu-7.0.0-38.38)

该版本 q_stats_desc[] 只把 mlx5e_qcounter_stats.rx_out_of_buffer 声明为一个
qcnt 统计项。UPDATE_STATS(qcnt) 遍历 mlx5_sd_for_each_dev，对每个有效
priv->q_counter[i] 发起 QUERY_Q_COUNTER，累加返回的 out_of_buffer，
最后写入单个 qcnt->rx_out_of_buffer。该路径没有 queue/channel ID。
per-channel channels 统计从 priv->channel_stats[i]->rq 读取并输出 rxN_*
字段，属于另一组统计；它不把上述 qcnt 值拆回 RX 队列。

## R1 只读现场

- 接口：eno17095np0，32 个 combined channels；driver mlx5_core，
  firmware 16.35.8002。
- 本次空闲时读取的累计值：rx_out_of_buffer=547397、
  dev_internal_queue_oob=0。这是绝对计数，不是传输 delta。
- ethtool -S 有 64 个逐队列 wqe_err / buff_alloc_err 字段，本次均为
  0；逐队列 rxN_out_of_buffer 字段数为 0。
- devlink health show pci/0000:0b:00.0 的 fw、fw_fatal、vnic reporters
  均为 healthy、error 0。健康状态不证明 OOB 未发生。
- /sys/kernel/debug/mlx5/0000:0b:00.0 可见 QP/CQ 等对象信息，没有逐
  netdev RQ 的 OOB 计数。可见的 page_pool tracepoint 只有
  update_nid、state_hold、state_release、release，不提供分配慢路径的
  逐队列事件流。

## 判定与后续

这关闭了“该精确版本已有 ethtool/debugfs/page_pool tracepoint 可直接读取
逐队列硬件 OOB”的假设；没有关闭 OOB 的实际原因。无需为了已有指标继续搜索
常规接口，也不应仅凭 Task47 的慢路径计数调整系统。

下一项是一次有停止门的复现诊断，不是性能优化：同一固定配置下各做一个
1 GiB、1-lane 和 4-lane TCP 任务，关闭详细 profiler，只读采集接口 OOB、
rx_missed_errors、逐队列 packet/page-pool/WQE/buffer、资源和完整性证据。
先核对两端提交、构建、二进制 SHA 与内存资源；任一门失败不启动传输。若只在
4-lane 任务复现 OOB，只能建立并发关联，不能断言具体 lane/队列；若均未复现，
记录该事件在这组条件下未复现并保持根因开放。该轮不得改 NIC/内核参数，也不
扩大成 profile 矩阵。

源码链接：Ubuntu Launchpad exact tag Ubuntu-7.0.0-38.38，commit
740b32353f43e9b00d0350d6fa7b29a6667deabd。
