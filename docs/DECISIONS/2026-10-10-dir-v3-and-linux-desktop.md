# CPNetFlux 目录 V3 与 Linux 桌面端设计

日期：2026-10-10
状态：设计基线，未授权实现
权威工作区：深圳 /root/projects/CPNetFlux
输入版本：4492d4a891e7d7f24d0039de96b8f3edbd84501a

## 1. 目标

在 100 Mbps 跨域目录传输中，降低小文件控制往返和通道尾部空闲；在不牺牲文件身份、manifest、错误隔离和可恢复性的前提下，让普通 Linux 用户通过桌面端创建、观察、取消和恢复任务。

首阶段桌面端按“Linux 本机目录 ↔ 远端 CPNetFlux 节点”设计。远端节点 A ↔ 节点 B 不进入首个 UI 交付。

## 2. 当前事实

- 当前 V2 persistent tree 在 fresh、checksum none、compression off、scheduler off、data TLS off、connections=1 条件下工作。
- file-parallelism N 表示 N 组独立控制/数据通道，N 上限 8；文件按 fileId modulo N 静态分片。
- 每条通道有界 pending window，默认 2；接收端通道内仍按文件顺序处理。
- V2 与 V1 共享 framed socket、TransferSession/DownloadSession、文件临时提交和 manifest 基础，但 V2 使用新的跨文件 session 生命周期。
- V2 不等于完整恢复/安全路径：显式 resume 或不满足能力条件时走 V1；V2 事务开始后不自动重放为 V1。
- 最新多通道版本已做 loopback 和全 CTest 验证；尚无该版本的深圳—上海/GridFTP 匹配性能结论。

## 3. 目录 V3：自适应混合调度

### 3.1 小文件路径

不把每个小文件再切成多个跨通道片段。每个文件仍以 FILE_BEGIN/DATA/FILE_END/FILE_RESULT 完成一个独立事务；数据按 buffer/chunk 分段发送，但不做跨 TCP 通道的文件重组。

调度器维护有界共享 ready queue。通道完成当前文件后，以原子方式领取下一个文件；队列只保留文件身份、路径、大小和 generation，不缓存完整文件内容。采用大小感知优先级或近似 longest-processing-time-first，避免 fileId modulo 造成某个通道拿到多个大文件而其他通道空闲。

### 3.2 大文件路径

只有当文件大小达到阈值（由 BDP、通道数和最小分片大小共同计算）时，才允许 range striping。文件被拆成带 offset/length 的独立范围，每个范围绑定 stream/range identity；接收端按偏移写入同一个临时文件，使用范围完成表和最终 commit。

小文件不能走 range striping。拆分的收益必须超过额外 begin/result、范围调度、写入随机性和 manifest 记录成本；阈值由 loopback 与跨域实测确定，不写死为用户可随意输入的数字。

### 3.3 背压与失败

- 总通道数、每通道 pending 数、共享 ready queue、读写 buffer 都有硬上限。
- 一条小文件事务失败时，已提交文件保留；未领取文件留在队列，策略决定继续或停止。
- 大文件任一范围失败时，整个文件标记失败，保留范围 checkpoint；不能把部分范围误报成文件完成。
- 取消先停止领取新任务，再取消活跃范围/文件，等待连接线程收口并保存 manifest。
- 动态领取必须校验 fileId/generation/path/size；禁止旧结果确认新文件。
- 目录 manifest 更新串行化，文件级数据流不得持有全局 manifest 锁。

### 3.4 V3 实施顺序

1. 先补 file-level telemetry：scan、control_prepare、data_connect、read、payload、file_result、commit、manifest_flush、wall；按文件输出原始区间。
2. 在不改变 wire framing 的前提下实现共享有界 ready queue 和动态领取；保留静态分片作为回归模式。
3. 对 128×1MiB 和 mixed 目录比较静态分片、动态领取、窗口 1/2/4；只在逐文件平均完成时间和尾部 wall 改善且 hash/manifest 不变时继续。
4. 再实现大文件 range striping；单独测试范围重叠、空范围、断线、取消、resume 和目标冲突。
5. 最后评估异步预读/写回和 io_uring，不能因为名称是异步就默认收益。

## 4. Linux 桌面端架构

### 4.1 进程

- `cpnetflux-desktop`：Qt 6/QML 界面，只负责展示和用户操作。
- `cpnetflux-agent`：本地任务代理，负责连接配置、任务生命周期、子进程隔离、事件聚合、取消和本地历史。
- 现阶段 agent 可以包装已验证的 tree upload/download client；稳定后把相同契约下沉为 C++ library API。
- UI 与 agent 使用本机 Unix domain socket JSON-RPC；界面崩溃不应直接杀掉传输任务。socket 权限限制为当前用户。

### 4.2 首版页面

1. 任务首页：新建上传/下载、运行中、历史任务、失败/可恢复任务。
2. 新建任务：方向、源节点、目标节点、源目录、目标目录、冲突策略、是否恢复。
3. 传输详情：文件进度、字节进度、实时速度、平均速度、ETA、已完成/失败/跳过、当前通道、实际启用模式、错误。
4. 节点设置：地址、端口、用户名/认证方式、受信 CA、默认目录；凭据进系统 keyring，不写 JSON/日志。
5. 高级设置：自动/1/2/4/8 通道、pending window、校验策略、数据 TLS、是否允许 V2；默认隐藏并显示限制说明。

### 4.3 基础 JSON-RPC 接口

请求：

- `profile.list/create/update/delete`
- `node.testConnection`
- `node.listDirectory`
- `task.create`
- `task.start`
- `task.cancel`
- `task.pause`（首版若未实现必须明确返回 unsupported，不伪造暂停）
- `task.resume`
- `task.get`
- `task.list`
- `event.subscribe`

任务创建必须携带 direction、source、destination、requested_mode、channels、pending_window、checksum、resume 和 conflict_policy。

任务记录至少包含：

```json
{
  "task_id": "uuid",
  "state": "queued|scanning|connecting|transferring|committing|completed|failed|cancelled|recoverable",
  "requested_mode": "auto|v1|persistent_tree",
  "actual_mode": "v1|persistent_tree|unknown",
  "direction": "upload|download",
  "files_total": 128,
  "files_completed": 0,
  "files_failed": 0,
  "bytes_total": 134217728,
  "bytes_transferred": 0,
  "bytes_committed": 0,
  "speed_bps": 0,
  "eta_seconds": null,
  "channels_requested": 4,
  "channels_active": 0,
  "data_connections": 0,
  "checksum": "none|crc32c",
  "resume": false,
  "error": null
}
```

事件至少包括 task.state、scan.completed、file.started、file.progress、file.completed、file.failed、channel.state、task.warning、task.completed、task.failed、task.cancelled。事件带 task_id、时间、file_id/relative_path（适用时）、bytes 和错误对象；实时速度由 agent 计算，不能由 UI 猜测。

### 4.4 关键产品规则

- 用户看到“快速模式”时，界面同时显示约束；若协商失败回退 V1，结果页明确显示 actual_mode 和 fallback_reason。
- “传输完成”表示目标文件已提交；“数据已发送”不能单独作为完成。
- 进度同时显示文件数和字节数。
- 校验、认证、加密分开呈现。
- V2 当前不允许把 resume、checksum、data TLS 等选项伪装成支持；不满足条件时自动选择 V1 或明确拒绝。
- 当前引擎 Linux-only；首版不承诺 Windows 本地路径。

## 5. 验收

- V3 动态调度在 128×1MiB、mixed、小文件大小不均的 loopback 中，文件集合、独立树 SHA-256、manifest 和失败隔离与静态模式一致。
- 跨域短测必须记录同一输入、方向、版本、实际 mode、通道数、window、GSI/GridFTP 状态；认证失败和空间不足不计入性能样本。
- agent 崩溃后已有任务不被 UI 进程退出直接删除；取消、重连和恢复状态可读。
- UI 不解析人类化 stdout 作为唯一事实源；使用 JSON-RPC/JSONL 事件和结构化 summary。
- 任何性能结论都同时保留原始 wall、实际 bytes、文件完成数和错误状态，不把并发阶段 sum 当任务 wall。

## 6. 非目标

本设计不授权立即实现 V3、桌面端源码、Windows 支持、远端到远端、QUIC/RDMA、完整 GridFTP 兼容、生产多租户权限、未知目录清理或云端实验。
