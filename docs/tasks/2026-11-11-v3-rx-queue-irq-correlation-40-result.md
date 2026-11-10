# 接收队列与 IRQ 关联诊断 40 结果

执行日期：2026-11-11。任务合同于2026-11-10建立，见[计划](2026-11-10-v3-rx-queue-irq-correlation-40.md)。
输入提交：`a1d429157492b6b403273d890d2d0ed881ae5449`；R770-1/R770-2提交、构建配置和运行程序SHA-256一致。

## 结论

按合同只运行一次1GiB完整V3 TCP任务。任务完整性和资源/清理门通过；`rx_missed_errors`与mlx5 `rx_out_of_buffer`均未增加。因此本轮没有复现事件，也没有形成历史计数的队列或流级归因。按边界不追加采样。此前37候选五次合计+2,169、256KiB对照+36的接口计数仍是开放历史现象。

四个高包量RX队列与若干mlx5完成向量在同一采集窗口有增量；没有证据建立队列号与向量的一一映射，也没有丢包事件可用于定位服务缺口。不能据此排除短暂服务不足、归因单条TCP流或声称网络根因已解决。

本次采集开启观察器，4个IRQ样本覆盖约0.285秒。T_ready 0.2872秒、脚本显示的29.91Gbps仅为带观测诊断值，不是吞吐验收或Milestone成绩；不与未观测任务或其他提交比较。本轮未改源码、NIC、路由或系统设置。

## 固定合同与结果

R770-2源到R770-1目标；TCP绑定`192.168.100.2/.1`与`eno17095np0`，不是原生verbs。4 lanes、256KiB DATA、请求1MiB接收buffer（端点读回2MiB/socket）、1GiB驻留tmpfs源与全新tmpfs目标、CRC32C/verified_chunks、1MiB恢复chunk、16-chunk checkpoint、TLS/压缩off、实验mmap接收。

| 项目 | 结果 |
|---|---|
| 完整V3任务 | 通过，1024个chunk校验，4096 DATA帧，8个range，1个已提交成员清单和完成树清单 |
| 外部内容摘要 | 源与接收树均为SHA-256 `d9f70df757715179296407698683d52877c695bf46d05a7a4350dd0569e0cc8d`，1,073,741,824字节 |
| RX缺缓冲 | 接口 `rx_missed_errors` 增量0；mlx5 `rx_out_of_buffer` 增量0；未见buffer allocation error |
| TCP/softnet | 采集到的TCP丢弃/重传筛选计数无增量；softnet dropped与time_squeeze均为0 |
| 驻留与内存 | R2源262,144/262,144页驻留；R1最低MemAvailable 29,621,010,432 B，R2源准备最低29,071,425,536 B，均高于12GiB |
| Swap | R1/R2本轮host `pswpout`增量0；客户端/服务进程owned swap峰值均0 |
| 清理 | R1输出槽位和R1/R2标记tmpfs根已移除；临时49336/61000/61001端口无残留监听 |

R1端点窗口内包数增量最高的接收队列为7:185,930、17:185,928、26:185,942、29:185,922；队列14、16、22、25各增12。4个IRQ样本的首尾差中，完成向量7/17/26/29分别在同号CPU上增加3,382/2,126/2,018/2,050次；其他向量也有增量。驱动向量和队列号未经独立映射验证，且主机计数不是流级计数，故这里只记录共窗活动，不作一一对应或因果结论。

## 构建与后续

两端均为RelWithDebInfo，RDMA/TLS/ISA-L/io_uring构建选项开启；实际任务显式选择TCP。三程序SHA-256一致：server `125db9bf5e21cf98de5ef674d097b894cdcaabe9ed5dbadebdf3c586570e45a5`、download client `20f600140276a636740b43deb172d5ecb8bab81f593733452b7e22a91c76c30c`、upload client `b6c9e9c53360ca0dd0ef8a071518834dd80208ac66a3c1fe38af365fb3fb76b0`。两端Python `-W error -m py_compile`、`cmake --build build -j4`与`ctest -R cpnetflux_receive_network_test`通过。

下一项按路线另立8GiB完整V3 M1任务：固定提交/配置，五对交错完整任务，保留源驻留、12GiB MemAvailable、零owned swap/host换出、SHA、checkpoint/恢复及清理门。1GiB源准备通过和本次网络不复现都不能替代该验收；30Gbps门尚未在8GiB任务上确认，40–50/70–85/100Gbps也仍未验收。

原始证据位于R770-1 `/home/Su/experiments/CPNetFlux/2026-11-10-v3-rx-queue-irq-correlation-40`；任务按2026-11-10预先固定证据路径，实际运行日期为2026-11-11。未将payload或构建产物纳入Git.
