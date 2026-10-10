# V3 动态目录调度与 V2 统一引擎计划

日期：2026-10-10
状态：用户已授权进入开发规划，尚未开始源码实现
权威工作区：深圳 /root/projects/CPNetFlux
当前输入：df852669d2d412e2aa3d0e0bedb52e311c7471b4

## 用户目标

先做底层性能和可靠性，不先做桌面端实现。目录传输采用动态文件领取和灵活的大文件策略；V2 逐步覆盖 V1 的 checksum、resume、data TLS 和结构化状态，最终让 V2 成为默认且唯一正式引擎。

## 重要取舍

V1 不能在第一步直接删除。它是当前已验证的兼容和恢复参考路径。迁移策略是：
1. V2 先实现功能等价和独立回归；
2. V2 默认启用，旧请求可明确回退并记录原因；
3. 通过固定版本、双向、故障/恢复、TLS、checksum 和性能门；
4. 只有所有门通过并确认没有使用者依赖后，才另立任务移除 V1。
“用 V2 代替 V1”指产品路线，不是立即删除尚未替代的代码。

## V3 动态队列

### 小文件

目录扫描生成不可变 FileTask：file_id、relative_path、size、mtime、generation、transfer_id。任务进入有界环状 ready queue；通道完成当前文件后原子领取下一项。队列只存元数据，不存完整文件内容。

默认调度使用大小感知策略，避免 fileId modulo 的固定分片造成一条通道拖尾。保留静态分片作为回归和对照模式。任何 FILE_RESULT 都必须核对 file_id、generation、path、size，旧结果不得确认新任务。

### 大文件

扫描阶段识别文件大小，调度策略为 auto/small-file/range-stripe。小文件继续文件级并发；只有达到根据通道数、RTT/BDP、最小范围大小和实测范围调度成本计算的阈值，才拆成 offset/length ranges。

范围拥有独立 range_id/generation，并写入同一个临时文件的偏移位置。文件只有所有适用范围成功、manifest 持久化并完成最终校验后才能提交。任一范围失败时，文件保持 recoverable，不把已完成范围伪装成完整文件。

首版不要求实现复杂动态窃取：先实现有界环状队列和静态/动态可切换，再根据 telemetry 增加范围调度。阈值必须可配置但默认自动，不能让普通用户直接输入危险的任意范围值。

## V2 统一可靠能力

按顺序把以下能力接入 V2 文件事务，而不是在 UI 层绕过：

1. V2 FILE_BEGIN/DATA/FILE_END/FILE_RESULT 支持 checksum 算法和块状态；
2. 每文件和目录 manifest 统一记录 V2 mode、attempt、generation、verified ranges、错误；
3. 中断后 V2 resume 读取现有 checkpoint，只补缺失文件/范围；
4. data TLS 在能力协商阶段明确支持/拒绝，不能在 payload 后切换；
5. 结构化 summary/event 同时记录 requested_mode、actual_mode、fallback_reason、transfer/integrity/evidence 状态；
6. V2 fresh/resume、checksum on/off、TLS、取消、断线、目标冲突、旧 manifest 恢复回归；
7. V2 默认启用，V1 仅作为兼容 fallback，所有 fallback 必须可见；
8. 完成使用者和性能门审计后，再单独评估移除 V1。

V2 快速模式当前的 checksum none、data TLS off 约束不能直接暴露给普通桌面用户。产品默认应选择标准校验和安全连接；实验 raw 模式作为明确的高级选项。

## 阶段验收

- 阶段 A：逐文件 telemetry 覆盖 scan/control_prepare/data_connect/read/payload/file_result/commit/manifest_flush/wall；不改变 hash、manifest 和退出码。
- 阶段 B：环状动态队列在 128x1MiB、大小不均目录和 mixed 目录中，相对静态分片降低尾部 wall 或逐文件平均完成时间；文件集合、独立 SHA-256、manifest、取消和错误隔离不变。
- 阶段 C：V2 checksum/resume/TLS 单元、loopback、故障和固定 Linux 构建通过；缺失项显式 blocked/skip。
- 阶段 D：V2 默认与 V1 对照，确认不支持配置的清晰 fallback；再做深圳—上海短实验和 GridFTP 对比。GridFTP 实验前刷新 GSI proxy、复核两端空间、服务和活动批次。
- 所有代码阶段提交明确文件并 push 到 GitHub，记录 commit、远端 SHA、构建/测试命令和证据路径。

## 非目标

本计划不授权直接删除 V1、改变 GridFTP 外部服务、触碰历史 GridFlux-Beta/CPSS(DCC)、清理未知目录、立即加入 QUIC/RDMA 或在未完成门禁前启动大矩阵。