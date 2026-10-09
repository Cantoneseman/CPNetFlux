# Transfer Engine v2：跨文件持久数据通道

- 决策日期：2026-10-09
- 输入基线：`47b0ca2050288f4a0f2efec846535c5d130b3188`
- 状态：一期单通道双向运行切片已实现并通过 Release 回归；总体架构继续分阶段推进。本文不是跨域性能结果或 readiness 声明。

## 完整目标

Transfer Engine v2 面向目录传输提供有界多文件任务队列，并允许同一条持久 TCP data session 顺序承载多个文件。每个文件必须有明确的 begin/data/end 生命周期和稳定的 `file_id`/generation（或等价、可验证的 framing 关联），文件级校验、manifest/checkpoint、提交和失败结果彼此隔离。任务队列必须有界并提供背压；一个文件失败不能把已提交文件回滚，也不能让下一文件误用前一文件的状态。单文件多流并发是后续阶段，不是本次首个切片的前置要求。

现有逐文件 framed data session 作为 v1 fallback 保留。双方协商不支持 v2 或参数不满足一期条件时，在发送 payload 前选择原逐文件 STOR/RETR 路径。已进入 v2 后的错误必须报告并保留 checkpoint，不能自动重新发送；用户显式 resume 时走 v1。不得把 v1 数据解释成 v2。

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

## 2026-10-09 一期实现契约

用户采用深圳根目录单人串行开发；任务 `DIR-V2-TRANSFER-ENGINE-01`，输入 `410f8a0` 加此前未提交的 framing 草稿。独立 worktree/迁移门已被最新 AGENTS 和云端决策取代。

- 显式 `--data-session-reuse tree`，默认 `off`。仅 fresh、checksum none、compression off、scheduler off、一个 worker/一条数据流、无 max-files、data TLS off；其他客户端配置保留 v1。
- 登录后 `XCPNETFLUX V2` 协商；服务端仅 POSIX、checksum none、data TLS off、默认 manifest flush、preallocate off、commit-sync none 返回 200，否则 504。未登录返回 530。
- 一次 EPSV + `XDIR PUT/GET <relative-directory>`，150 后建立一条 TCP 数据连接。目录在 control root 下授权，拒绝父目录 symlink/逃逸。下载一次本机扫描，省去客户端逐文件 SIZE/MDTM。
- 沿用 64-byte header，新 FILE_BEGIN/FILE_END/FILE_RESULT/DIRECTORY_END 仅用于协商后的路径。streamId=file_id、chunkId=generation，在连接内严格递增。DATA 校验身份、大小和连续偏移。元数据带 transfer_id/path/size/chunk size/秒级 mtime。
- FILE_BEGIN 最多 8 KiB，DATA 最多 64 KiB；在途文件数 1。每个 FILE_END 后独立发布/checkpoint，返回同身份 FILE_RESULT。文件 IO/目标冲突排空到其 END，失败结果不影响后续文件；协议破损/断连关闭 session 并保留部分 checkpoint。
- 30 秒读写空闲超时、TCP_NODELAY、header/payload 合并写。no-replace hard link 发布，保留原 upload/download manifest 和 chunk 完成范围。一期 commit-sync none，不声称断电耐久性。
- 扫描只排除可解析且路径/临时文件归属匹配的内部 checkpoint，普通同名用户文件保留。双向中断 checkpoint 被旧恢复器读取；上传完成真实 v2 中断→v1 续传。
- summary 新增实际 reuse mode、data connect count（v2 observed/v1 estimated）、control_prepare/transfer_complete_wait 及范围。前者为协商/EPSV/XDIR 准备；后者上传含逐文件结果+控制最终回复，下载客户端只测控制最终回复，不能当同一分解。v2 wire 是双向应用 frame，非 TCP/IP 线速计数。

Release CMake 构建完成；CTest 222 项：221 pass、1 个 io_uring 环境测试 skipped、0 fail。128×1MiB 上传/下载各仅一条实际数据连接，独立 SHA-256、manifest、v1/fallback、错误隔离、空/嵌套文件、timeout/disconnect/replay 均有测试。日志与限制见任务记录；没有本版本的新跨域/GridFTP 性能结论。

## 总体架构尚未交付部分

固定总通道预算下的多文件并发、有界 pending/lookahead 队列、共享缓冲池和异步读写流水、v2 多流及校验/TLS/resume，均未由一期完成。后续先测一期跨域收益，再在稳定文件事务边界上增加有界调度；持久单连接不等于完整 Transfer Engine v2。
