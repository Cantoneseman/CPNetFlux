# 三阶段流水线优先于 RDMA 资源优化

日期：2026-11-07。用户本轮明确要求先跳过 RDMA 优化，进入三阶段流水线优化。

## 决定和边界

- 当前串行阶段改为源读/校验、网络收发、接收校验/写入的解耦、反压和缓冲所有权。
- 原 RDMA CQ/RQ/SQ/QP、注册池与生命周期优化从“立即下一步”改为暂停；不是完成、取消或验收通过。既有 CQ response/flush 和 peer-mapping 问题继续列为独立未关闭项。
- 数据面仍可使用固定版本的原生 RDMA 作为实验载体；保留显式 bind、同提交构建、摘要、完整性和内存门。使用 RDMA 进行流水线实验不等于优化了 RDMA。
- 接收队列保持有界，默认深度 2 不扩大。FILE_END/恢复/断连必须保证已接受 DATA 的写入和 checkpoint 语义，不能以丢弃队列制造性能收益。

## 首轮状态和性能门

实现 `50efc0e` 将网络/校验与 writer 解耦；`2f7e5cb` 仅删掉未使用的停止 lambda。
1 GiB tmpfs 单 lane 的基线与候选为顺序小样本，文件阶段中位数约 19.37→21.78 Gbps，接收线程合计 CPU 约 0.442→0.686 秒/GiB。队列深度 2→8 没有可重复的完整 task 收益。实现和正确性可以完成一轮交付，但整体性能验收未通过；不能将线程增加或局部 wall 下降作为完成 100 Gbps 的证据。

下一轮先拆分队列交接、线程调度、write/flush/checkpoint/commit 的 CPU 与锁等待，再选一个因素做固定版本对照。已有同步路径的写合并无净收益结果仍有效，不能重复增加复制/缓冲而不测总成本。阶段性能门同时看文件阶段、完整 task、接收线程及双端进程 CPU、峰值 RSS 与恢复完整性；准备时间仍在 task 内。

证据与原始提交边界见 `docs/tasks/2026-11-06-v3-pipeline-01-result.md`。


## 第二轮归因与负结果（2026-11-07）

详见[流水线 02 结果](../tasks/2026-11-07-v3-pipeline-02-result.md)。c572b39 的四对唤醒对照证实减少 waits/switch 不等于降 CPU，故 c876d642 恢复 AvailableCapacity 默认。独立 pwrite 探针显示 writer 主要 CPU 在实际 kernel 写路径。545b929 接通 V3 下载已有 off/full 预分配，准备 wall/CPU 保留在总计时内；三对对照无净收益，默认 off。实现、局部 write_seconds 下降与性能验收分别判断。

第二轮提出的 ownership 预算内无额外复制 gathering write 已完成评估，见第三轮；不再作为待实现任务。旧带复制 coalescing 结果不能当该方案成绩，syscall 数下降不能代替性能验收。独立三阶段全局 worker 池未实现，RDMA 优化仍暂停，CQ/peer-mapping、100G 稳定性门仍开放。


## 第三轮 gathering 处置（2026-11-07）

[实现和 TCP 对照结果](../tasks/2026-11-07-v3-gathered-write-01-result.md)：`57502f30` 保留最多两帧借用 span 的候选、回收修正和实际 partial-write 恢复测试。固定同提交四对 TCP 内存对照，调用减少但完整 task20.099→17.045 Gbps、接收线程CPU+13.54%，因此默认保持 batch1，batch2仅为显式诊断。首版回收问题经确定性 RED/GREEN 修复，两端最终门通过，不能把减少 syscall/writer calls 当验收。

当前下一步改为 TCP 1/2/4 lane 有界伸缩归因，先用现有 timing 观察 range共享事务锁、读/CRC、网络与writer服务率，再选择一个因素实现与对照。没有执行该新实验，没有解锁 RDMA 优化，也没有实现全局独立三阶段 worker 池。小规模非持久化TCP成绩与旧原生RDMA冷启动成绩分别解释。
