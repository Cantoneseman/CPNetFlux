# Transfer Engine v2：跨文件持久数据通道

- 决策日期：2026-10-09
- 输入基线：`47b0ca2050288f4a0f2efec846535c5d130b3188`
- 状态：目标已冻结；实现需按下述阶段推进。本文不是性能结果或 readiness 声明。

## 完整目标

Transfer Engine v2 面向目录传输提供有界多文件任务队列，并允许同一条持久 TCP data session 顺序承载多个文件。每个文件必须有明确的 begin/data/end 生命周期和稳定的 `file_id`/generation（或等价、可验证的 framing 关联），文件级校验、manifest/checkpoint、提交和失败结果彼此隔离。任务队列必须有界并提供背压；一个文件失败不能把已提交文件回滚，也不能让下一文件误用前一文件的状态。单文件多流并发是后续阶段，不是本次首个切片的前置要求。

现有逐文件 framed data session 作为 v1 fallback 保留。双方协商不支持 v2、参数不满足安全条件或 v2 session 失败时，客户端按现有逐文件 STOR/RETR 路径执行；不得改变默认传输、manifest/resume/checksum 语义或静默把 v1 数据解释成 v2。

## 与旧目录 lookahead 的关系

目录 lookahead/bounded control pipeline 只提前准备下一文件的控制阶段，最终仍为每文件建立并关闭 framed data session。它可以是 v2 的独立控制优化，但不等于跨文件持久数据通道，也不能作为 Transfer Engine v2 的完整交付。任何 lookahead 代码只有在其不改变 v1 fallback、资源边界和文件顺序时才能独立保留。

## 固定源码事实与当前技术限制

在输入基线中：

- `FrameType` 只有 `DATA/FIN/COMPLETE/ERROR/SESSION_INIT/RESUME_RESPONSE/CHUNK_COMPLETE`；header 没有 `file_id` 或 generation 字段。
- 上传 client 的每个 stream 建立自己的 `FramedDataSocket`，发送一次 `SessionInit`、DATA/ChunkComplete、FIN，并等待 COMPLETE 后返回。
- 上传 server 的 `runFileTransferServerOnListenerTls` 为单个 `FileTransferOptions.path` 初始化一个 `TransferState/TransferSession`；全部 stream FIN 后校验、rename/commit、回复 COMPLETE 并结束该 listener 生命周期。
- 控制面 `STOR`/`RETR` 各自创建一次 passive listener 并同步运行该文件传输；control reply `226` 是该命令完成边界。download sender 同样以单文件为单位接受连接并结束。
- `SessionInitPayload` 标识单文件 transfer_id、大小、chunk 和 checksum；manifest 与临时文件路径属于该单文件 session。

因此，只在 client socket 层循环重发 `SessionInit`/DATA，或只增加 file marker，而不重构控制面路径授权、服务端 session 状态机、每文件 manifest/临时文件 owner、结束/错误回复和连接清理，不能安全实现目录多文件复用。可能造成文件路径绑定错误、generation 串用、manifest 覆盖、文件边界校验丢失，以及一次文件失败关闭后续文件通道。当前固定实现不能证明满足 v2 的隔离与 fallback 要求；不得将 framing-only helper 或 lookahead 声称为持久多文件传输。

## 一期可验收切片

首个运行切片限定为：目录上传与下载均支持一个 worker、一个持久 TCP data session、顺序处理多文件、队列深度/在途文件数为 1；每个文件单独 begin/end 和结果确认，单文件 commit 后才允许提交下一个文件；文件失败只终止当前文件或按明确策略关闭 session，并保留已提交结果。v2 必须显式协商能力，默认关闭；v1 fallback 保持原 wire 行为。TLS 若首期未能证明 session 级边界和失败清理，首期明确 reject v2 并走 v1 fallback，不得降级校验。

## 实现阶段与门禁

1. **协议与控制面契约**：冻结能力协商、v2 session 建立、规范化路径授权、file_id/generation 生命周期、文件 begin/end/result、每文件 checksum/resume/manifest 关联、取消/断连、TLS 约束及 v1 fallback。禁止从未授权的 data-frame path 直接打开任意路径。
2. **服务端单 session 生命周期**：加入有界顺序队列和单文件事务 owner；每个文件分别初始化/验证/提交 manifest 与 temp，成功后发送明确结果并重置单文件状态；失败回收当前文件资源，不污染已提交文件。
3. **客户端垂直切片**：tree upload/download 经显式协商进入持久 session；默认及不兼容情况走 v1；仅队列深度 1，不加入 lookahead、并行 worker 或单文件多流优化。
4. **测试与启用门**：frame/parser golden、路径授权、独立文件提交/失败隔离、取消/截断/重复 generation、队列背压、旧 v1 wire/manifest/hash 回归、loopback upload/download、多次文件同 socket 证明；对照 v1 输出 hash、manifest 与 DATA accounting。性能只在功能门通过后独立测量，不能由 loopback 推断跨域收益。

## 停止条件

若无法从 control plane 将每个 file_id 安全绑定到已授权规范路径，无法为每个文件维持独立 manifest/temp/commit owner，无法精确定义 session 关闭与失败恢复，或无法保持 v1 fallback 字节与行为兼容，则停止 v2 实现并修订契约；不以 lookahead、多个 control socket 或共享单文件 manifest 替代目标。
