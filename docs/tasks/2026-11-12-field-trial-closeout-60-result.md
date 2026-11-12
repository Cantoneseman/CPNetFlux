# 阶段60进度：开发闭环，比较与长时稳定性待执行

2026-11-12；状态DEVELOPMENT CLOSED / COMPARISON PENDING / SOAK PENDING。
用户要求保持当前性能、先闭环已开发阶段，再GridFTP比较及稳定性，准备上机候选。
[执行合同](2026-11-12-field-trial-closeout-60.md)、[固定开发版本](2026-11-12-field-trial-closeout-60-freeze.json)。

## 是否有开发了一半的设计

三阶段执行、256KiB DATA/有界buffer、V3租约/CRC/恢复与明确发布窗口已实现并有回归；
没有需要在本轮另建的新性能架构。Task55-58私有后端/监督器/内核ceiling的机制也已完成，
正式M2的资源失败属于未通过实机验收，保持原结论。其默认未晋级不等于代码开发一半。
S1摘要cached路径已显式完成，S1B默认晋级、新微批、原生RDMA优化、策略记忆仍延期；
不借“闭环”扩成新研究项目或宣告这些功能已交付。

这次补齐的真实缺口是恢复回归的fixture/启动失败生命周期：复制源未删除、启动或生成失败
绕过收尾。现在统一try/finally、复制源独占创建与inode身份检查，保留原始输入/诊断和替换对象。
5项行为测试先失败后通过；新测试注册为cpnetflux_corrupt_checkpoint_cleanup_test。
同时更新三阶段预算：lanes×(depth+6)×frame_capacity_bytes；4 lanes/depth2/262148容量
为8388736B，64MiB总payload预算不增。旧Alpha readiness仅标为历史，不替代当前门。

## 已验证的开发闭环

- 实现与受测固定提交b183cdf5530a2e6e21d13f909d070d68d5a73b5e，GitHub回读已确认。
- 双端RelWithDebInfo构建成功，相关CTest各278/278，无skip；R1 109.07s、R2 139.74s。
- 集合含真实损坏checkpoint双向拒绝、现有V3 loopback稳定性/TLS/目录/RST组合、预算、
  取消/超时、中断恢复与发布；这是相关集合，不是全仓所有测试或双机长时稳定性。
- 双端新fixture的source、download复制源和partial消失，日志/manifest保留，旧证据目录未动。
- 四程序SHA与Task58一致，src/include未变，保持当前传输实现和默认；没新增吞吐候选。
- 两端/dev/shm为0，自有cpnetflux-s0根为空，49337无监听；原2121身份不变，测试窗host swap增量0。

## 仍须完成后才评估上机候选

先把固定最新版本交“配置和实验”做新比较，再在选定配置做≥30分钟且≥100完整任务的双机稳定性。
比较覆盖有资源准入的1/8GiB大文件、小/混合目录、双向、配对重复/中位/CV；公平披露明文/GSI/
TLS和控制拓扑/必要准备差异，外部SHA与资源/NIC门不放宽。不得把旧64GiB文件重新用于新测试。
常驻服务资源趋势、取消/断连/恢复、坏checkpoint/数据拒绝、重启以及清理独立验收；
loopback组合不能代替真实数据网。比较和稳定性没有完成前，不叫上机ready。

旧性能参考仍为506b63b五对8GiB中位34.159Gbps；Task57单次1GiB 40.865Gbps只有准入，
不能跨规模/提交算收益。M2/70–85/100G、磁盘持久化、机构安全认证及生产验收没有新增结论。
实验所有者串行交接与真实turn/status回执放实验根handoff.json；总指挥停止根目录写入后才续接，
新成果由该聊天提交，随后只读审核，有问题再串行收回修复。
