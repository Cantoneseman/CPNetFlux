# 100G 底层数据通路优化路线

- 状态：superseded；用户明确总目标为基于旧实验数据做底层/架构优化，再在深圳—上海 100 Mbps 服务器上验证。
- 路线版本：`R2026-09-23.2`。
- 输入依据：实时 HEAD `a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；工作区有其他角色留下的文档改动，源码/测试在 HEAD 上无差异。

## 目标

本路线方向已被后续证据驱动路线取代。SENDV-01 作为候选微优化保留，尚未实现或获旧证据支持，不再默认优先。

项目目前只有不可用于本任务现场验收的旧 100 Mbps 环境信息。旧 GridFTP 比较来自 dirty 树、跨配置汇总，不用来推断具体 CPU 瓶颈；新优化的收益必须以固定构建和匹配工作负载实测。

## 非目标

- 不修改 framed wire 格式、checksum/resume/manifest 的外部语义、默认 buffer/chunk/stream 参数或 TLS 安全级别。
- 当前首个任务不改 scheduler、compression、IO backend、校验算法、目录并发策略，也不访问云端或启动性能矩阵。
- 不把本地 socket 行为测试写成 100G 吞吐证明；若收益只体现在减少调用次数，仍须在以后硬件上报告真实吞吐与 CPU/软中断数据。

## 优化顺序

1. **SENDV-01：payload 帧 scatter/gather 发送。** 目前单文件上传和下载发送端对每个 DATA frame 分别发送固定头部与 payload。增加有短写/EINTR 处理的 `sendmsg` 路径，把两段作为同一组 iovec 写出；plain TCP 下每个 data frame 最多一次发送系统调用。TLS 继续走现有安全写路径。两个方向共用该抽象；tree 文件传输最终复用单文件客户端/服务端发送实现。要求逐字节 wire 等价、resume/chunk-complete 不变，并覆盖部分写入。
2. **TREE-IO-02：目录清单持久化写放大。** `tree_transfer_client.cpp` 的每文件状态变更会在持锁时原子重写整份 manifest。先用微基准/调用计数确认成本，再单独设计安全检查点与批量刷新；在该规格通过质量审查前，不降低崩溃后恢复保证或延迟关键 Failed/Completed 持久化。
3. **CPU-CRC-03：校验与复制成本。** 分别检查 CRC dispatch 是否重复、收发压缩缓冲是否多余复制、文件 I/O 的调用批量化。每项只改一个热路径因素，保持校验输出和数据正确。

## SENDV-01 验收

- 上传端与下载发送端的大 payload DATA frame 都使用组合写 API；控制/小帧行为及 TLS 路径保持原有语义。
- socketpair 测试检查组合写的字节序与既有 `encodeFrameHeader + payload` 完全一致，并通过小 socket buffer 与并发 reader 实际触发部分写；单段、空段、EINTR/错误行为有覆盖或有明确不可注入说明。
- 单文件上传、下载、tree upload/download、resume、compression raw/fallback 等相关 smoke/CTest 通过。无工具或缺权限时，将该检查标为 blocked，不能称通过。
- 在固定输入与工具可用时，`strace -c` 或等价 syscall 计数展示每个 DATA frame 的发送调用下降；这验证机制，不取代目标机性能矩阵。
- 新增真实 GridFTP 相对吞吐结论前，要求独立测量实际传输、校验、构建 hash 与两个现场端点配置。目标是每个匹配格 CPNetFlux/GridFTP 中位数比值达到至少 0.90，作为待实测验收门槛，不是当前事实或 readiness 声明。

## 任务边界和阶段门

- G100-SENDV-01：03 实现 API、上传/下载 payload 路径和行为测试；独立 worktree；04 后续审查 diff 与测试。先在本地 Linux 工具链验证。
- G100-TREE-IO-02：待 SENDV-01 验收后再派发，先只读量化 manifest 写放大并提出崩溃恢复契约；未审查前不实现批处理。
- 新 100G 现场暂不可用，故 05 仅维护未来现场配置需求；不续用旧云端 dirty 源码目录，不连接或清理旧主机。

## 路线变化

本路线先 supersede `R2026-09-23.1` 的 runner/telemetry 前置顺序；后续 `R2026-09-23.3` 又将“先实现 SENDV”的默认方向 supersede。保留历史决策、矩阵、QA 和环境回执；新路线以旧原始证据取证、源码映射、选定优化和固定提交跨域复验为主线。
