# 阶段78：V3完整目录语义与断连重新派发修复

状态：ENGINEERING CORRECTNESS CLOSED。两端同提交构建/四SHA一致，各完整回归825通过/3原生RDMA环境跳过/0失败。A/B/C对新运行时未验收，上机NOT_READY；用户整体目标OPEN。

## 实现

输入1974c05c1466155098254751ce425e746300c45d；代码提交dbb2d6bac86b29914dbea655b29660937267e38f已推送origin并回读同SHA。单人R770-1根目录开发，未使用子代理/并行worktree。R770-2仅同步该固定提交并构建测试。

1. 为普通目录、空目录、纯目录树和空根增加显式V3元数据；新客户端要求DIRECTORIES=1。DirectoryEntry/Result/MetadataEnd独立于FileBegin，目录不伪装成零长文件、不计payload字节。只由lane0发送目录清单，其它lane发送空结束；CRC覆盖header和身份/路径，逐项确认、父项先于子项。每任务65536目录、单路径4096字节、合计16MiB上限，DATA/控制帧限额不扩大。
2. tree manifest schema2分别保存files/directories及完成事实，两个serializer保持相同字节；摘要输出directory_count/completed_directories/failed_directories，PASS要求完整树均完成。schema1可历史读取但不算完整树完成；上传恢复明确重扫升级，下载旧清单要求新V3任务。每次恢复重新验证/补建目录。
3. 安全mkdirat/openat与O_NOFOLLOW锚定每个路径组件，拒绝绝对/父遍历、symlink及文件冲突。移除配置解析阶段未经V3安全检查的create_directories副作用。只承诺普通目录语义，不承诺整树原子替换/权限/owner/ACL/xattr/链接复制。
4. 修复真实早期控制断连竞态：共享队列recover先于旧批次lease清理，健康lane领到attempt1后报duplicate active range，后续重建批次触发transfer ID变化。改为两个I/O线程停止后先按owner和完整identity释放旧占用，再重新派发；不detach健康lane、不放松身份/CRC、不增加重试或deadline。该通知仅用于服务端发送路径，接收保持原有TransportError传播与已提交文件损坏审计。

## 验证与失败保留

实际客户端首先RED复现空目录遗漏；协议/manifest、安全路径、能力拒绝、取消重放及旧清单测试逐项补齐。真实loopback覆盖1/2lane、upload/download、混合/纯目录/空根共12组，以及空格/中文目录、删除目录再恢复、服务重启、类型冲突、祖先symlink和旧能力拒绝。

恢复竞态有确定性socket+共享queue/batch回归：旧顺序必败，修复后相关14/14单测通过。无trace真实512B早期控制断连64/64；最终二进制又独立64/64通过，原retry2/deadline10s保持。开启trace的12次仅作诊断，不替代无trace证据。

R770-1最终完整CTest：828项中825通过、3项原生RDMA环境跳过、0失败，328.60秒；最终定向4/4通过。此前821/826/828候选的失败日志均保留。一次新回调错误覆盖接收路径导致两项坏数据拒绝等待deadline，已收窄为sender-only，最终完整回归坏数据0.38秒通过，未放宽原3秒门。

R770-2最终完整CTest：828项中825通过、3项同样的原生RDMA环境跳过、0失败，328.34秒；证据已回收到R1的r2/子目录及Windows。二端RelWithDebInfo、tests/TLS/RDMA/ISA-L/io_uring ON、desktop/crossdomain OFF；四二进制SHA已逐项相等，详情见冻结JSON。启用RDMA构建不等于本轮走verbs。

## 证据、清理与下一步

原始证据：/home/Su/experiments/CPNetFlux/2026-11-13-v3-directory-semantics-78；R2证据同前缀加-r2。日志保留RED/候选失败/最终通过，凭据未记录。故障夹具完整压缩归档5283280字节，SHA256=76516f0b5d8880b4ef8c7c49ebf104046d59042173dbf114d02bfcccd9c5fd3c；Windows副本D:/Project/CPNetFlux/_codex_v3_directory_semantics_78/fixtures.tar.gz已核对。随后只删除736个本阶段owned payload（772822816字节），日志/清单仍保留。无本轮跨机payload、网卡修改或新性能成绩；原2121服务不动。

下一步在双端验收完成后串行交给既有“配置和实验”聊天，重新执行1GiB准入、五对大文件及dense/tiny/mixed场景，再满足>=1800秒且>=100任务的稳定性和真实双机故障组；失败即停止清理。RX4096只在用户已授权的新90分钟有界窗口临时启用，到期/失败/完成恢复1024。阶段75/77数字仅属旧运行时，不能搬到新版本；30–50/70–85/100Gbps、8/64GiB资源、真实磁盘持久化、原生verbs和生产身份管理仍未验收。逐目录RTT可能影响WAN多目录时间，不在本次正确性修复中承诺收益。
