# V3 摘要 CRC 复用

阶段6的range并发写候选因未达到10%收益已撤下。深圳4 GiB loopback TLS/CRC测试中，完整客户端进程约72.34秒，单独的summary_tree_hash约25.39秒。此任务优化该摘要重复读取成本。

## 范围和行为

原tree_hash格式不可变：按相对路径排序，每个文件依次输入路径UTF-8、0、十进制大小ASCII、0、完整文件内容、0，再输出CRC32C。继续过滤现有CPNetFlux临时/manifest路径并拒绝symlink。利用传输期间已经读取、接收和校验的块CRC32C及offset/length组合文件CRC，再组合为完全相同的tree_hash。复用必须证明从0到文件size连续完整覆盖、长度正确、没有洞/重叠/冲突记录，且数据来自成功校验的V3传输。

上传如有远端已验证块而本端没有其CRC、下载如恢复manifest未能提供完整已验证块，或遇到跳过、取消、传输失败、数据覆盖不足，一律回退原始全文件读取。比较提交后文件类型、size、mtime_ns、ctime_ns；不一致时回退。摘要错误仍按现有方式传播。CRC combine 采用 finalized CRC 与右侧长度为输入，必须由软件CRC参考测试确认。

## 非目标和受影响文件

不改传输协议/TLS/CRC校验、checkpoint batching、pipeline、网络接口、外部完整性验证和计时边界。不允许未证明全覆盖时复用，也不以关闭校验获得收益。预计涉及 include/cpnetflux/checksum/crc32c.h、src/checksum/crc32c.cpp、checksum tests、persistent_tree_transfer.h/.cpp、tree_transfer_client.cpp 和对应单元/集成测试；开工先核实所有权，尽量缩小文件范围。

## 当前实现说明

传输成功后，上传端记录已经成功发送并校验的 CRC32C 分块，下载端记录已经校验并写入的 CRC32C 分块；文件提交时保存 device/inode/size/mtime/ctime 快照。摘要阶段先检查文件集合、身份和分块是否无洞/无重叠/无冲突，再使用 finalized CRC32C combine 重建原 tree_hash。目录总大小小于 128 MiB 时直接走原始全文件读取，因为实测索引与快照检查成本可能超过收益。恢复、跳过、失败、取消或任何身份变化都会回退原算法。

性能矩阵使用深圳同机 loopback TCP、控制和数据 TLS、4 条连接、4 MiB 分块、CRC32C Auto、无压缩、独立 SHA-256；基线为提交 `0d4686cc21f1d8083c189119df576906ce419ae4`，候选为当前工作区构建。4 GiB 5 组配对的完整进程中位数从约 69.39 s 降到 47.56 s，改善 31.31%；摘要阶段约 23.80 s 降到 2.03 s，传输阶段保持约 45.8 s。128 MiB 级 dense 约改善 5.86%；约 72 MiB mixed 在门槛后回退全读，复测中位改善 -0.94%，满足不超过 5% 退化。所有结果独立 SHA-256 一致。原始未设门槛的 mixed 候选曾退化约 5.67%，因此未直接采用，已由门槛修正。

## 验收

1. combine先有失败测试；覆盖空、1字节、短尾、大小边界、4 MiB及多种切分顺序，结果逐位等于crc32c软件计算。
2. 覆盖聚合器拒绝洞、越界、重叠、错误长度及不可信块；空文件处理正确。
3. 树hash黄金用例覆盖空树/文件、嵌套与非ASCII路径、过滤路径、symlink、单/多块和尾块；复用与旧算法逐字节相同。
4. upload/download fresh、resume、skip、取消/失败和文件提交后变化分别验证复用或fallback；最终SHA仍正确。
5. Release完整CTest/脚本门禁通过；4 GiB至少5组交错配对，dense/mixed各5组，独立SHA置于原计时边界外。进程wall中位改善至少10%、其他集合退化不超5%才采纳，否则撤下并记no-go。
6. 深圳提交/push/readback后先把相同源码归档同步上海，两端构建并核对server/upload/download二进制SHA；完成后才能跨服务器传payload。

阶段结果：CRC combine、聚合器拒绝非法覆盖、fresh upload/download 复用、resume/skip/failure fallback、独立 SHA 和性能矩阵均已通过；本任务达到性能门槛，待提交、GitHub 回读和上海同提交构建验收。

阶段结果：CRC combine、聚合器拒绝非法覆盖、fresh upload/download 复用、resume/skip/failure fallback、独立 SHA 和性能矩阵均已通过；本任务达到性能门槛，待提交、GitHub 回读和上海同提交构建验收。
