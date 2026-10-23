# V3-SUMMARY-CRC-REUSE-01 结果

## 实际解决的问题

大文件传输结束后，CPNetFlux 原来会再次把整个目录文件读一遍，只为了生成 JSON 摘要里的 tree_hash。4 GiB 样本中，这个收尾步骤约占 23.8 秒，拖长了完整进程退出时间。

现在传输过程中直接保存已经成功校验的 CRC32C 分块；传输完成后检查分块是否从 0 连续覆盖到文件末尾，并检查文件的 device/inode/size/mtime/ctime 是否仍与提交时一致。条件都满足时，使用 CRC32C combine 拼出同一个 tree_hash，不再重读文件。任何恢复、跳过、失败、取消、分块缺失/重叠或文件变化都会回退旧的全文件读取算法。目录小于 128 MiB 时也主动走旧算法，避免索引检查反而变慢。

## 代码与测试

- crc32cCombine 使用 finalized CRC32C 和右侧长度做 GF(2) 矩阵组合；覆盖空、1 字节、短尾、4 MiB 边界和不均匀多段。
- 新增树哈希聚合器，拒绝重复路径、零长度、越界、重叠和有洞的分块。
- upload/download fresh 复用摘要；resume、skip、failure fallback 保持全读取。
- 独立 SHA-256 检查每次传输结果。
- 深圳与上海 Release：446 个 CTest 中 445 个通过，1 个 io_uring 条件测试跳过。token smoke 使用一次性测试 token 重跑通过。

## 性能证据

条件：深圳同机 loopback TCP；控制和数据 TLS；4 条连接；4 MiB chunk；64 KiB buffer；CRC32C Auto；无压缩；独立 SHA-256 在计时边界外。

- 4 GiB，5 组交错：完整进程中位数 69.39 s -> 47.56 s，改善 31.31%；summary hash 23.80 s -> 2.03 s；传输核心约 45.8 s，基本不变。
- 128 MiB dense，5 组交错：改善 5.86%。
- 约 72 MiB mixed：首次无门槛候选退化 5.67%，因此加入 128 MiB 门槛；门槛后复测回退全读取，退化 -0.94%，满足不超过 5%。
- 所有运行的目标文件独立 SHA-256 与输入一致；性能矩阵结果保存于深圳 /tmp/cpnetflux-summary-crc-reuse-perf-01/result.json，mixed 门槛复测保存于 /tmp/cpnetflux-summary-crc-reuse-mixed-after-threshold-02/result.json。

## 交付

实现提交：85baa2f42009d073ec229ea0ffabb25b4982d5ca，已推送 origin/codex/V3-DESKTOP-NETWORK-BINDING-01。深圳和上海已同步源码并分别构建；server、tree-upload-client、tree-download-client 二进制 SHA-256 均一致。

下一步只剩最终深圳—上海双机验收；物理多网卡验证继续暂缓。
