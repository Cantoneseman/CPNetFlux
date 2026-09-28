# CPNetFlux 100 Mbps 底层优化选型

- 状态：当前设计工作版本；实现与实验尚未批准。
- 路线版本：`R2026-09-23.4`，承接 `R2026-09-23.3`。
- 固定文档输入：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；最近实现为 `3b0820d`。

## 目标与边界

先针对当前 100 Mbps 深圳—上海环境，为单文件和多文件目录分别找到可解释、可回归的底层优化。旧数据用于确定调查优先级，不当作公平 GridFTP 性能验收。目标是尽快冻结一个最小但有实质收益机会的架构，再实施、做本地 Linux 回归，最终在资源与配置门禁通过后跨域复测真实 GridFTP。

广州—深圳 100G 暂不可用；不外推当前结果。不得在两台云主机的 dirty `/root/projects/GridFlux-Beta` 上开发或覆盖它们，不访问 `/root/projects/CPSS(DCC)`。

## 目前足以指导选型的事实

- 旧原始证据为 218 planned、216 results，来源是 `16b3773` 加 78 条 dirty 状态；不能按单一 commit 重建。
- 04 独立复算 dense 128×1 MiB 上传：CPNetFlux 客户端进程 goodput 中位数从 fp1 的约 18.106 Mbps 增至 fp8 的约 101.431 Mbps；同格 GridFTP 约 93.516/95.170 Mbps。此结果仅是 CPNetFlux 内部并发响应及有配置差异的条件诊断；GridFTP GSI+data-channel privacy，而 CPNetFlux anonymous/TLS-off，且 CPNetFlux 旧行被标为 `fail_correctness`，虽 CSV hash 字段相等。不能称为匹配性能结论，也不能推断具体因果瓶颈。
- 03 静态追踪确认普通 tree worker 在取下一个文件前，会完成该文件控制命令、为每条 stream 新建 framed data TCP/TLS 连接、等待单文件完成并更新 manifest。worker 控制连接可复用；数据连接仍是每文件/每 stream 新建。
- 普通 scheduler 每个活动文件至少进行 transferring 与 completed 两次 manifest 全量原子写，且序列化和写入位于共享 scheduler mutex 内；目前没有锁等待/持久化耗时数据，不能把它定为主因。
- 单文件 DATA 发送目前分别写 frame header 与 payload；`FramedDataSocket::writeAll` 对明文路径循环调用 `send`，TLS 路径使用 TLS write。额外系统调用是源码事实，尚无 CPU/系统调用证据证明它是单文件差距的原因。
- 当前旧数据没有严格匹配配置的 performance-eligible 样本。深圳、上海只读快照仍不足以批准新 build/实验：活动进程归属、峰值 payload/build/evidence 预算以及数据通道安全配置未闭合。

## 设计任务

下一步先由 03 比较跨文件数据连接/会话复用、每 worker 文件流水化、明文 framed header+payload vectored write、manifest checkpoint 写放大等候选，选出单文件和目录各自的最小实施方案。04 独立检查收益论据、协议兼容、TLS、校验/resume、失败恢复及测试门槛。设计任务不得改代码、默认值或 wire protocol。

设计必须区分：

- **单文件路径：** 大文件连续 DATA 热路径的吞吐、系统调用/复制与缓冲；TLS on/off 行为；不得用目录 worker 的并行增益代表单文件改进。
- **目录路径：** fp1 的每文件固定往返/建连成本和 worker 占用；候选如何让固定成本重叠或摊销；是否需要协议版本变化；端点升级与回退策略。
- **指标：** 说明针对 100 Mbps 目标为何会形成可测收益；提出同进程基准或本地可控 RTT/带宽测试，标清它只能验证机制，不能替代深圳—上海对照。
- **语义：** wire compatibility、认证/data TLS、每文件 transfer ID、并发 stream 归属、manifest 状态、partial failure、取消、断连、resume 与重试。

03 交付后由 04 独立审查。00 根据两份证据选择实现任务；实现必须用 `codex/<task-id>` 独立 worktree、固定输入 commit 和文件白名单，由 04 审查。不得因云端构建受阻而停止本地设计/实现准备，也不得将本地性能模拟写成跨域验收。

## 远端验证停止条件

05 尚未确认活动 PID/批次归属和每个挂载点完整峰值预算；上海历史快照约 32–33 GiB 可用本身不等于准入。只有运维给出隔离路径、任务归属、至少 10 GiB 保留加峰值 payload/build/archive/evidence 预算、GridFTP 进程/端口及匹配安全配置，并由 04 接受固定构建/test/hash 证据后，才可派发 SSH 构建或跨域传输实验。清理授权目前为 0。
