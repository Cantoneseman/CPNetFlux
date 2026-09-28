# LOWLEVEL-SENDV-QA-RECHECK-02 独立复核结果

日期：2026-09-24（Asia/Shanghai）
路线/任务：`R2026-09-24.9 / v1`
结论：**PARTIAL**。sendv 原型可以保留在未合入的隔离 worktree，并可作为后续匹配环境验证的候选输入；本轮不批准合入，不批准深圳—上海跨域收益，也不把 loopback 结果写成 100 Mbps/100G 收益。

## 输入与状态

- 主工作区 HEAD：`a076c532640ba06de016ed7ed20f7d2a6d48a0a7`；未操作 index，暂存区为空。共享工作区已有文档改动，未覆盖。
- 实现 worktree：`C:\Users\12563\AppData\Local\Temp\cpnetflux-lowlevel-sendv-impl-01\worktree`，HEAD=`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`，分支 `codex/LOWLEVEL-SENDV-IMPL-01`，status 显示任务白名单内 7 个代码/测试/CMake 改动、未跟踪 `docs/tasks/` 和 `src/core/io/framed_data_socket_internal.h`/`tests/unit/framed_data_socket_test.cpp`；未暂存。
- 外部 bundle：`D:\Project\CPNetFlux-evidence\LOWLEVEL-SENDV-QA-RECHECK-02`。`SHA256SUMS.audit-bundle` 自身 SHA-256 为任务给定的 `b43a129ba2709a80313b65438c0c6c61415e50fed2ca4e682f27c07714fdd8ce`；逐项用 PowerShell SHA-256 重算后 172 个条目全部匹配（退出码 0）。关键指纹也匹配：source archive `d541882f34603aab82a97078cb1e821cb8030df0cffeb5692cecb099835282e9`、新客户端 `b818ea8f489f76533c55dba55dc4d9e463c6a2a00260c513e087a7ce1b65c830`、`summary.json` `88c8c612fb7d2ab13554f14c8a866a51220bcc76f1bf7c587969210c98e24167`、`results-corrected.csv` `b45256a8921877bcd4f01bcc6d88b4745c6de486684fa102a653dfca7a50d19a`。

bundle 内的 `source-worktree.tar` 和 `git-provenance.txt` 都记录基线提交与实现分支，故归档内部链条一致。由于本轮未连接原始 WSL 路径，不能独立证明 bundle 与 WSL 源目录在导出前的逐文件内容一致；该边界保留为证据风险。

## DATA peer trace 复算

只读扫描每个 `strace -ff -yy` 文件的 TCP 对端记录，按 DATA peer 的 `sendto`/`sendmsg` 行计数；没有把不同进程的非 TCP 或日志行计入。每次输入为 256 MiB、4,096 个 64 KiB DATA frame。

| connections | 版本 | 每次 DATA frame | DATA peer 形态（每次） | peer send 调用总数 |
|---:|---|---:|---|---:|
| 1 | before | 4,096 | `sendto`: 4,354 个 64-byte header + 4,096 个 65,536-byte payload；无 `sendmsg` | 8,707 |
| 1 | after | 4,096 | `sendmsg`: 4,096 个 `iovec={64,65536}`、返回 65,600；另有 258 个 64-byte `sendto`；无 65,536-byte `sendto` | 4,611 |
| 8 | before | 4,096（8×512） | 每个 DATA peer 进程 512 frame、546 header `sendto` + 512 payload `sendto`；合计 header 4,368、payload 4,096 | 8,728 |
| 8 | after | 4,096（8×512） | 每个 DATA peer 进程 512 个组合 `sendmsg`；合计 4,096 个组合 `sendmsg`、272 个 64-byte `sendto` | 4,632 |

独立脚本还确认上述 DATA peer 记录的 `write`/`writev` 均为 0；每个 trace 的 frame 数、组合长度、header/payload 计数与 `results-corrected.csv` 和 `summary.json` 相符。`sendto` 总数包含会话/控制类 header，因此不能把 8,707/8,728 当作 DATA frame 数；DATA frame 数由 65,536 payload 或 65,600 组合记录确认。调用数变化为 DATA 发送调用从 8,192 降至 4,096（约 -50%）；包含控制调用的 peer 总数为 1 connection 8,707→4,611、8 connections 8,728→4,632（约 -47%）。

## 未跟踪 wall、退出状态和 hash

`run_ab.py` 显示固定矩阵使用相同 256 MiB source、1 MiB chunk、64 KiB buffer、plain TCP、`checksum=none`、`data_tls_mode=off`，连接数 1/8，各 before/after 各 3 次。corrected CSV 中 12 条未跟踪 wall 记录（每个版本各 6 条）均 `client_rc=0`、`server_rc=0`、source/destination SHA-256 均为 `545a77fd421938559bd8fff15e56493a4fa27d2c5961a661fdc3f6ad7fa8251f`，`hash_match=True`。因此六个 case/模式的输入、退出和目标 hash 证据一致；全部 24 条 trace/wall 记录同样为零退出、hash match。

以下使用 CSV 的 `wall_python_s` 和未跟踪 wall 的 `/usr/bin/time` `SYS`，每格 n=3；离散度为三点总体标准差。`cpu/GiB = system_seconds / (256 MiB / 1 GiB) = system_seconds / 0.25`。

| 格 | wall 原始值（s） | wall median；范围；σ | SYS 值（s） | SYS median；范围；σ | CPU/GiB median |
|---|---|---|---|---|---:|
| before, 1 | 0.255875, 0.281856, 0.301306 | 0.281856；0.255875–0.301306；0.018611 | 0.13, 0.19, 0.21 | 0.19；0.13–0.21；0.033993 | 0.76 |
| after, 1 | 0.189361, 0.214168, 0.225952 | 0.214168；0.189361–0.225952；0.015250 | 0.13, 0.14, 0.15 | 0.14；0.13–0.15；0.008165 | 0.56 |
| before, 8 | 0.201502, 0.239423, 0.280191 | 0.239423；0.201502–0.280191；0.032132 | 0.18, 0.23, 0.31 | 0.23；0.18–0.31；0.053541 | 0.92 |
| after, 8 | 0.226898, 0.254899, 0.282977 | 0.254899；0.226898–0.282977；0.022894 | 0.18, 0.19, 0.44 | 0.19；0.18–0.44；0.120277 | 0.76 |

按原始 Python wall，1 connection 中位数约下降 24.0%，8 connections 上升约 6.5%；按日志两位小数展示，则为 `0.27→0.21 s`（-22.2%）和 `0.23→0.25 s`（+8.7%）。8-stream after 的 SYS=0.44 s 也是明显离散点。每格只有 3 次，wall 约四分之一秒，不能用这三点证明 8-stream 是稳定回归，也不能排除 loopback 调度噪声；同样不能用 1-stream 的下降抵消 8-stream 的反向 wall 结果。被 `strace` 跟踪的 12 条 trace case wall（约 0.6–2.6 s）不参与上述无观测器 wall 比较。

这些数值支持“plain loopback 的 DATA syscall 数量和 system CPU/GiB 下降”的机制证据，但不支持端到端 wall 普遍改善、跨域吞吐提升或 GridFTP 等价。

## 实现静态复核与测试证据

**代码范围与字节推进：PASS（静态）。** `detail::writeSegments` 使用最多两个非空 `iovec` 和 `sendmsg(..., MSG_NOSIGNAL)`；正数短写按当前 segment 的未发送尾部推进，能跨 header/payload 边界；`EINTR` 重试，零写返回 runtime error，其它 errno 走 `systemStatus("sendmsg", errno)`。没有重发已发送 header 或复制整个 payload。

**调用点与 fallback：PASS（静态）。** upload `file_transfer_client.cpp:344-349` 和 download `file_download_sender.cpp:303-308` 仅在 DATA frame、plain TCP、payload 非空时调用 `writeSegments`。TLS、空 payload 和 SessionInit/ResumeResponse/ChunkComplete/FIN/COMPLETE 继续走既有 `sendAll`/`sendFrame` 路径。公共类新增的 `writeSegments` 在 TLS 上拒绝调用，但两个生产调用点已先行分流；该接口可见性仍是后续 API 收口时的窄风险。

**专属测试与构建日志：证据 PASS，04 未重跑。** `tests/unit/framed_data_socket_test.cpp` 使用真实 `encodeFrameHeader`（64-byte header）和 socketpair，覆盖 header+payload 字节拼接、header-only/payload-only、header/跨 iovec/payload 短写、EINTR、零写、peer close/EPIPE 和 `MSG_NOSIGNAL`。bundle 中的固定日志记录 Release/Ninja build 111/111、`FramedDataSocketTest` 6/6、file transfer/resume/checksum smoke 3/3、tree upload/download/resume/parallel/control-reuse/changed/edge/manifest smoke 8/8；这些是可回读的 00/固定快照证据，不冒称 04 本轮执行。`ctest-list-tree-tls.txt` 只证明 data TLS smoke 已注册，没有对应通过日志；专门 data TLS smoke 和完整 CTest 仍未通过本轮独立运行证明。

## 取舍与下一步

- **可保留：** 保留未提交、隔离 worktree 的 sendv 原型；功能字节/错误路径静态检查和固定快照中的功能测试结果与证据 bundle 一致。
- **不可宣称：** 不合入主线，不把 syscall 减半自动等同 wall/goodput 提升，不宣称 100 Mbps、100G 或深圳—上海收益。
- **所需后续：** 先做更大、未跟踪的本机 A/B（至少增加重复数，固定输入/二进制/hash/退出状态，并预注册 wall/CPU 离散度与排除规则），同时单独完成 data TLS smoke；若 8-stream wall/CPU 在扩大样本后仍支持收益，再进入深圳—上海匹配认证和 data TLS 的验证任务。云端验证前仍需按环境门禁核实资源、认证和清理，不因本轮 bundle 可读而启动云端工作。

本轮未运行：构建、CTest、新 A/B、传输、SSH、云端实验和清理。只读检查命令包括 worktree `git rev-parse/status/diff --cached`、bundle SHA-256 清单、CSV/JSON 算术、strace 文本计数、源码定向读取和日志回读。

验收人：00 总指挥（待验收）。
