# GridFlux 设计文档

## 1. 项目背景

本项目面向算力网场景下的大规模数据传输需求。传统通用文件传输工具在高带宽网络、断点恢复、传输可靠性和业务定制能力方面难以满足项目要求。

GridFTP 曾被广泛用于高性能科学数据传输，具备并行流、断点续传、服务端到服务端传输等能力。但其完整实现复杂，历史包袱重，部分生态对本项目非必要。

本项目采用"对外兼容 GridFTP 接口，对内自研传输引擎"的方式，构建适配项目需求的高性能传输底座。

### 定位

> 面向算力网数据流动场景的 GridFTP-compatible 高性能自研传输底座。

- 控制面兼容必要的 GridFTP 行为。
- 数据面完全自研和优化。
- 功能范围由项目需求驱动。
- 以性能、可靠性、可维护性为优先级。
- 不完整复刻 GridFTP。

### GridFTP 源码经验取舍

`docs/GridFTP传输源码学习笔记.md` 总结了 Grid Community Toolkit 的传输路径。GridFlux 只吸收其中的分层和恢复思想，不复制 Globus 实现：

- 保留控制面、数据面、存储后端分离的模型。
- 控制命令只转换为内部传输任务，不直接读写文件。
- 数据面采用自研 chunk frame 协议，不复刻完整 GridFTP Mode E。
- 存储侧借鉴 DSI 隔离思想，但不兼容 DSI 插件 ABI。
- 断点恢复以 manifest/chunk 状态为事实源，GridFTP `REST` 只作为兼容入口。
- Phase 5A 的目录传输继续沿用该分层：tree manifest 只负责 file-level 编排，每个文件内部仍使用已有 STOR/RETR framed data channel 与 per-file manifest。

## 2. 目标传输场景

### 场景一：专线 TB 级网络

- 网络特征：超高带宽（100G-400G）、超低时延（< 1ms RTT）、几乎零丢包。
- 典型部署：超算中心互联、鹏城-广超直连专线、数据中心间光缆直连。
- 优化方向：极致吞吐，跑满物理带宽。瓶颈在存储 IO、PCIe、NUMA 和应用层效率。
- 关键技术：多流并发、大 chunk、零拷贝、NUMA 绑定、io_uring、存储并行读写。
- 性能目标：端到端 ≥ 11GB/s（100G 网卡）。

### 场景二：虚拟网络

- 网络特征：中等带宽（10G-25G）、较低时延（1-5ms RTT）、偶发微丢包，虚拟化 overhead。
- 典型部署：云平台 VPC 跨节点、容器间数据搬运、虚拟机间文件同步。
- 优化方向：适应带宽波动和 CPU 竞争，共享资源下保持稳定高吞吐。
- 关键技术：自适应并发度、动态 chunk、CPU 亲和性感知、拥塞退避。
- 性能目标：稳定利用可用带宽 80%+，低 CPU 开销。

### 场景三：广域网

- 网络特征：带宽受限（1G-10G）、高时延（10-100ms+ RTT）、丢包 0.1%-1%、链路抖动。
- 典型部署：跨城市/跨区域算力节点间传输，公网或 MPLS VPN。
- 优化方向：克服高 BDP、应对丢包和抖动、保证传输可靠完成。
- 关键技术：大窗口 TCP、多连接聚合、FEC（实验性）、QUIC（实验性）、智能重传、断点续传。
- 性能目标：充分利用可用带宽，丢包下吞吐衰减 < 20%（@ 0.1% 丢包）。

### 场景优先级

Phase 1-3 以专线为主线验证性能上限。Phase 4 引入虚拟网络适配。广域网作为中后期传输后端扩展。

## 3. 项目目标

### 性能目标

- 专线：大文件传输接近 100G 网卡上限（≥ 11GB/s）。
- 虚拟网络：稳定利用可用带宽 80%+。
- 广域网：充分利用可用带宽，丢包下衰减可控。
- 支持多连接、多流、分块并行传输。
- 建立可复现的性能测试方法和基准数据。

### 功能目标

- 基本上传和下载。
- 大文件分块传输。
- 断点续传。
- 完整性校验。
- 失败重试和任务状态恢复。
- Alpha 级多文件/目录传输，支持逐文件 resume，不承诺权限/owner/xattr/ACL 或空目录保留。
- 任务日志、指标和故障定位。

### 兼容目标

- 对上层系统暴露 GridFTP 风格接口。
- 优先兼容项目实际调用的命令。
- 不支持的命令返回明确错误。
- 不承诺完整 GridFTP 生态兼容。

---

## 4. 系统架构

```text
GridFTP-compatible Frontend
        |
        v
Transfer Session Manager
        |
        v
High-performance Transfer Engine
        |
        +--> Transport Adapter (TCP/QUIC/RDMA)
        +--> Storage Adapter
        +--> Checkpoint / Manifest
        +--> Checksum / Verify
        +--> Retry / Recovery
        +--> Metrics / Tracing
```

### GridFTP 经验映射

| GridFTP 概念 | GridFlux 对应设计 | 取舍 |
|--------------|-------------------|------|
| server-control 命令解析 | GridFTP-compatible Frontend | Phase 3 只支持项目 Profile |
| data operation | Transfer Session / Chunk Plan | 控制语义转换成明确任务 |
| data channel / stripe / connection | Transport Adapter / ConnectionContext | 支持多连接，不暴露完整 stripe 语义 |
| DSI | Storage Adapter | 不做插件 ABI，先用静态 C++ 实现 |
| restart marker / range list | Manifest / verified chunks + derived ranges | manifest 是恢复事实源 |

Session Manager 负责把 `REST`、partial transfer、chunk 分配和恢复状态统一转换成 `ChunkTask`。Transfer Engine 只消费明确的 offset/length 任务，Storage Adapter 只执行 `pread/pwrite`，不理解 GridFTP 协议细节。

### GridFTP-compatible Frontend

Phase 3A/3B/3C/3D 新增最小控制面入口 `gridflux-gridftp-server`。它只把 FTP/GridFTP 风格命令映射到内部传输任务或目录元数据查询，不直接复刻完整 FTP 数据面：

- 控制连接支持 `USER`、`PASS`、`TYPE I`、`SYST`、`FEAT`、`PWD`、`CWD`、`CDUP`、`NOOP`、`QUIT`、`EPSV`、`PASV`、`OPTS PARALLELISM`、`REST GFID:<transfer_id>`、`SIZE`、`MDTM`、`LIST`、`NLST`、`STOR` 和 `RETR`。
- STOR/RETR 数据连接仍使用 GridFlux framed protocol；Phase 3A/3B/3C/3D 不兼容普通 FTP raw stream STOR/RETR。
- `STOR` 只能写入 server `--root` 下的相对路径，拒绝绝对路径、`..` 和目录路径。
- 新上传由控制面生成 `GFID:<transfer_id>`；resume 由 `REST GFID:<transfer_id>` 映射到现有 manifest v2 `verified_chunks` / missing ranges。
- `RETR` 只能读取 server `--root` 下的相对普通文件，拒绝绝对路径、`..`、目录和不存在文件；数据端由 `gridflux-file-download-client` 接收 framed DATA/ChunkComplete 并原子 rename。
- Phase 3C 支持 RETR resume；下载恢复事实源位于接收端 `<output>.gridflux.download.manifest`，`REST GFID + RETR` 只提供 transfer id，真正缺失范围由 download client 的 verified chunks 派生。
- Phase 3D 的 `LIST/NLST` 使用 FTP-style ASCII data channel 传递目录元数据；这不是普通 FTP raw STOR/RETR 文件数据通道。
- Phase 5A 的目录 upload/download client 只复用 `LIST/NLST/SIZE/STOR/RETR/REST GFID` 编排多个单文件传输，不新增 GridFTP 递归命令，也不支持 raw FTP recursive data stream。
- `REST offset`、`PORT/EPRT`、TLS/GSI/DCAU/PROT、SPAS/SPOR、Mode E、MLST/MLSD 和第三方传输仍不实现。

所有控制面路径都通过统一 root-confined resolver 处理。`CWD` 只改变 control session 的 root-relative 当前目录，不改变进程工作目录；绝对路径、`..` 逃逸和符号链接逃逸 root 均被拒绝。`LIST/NLST` 输出只包含 entry name、size 和 UTC mtime 等 root 内元数据，不泄露 server root 外真实路径。

### Transport Adapter

上层 Engine 通过统一接口调度数据收发，底层根据网络环境选择传输后端：

| 后端 | 适用场景 | 引入阶段 |
|------|----------|----------|
| TCP 多流 (epoll) | 专线、虚拟网络（基线） | Phase 1 |
| TCP 多流 (io_uring) | 专线（高性能） | Phase 4 |
| QUIC | 广域网（高 RTT、丢包） | Phase 4+ 实验 |
| RUDP + FEC | 广域网（前向纠错） | Phase 4+ 实验 |
| RDMA | 数据中心内部（可选） | 远期 |

### 内部数据面协议

GridFlux 内部不复刻 GridFTP extended block mode。Phase 1 起采用自研二进制数据帧，Phase 2A 在其上增加轻量会话控制帧，Phase 2B 增加 chunk 完成校验帧：

- 协议 magic / version。
- frame type：`DATA`、`FIN`、`COMPLETE`、`ERROR`、`SESSION_INIT`、`RESUME_RESPONSE`、`CHUNK_COMPLETE`。
- DATA/FIN header 固定 64 字节，包含 `stream_id`、`chunk_id`、`offset`、`payload_size`、`total_size`、`status_code`。
- `transfer_id` 通过 `SESSION_INIT` payload 协商，不塞进 DATA 热路径 header。
- RETR sender 可在 `SESSION_INIT` payload 中追加可选 `source_path`；旧上传/STOR payload 不带该字段仍兼容。
- `checksum_algorithm` 通过 `SESSION_INIT` payload 协商，默认 `crc32c`，可用 `none` 做性能对照；`checksum_backend` 是本地进程实现细节，不进入 wire protocol。
- `RESUME_RESPONSE` 返回缺失 range list，client 只补传缺失范围。
- `CHUNK_COMPLETE` payload 携带 `chunk_id`、`offset`、`length`、checksum algorithm 和 checksum value。服务端写入 temp 后用本地计算值校验，只有校验通过的 chunk 才写入 manifest v2 `verified_chunks`。

这样每条 TCP 连接都能独立发送 offset-aware chunk，接收端按 offset 写入。Session Manager 用 manifest `verified_chunks` 派生 missing ranges；`completed_ranges` 只作为人类可读字段。Phase 2B/2C/3C 不做 per-chunk ACK 和重传窗口。

### Manifest 与恢复事实源

Phase 2B 起 manifest 默认写 v2。v2 包含：

- `checksum_algorithm=crc32c|none`。
- `verified_chunks=chunk_id:offset:length:algorithm:checksum_hex,...`。
- `manifest_body_crc32c=<hex>`，用于发现半写或手工篡改。

恢复时服务端先校验 manifest body CRC，再重新读取 temp 文件中已 verified chunk 并计算 checksum。若 temp 中某个 verified chunk 损坏，服务端从 verified set 移除该 chunk，保存 manifest，并在 `ResumeResponse` 中把该 chunk 作为缺失范围返回给 client 补传。若 manifest body CRC 或 verified chunk 元数据损坏，则 resume 失败，不提交最终 output。

Phase 2C 起 CRC32C 支持 backend 自动选择：`auto` 在 Linux x86_64 且运行时检测到 SSE4.2 时使用硬件 CRC32 指令，否则回退 software table-driven 实现；`hardware` 显式请求在不可用时失败。backend 不参与协议协商，因为每端只需要保证相同 algorithm 输出一致。

Phase 2C 起 manifest 默认每 16 个 verified chunk 批量 flush。失败、resume 预检、所有 FIN 后 commit 前和 `Failed/Committed` 状态转换仍强制 flush。崩溃可能导致少量已写 temp chunk 未记录为 verified；恢复时这些 chunk 会被视为 missing 并补传，不会作为已完成事实源。

最终 sha256 只用于测试和性能脚本验收，不是内部恢复事实源。GridFTP-compatible Frontend 的 `REST GFID:<transfer_id>` 只映射到 manifest/chunk plan 的入口，不能替代 manifest + range + chunk checksum。

下载方向在 Phase 3C 使用独立 download manifest。它记录 `source_path`、`target_path`、`temp_path`、`total_size`、`chunk_size`、checksum algorithm、`verified_chunks` 和 `manifest_body_crc32c`。resume 前 download client 读取 temp 中已 verified chunk 重新计算 checksum；损坏 chunk 会被移出 verified set 并作为 missing range 请求 sender 补传。server-side RETR sender 不保存下载状态，只根据接收端 `ResumeResponse` 中的 missing ranges 发送对应 chunk。

Phase 5A 新增 tree manifest。tree manifest 记录目录传输 mode、logical root path、checksum policy 和每个 regular file 的 relative path、size、mtime、transfer_id、status/error。它是目录级 file orchestration 事实源，不替代每个文件内部的 upload/download manifest。目录 resume 遇到 size/mtime 或目标文件状态不一致时采用 fail-safe：标记 `changed` 并失败，不自动覆盖或删除已提交结果。Phase 5B 在该分层上增加 bounded file-level scheduler：`--file-parallelism` 只控制同时处理的文件数，每个文件内部仍走既有 STOR/RETR framed data path 和 per-file chunk manifest。

### 模块职责

| 模块 | 职责 | 阶段 |
|------|------|------|
| Transfer Engine | 数据收发核心，多流并发，buffer 管理，Transport Adapter 抽象 | Phase 1 |
| Storage Adapter | 文件读写抽象，支持 POSIX/io_uring/并行 FS | Phase 1 |
| Session Manager | 任务状态机，chunk 分配与跟踪 | Phase 2 |
| Checkpoint | manifest 持久化，断点恢复 | Phase 2 |
| Checksum | 完整性校验（chunk 级 + 文件级） | Phase 2 |
| GridFTP Frontend | 控制面协议解析，命令映射 | Phase 3 |
| Metrics | 吞吐/延迟/错误率指标采集与导出 | Phase 4 |

### 数据传输流程

**上传：** 客户端发起请求 → 控制面创建任务 → Session Manager 生成 chunk manifest → Engine 建立数据连接 → 客户端按 GridFlux frame 发送 → 服务端写入临时文件 → checksum 校验 → rename 提交。

**下载：** 客户端发起请求 → 服务端查询文件 → Session Manager 生成 chunk 计划 → Engine 并行读取 → 多流发送 → 客户端按 offset 写入 → 完整性校验。

Phase 3B 的控制面下载是完整 framed RETR：control server 在 passive data listener 上运行 server-side sender，GridFlux-aware download client 接收 `SESSION_INIT`、回复完整文件 range、按 offset 写入临时文件，所有连接收到 `FIN` 并返回 `COMPLETE + OK` 后 rename。

Phase 3C 的控制面下载 resume：首次 RETR 中断后保留接收端 download manifest 与 temp 文件；新控制会话执行 `REST GFID:<transfer_id>` + `RETR <path>` 后，sender 在 `SESSION_INIT` 中携带同一 transfer id 与 source path；download client `--resume` 加载 manifest、预检 temp、返回 missing ranges；sender 只发送 missing chunks，最终接收端校验并 rename。

**断点续传：** 中断后保留 manifest 与 temp 文件 → 重连时校验 manifest 和 temp verified chunks → 用 verified chunks 派生缺失范围 → 只传缺失 chunk → 所有 chunk 校验通过后 rename 原子提交。GridFTP-compatible `REST GFID` 只映射到该流程入口，内部事实源始终是 manifest。

---

## 5. 技术选型

### 语言与构建

| 维度 | 选型 |
|------|------|
| 语言 | C++20（主标准，C++23 局部可选增强） |
| 编译器 | GCC 13+ |
| 构建 | CMake 3.20+ |
| 包管理 | vcpkg |
| 平台 | Linux only，x86_64 |

### IO 模型：先闭环，后优化

Phase 1 使用 POSIX socket / epoll / pread / pwrite 打通闭环。io_uring 在 Phase 4 通过基准测试证明收益后引入。

- Phase 1 内核要求：Linux 5.4+
- Phase 4+ 内核要求：Linux 5.10+，推荐 6.1+

### 核心依赖

| 用途 | 库 | 备注 |
|------|-----|------|
| 基础网络 | POSIX socket + epoll | Phase 1 基线 |
| 高性能 IO | liburing | Phase 4+ |
| 日志 | spdlog | |
| 配置 | toml++ | |
| 序列化 | protobuf（manifest/控制面） | 早期可用简单二进制 |
| 测试 | GoogleTest + Google Benchmark | |
| 校验 | XXH3 + CRC32C | 文件级可评估 BLAKE3 |

### 数据面约束

- 不使用序列化框架，裸字节流。
- 不使用动态内存分配，预分配 buffer 池。
- 不使用异常，用 Result<T>。
- 不使用虚函数，热路径用模板多态。
- 最小化内存拷贝。

### 控制面约束

- 允许动态分配、异常、Protobuf。
- 日志详细记录状态变迁。
- 优先可维护性。

---

## 6. 可行性与风险

### 可行性结论

方案技术可行。GridFTP 控制面本质是 FTP 扩展（核心命令 < 20 个），数据面是独立 TCP 连接，天然解耦。项目自用场景降低兼容压力。

### 关键风险

| 风险 | 应对 |
|------|------|
| 100G 是系统工程，非纯软件问题 | 先内存到内存验证，分层定位瓶颈 |
| GridFTP 兼容范围蔓延 | 定义 Profile，不支持的命令直接 502 |
| 复刻 GridFTP Mode E 导致状态机膨胀 | 内部数据面使用自研 chunk frame，控制面只做兼容映射 |
| 可靠性设计后置导致改造成本高 | manifest 数据结构从 Phase 1 预留 |
| 许可证合规 | 不复制源码，只参考协议规范 |
| 加密导致性能下降 | 区分控制面/数据面加密，评估硬件加速 |
| 小文件场景无法接近 100G | 聚合传输策略，区分大小文件路径 |
| 存储 IO 不足拖累网络 | NVMe RAID 或并行 FS，先做内存基线 |
| io_uring 内核版本不满足 | Phase 1 用 epoll，准备回退方案 |
