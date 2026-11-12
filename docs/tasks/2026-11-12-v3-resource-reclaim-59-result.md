# Task59：回归fixture生命周期缺陷修复

输入a1be276；按用户新目标只完成已有清理缺陷，内存后端/缓存释放设计未实施。
真实根因：download复制源在run_direction返回后仍留在server-root，且prepare/start_server/
start_proxy在try外；source生成也在run的try外，异常不经过统一收尾。
现在初始化/复制/启动统一进入try；复制源xb独占创建，清理核对regular与dev/inode，
若已被替换则保留并报告失败；原始输入、日志和manifest不删除。生成失败也记录错误并清理。

5项真实文件系统行为回归先RED（4失败/1预期异常类型不符），修复后5/5 GREEN，
仅网络端点使用故障替身；没有用mock调用次数替代文件与生命周期断言。
R770-1相关CMake构建及278/278 CTest通过，包含真实64MiB损坏checkpoint双向拒绝、
V3稳定性组合、预算/取消/恢复/发布；新fixture无payload.bin/partial遗留，日志与manifest保留，
原73个历史测试目录未删除。原2121身份不变，host pswpout增量0，四二进制与Task58相同。
不是新吞吐或双机长时稳定性成绩；R2固定代码同步验收及最后版本见[阶段60](2026-11-12-field-trial-closeout-60.md)。

只读内存盘点保留在实验根2026-11-12-v3-resource-reclaim-59：R1 build驻留944156672B，
CPNetFlux历史实验文件驻留21087297536B，其中旧64GiB输出驻留14017495040B。
没有读内容、drop cache或删历史payload，pswpout增量0；R2扫描达到60000文件安全上限退出，
无完整归属结果。它解释当前缓存构成，不证明Task58逐页换出因果或资源问题已解决。

新单元回归：python3 tools/test/run_corrupt_checkpoint_cleanup_test.py。
真实fixture与完整相关验收日志/正则/清理回执位于阶段60实验根；RED/GREEN在Task59实验根。
工程修复闭环，性能/资源新设计按用户决定暂停；不继续扩大本任务。

最后双端验收：固定b183cdf后R2相关278/278（139.74s）通过；双端真实fixture清理、
源码/配置/四SHA、原服务和host pswpout复核通过。Task59清理修复已工程闭环；
Task60的新对照和长时稳定性不包含在这278项回归中。
