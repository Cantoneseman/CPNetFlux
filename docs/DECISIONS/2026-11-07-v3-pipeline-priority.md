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

第三轮交接提出的TCP 1/2/4 lane伸缩归因与一个因素优化现已执行，见第四轮；不再作为尚未执行任务。仍没有解锁RDMA优化或实现全局独立三阶段worker池，小规模非持久化TCP与旧原生RDMA冷启动成绩分别解释。


## 第四轮 TCP range锁拆分处置（2026-11-07）

[完整结果](../tasks/2026-11-07-v3-tcp-scaling-01-result.md)：60873ba0在eligible POSIX direct路径采用不重叠range锁外writeAtAll；保留slot/attempt排空、校验认证、checkpoint和原子发布，buffered/io_uring仍串行。10个确定性upload/download行为测试，R1全量579/579、R2相关241/241通过；失败记录与复测边界保留。

同提交、分离stdout/stderr取证的2/4 lane四对TCP tmpfs对照，task12.394→17.603 Gbps（+42.03%）、8.640→13.666（+58.17%），八对均更快；Entry wait下降约98%。双端CPU却增加4.39%/46.22%，4 lane接收CPU增加68.75%。保留eligible parallel默认与CPNETFLUX_RANGE_DIRECT_WRITE=serial诊断，不提高默认lane，不称CPU效率改善。单lane控制20.022仍最快，2 lane串行CV11.45%及小样本边界明确保留；不构成长期性能门。

当前下一步：先归因raw pwrite系统CPU/同inode争用、每lane交接与源端服务率；再依据证据选择按文件固定writer/有界队列或其他一个因素，继续独立控制网络lane与writer并发。总字节预算、恢复和版本/哈希/清理门不变。29正式样本与full控制校验通过、两端fixture已清理；全局worker池、小文件微批、100G与CQ/peer-mapping未关闭。


## 第五轮阶段收口实现决定（进行中）

本轮目标是完成独立阶段并发、总字节预算和验收，不再只交付一个局部剖析。局部同inode写入探针显示单writer准入比多writer省CPU，逐帧固定线程派单在单lane会明显回退，因此选择共享FIFO阶段准入与现有异步队列组合；读/CRC和写入并发跨lane独立控制，网络仍由lane数控制。此前“待实现独立全局worker池”的实现形式由此决定替代，并不声称已实现线程池。控制器不在持有许可时等待网络/队列；总预算先按全部lane最坏payload所有权验证，配置不足就拒绝，避免跨端整条stream预算等待死锁。实现与性能验收尚未完成，测试和同版本对照后再做默认处置。详见2026-11-07-v3-stage-decoupling-01任务。
