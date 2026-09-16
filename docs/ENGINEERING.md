# CPNetFlux 工程规范

## 1. 目录结构

```
CPNetFlux/
├── INDEX.md
├── AGENTS.md
├── CMakeLists.txt
├── docs/
│   ├── DESIGN.md
│   ├── ROADMAP.md
│   ├── PROJECT_STATE.md
│   ├── GridFTP传输源码学习笔记.md
│   └── ENGINEERING.md
├── src/
│   ├── core/engine/
│   ├── core/session/
│   ├── core/chunk/
│   ├── core/tree/
│   ├── core/buffer/
│   ├── core/io/
│   ├── protocol/command/
│   ├── protocol/auth/
│   ├── protocol/control/
│   ├── storage/
│   ├── checkpoint/
│   ├── checksum/
│   ├── metrics/
│   ├── config/
│   └── common/
├── include/cpnetflux/
├── tests/unit/
├── tests/integration/
├── tests/benchmark/
├── tools/perf/
├── tools/deploy/
├── proto/
├── third_party/
└── cmake/
```

## 2. 代码风格

基于 Google C++ Style，调整如下：缩进 4 空格，允许 C++20 特性，允许异常在非热路径使用。

### 命名

| 类型 | 风格 | 示例 |
|------|------|------|
| 类/结构体 | PascalCase | `TransferEngine`, `ChunkManifest` |
| 函数/方法 | camelCase | `startTransfer()`, `getChunkStatus()` |
| 变量 | camelCase | `chunkSize`, `bufferPool` |
| 成员变量 | 尾部下划线 | `chunkSize_`, `manifest_` |
| 常量 | kPascalCase | `kMaxChunkSize`, `kDefaultPort` |
| 枚举值 | PascalCase | `ChunkState::Completed` |
| 命名空间 | 小写下划线 | `cpnetflux::core` |
| 文件名 | 小写下划线 | `transfer_engine.h` |
| 宏 | 全大写下划线 | `CPNETFLUX_ASSERT` |

### 头文件

- `#pragma once`。
- Include 顺序：本模块 → 项目其他模块 → 第三方 → 标准库，各组空一行。

### 格式化

- clang-format + clang-tidy，配置文件放项目根目录。

## 3. 错误处理

- 数据面（热路径）：不用异常，用 `Result<T>`，所有 IO 必须检查返回值。
- 控制面：允许异常处理不可恢复错误，异常只在模块边界捕获。

## 4. 内存管理

- 数据面：预分配 buffer 池，运行时零 malloc。
- 控制面：智能指针管理生命周期。
- 禁止裸 new/delete。
- 大块内存用 mmap + huge pages（Phase 4+）。

## 5. 并发模型

- 主线程负责控制面。
- IO 线程绑定 CPU 核心，各持独立 IO 后端和 buffer 池。
- 线程间通信用无锁队列或 eventfd。
- 避免共享可变状态，优先消息传递。
- 每条数据连接必须有明确的 `ConnectionContext`，集中保存 fd、状态、所属 worker、pending IO、错误和吞吐计数。

## 6. 传输边界

CPNetFlux 借鉴 GridFTP 的控制面/数据面/DSI 分层，但内部实现保持简单直接。

- Frontend 只解析协议和参数，输出内部 transfer request。
- Phase 3A/3B/3C/3D Frontend 支持最小 FTP/GridFTP 控制面 STOR 上传、RETR 下载、RETR resume 和常用只读元数据命令；STOR/RETR 数据连接仍使用 CPNetFlux framed protocol，不实现普通 FTP raw stream。
- Phase 3D 的 LIST/NLST 例外使用 FTP-style ASCII metadata data channel，只用于目录元数据，不用于文件 STOR/RETR。
- Session Manager 统一生成 chunk plan，不把 `REST`、range、stripe 或恢复逻辑下放到 IO 层。
- Transfer Engine 只消费 `ChunkTask`，按 offset-aware frame 收发数据。
- Storage Adapter 只执行明确的 `pread/pwrite`，不理解 GridFTP 命令或 restart marker。
- Checkpoint / Manifest 是断点恢复事实源，记录 `transfer_id`、输出路径、临时路径、total size、chunk size、状态和 verified chunk checksum；completed range list 只由 verified chunks 派生。
- Checksum 逻辑属于 checksum/session 层，不塞进 socket 收发状态机；socket 层只承载 frame、payload 和最小必要校验。CRC32C backend 选择只由 checksum 层处理，transport 只使用 `ChecksumComputer`。
- Storage Adapter 只读写字节，不理解 GridFTP `REST`、manifest、checksum 或恢复策略。
- `REST GFID:<transfer_id>` 作为 GridFTP-compatible 控制面映射入口，不能替代内部 manifest/range 状态；Phase 3A/3B/3C 不支持 `REST offset` 单偏移恢复。
- Phase 3C 的 RETR resume 必须以下载端 manifest/verified_chunks 为事实源。sender 只按 receiver `ResumeResponse` 中的 missing ranges 发送 chunk，不允许做单 offset 或完整重传伪恢复。
- Phase 5A/5B 的目录传输必须保持分层：tree scan/path validation 只产出稳定 relative file list，tree manifest 只管理 file-level status，单文件 upload/download 继续调用现有 framed STOR/RETR 逻辑。`--file-parallelism` 只能作为 bounded file-level pending queue；不得把 chunk 级调度复制进 tree scheduler。
- Phase 1 不实现完整 GridFTP Mode E、SPAS/SPOR、GSI/DCAU/PROT、server-to-server 第三方传输。

数据帧头应保持定长、易解析、可版本化，至少包含 magic、version、type、stream id、chunk id、offset、length、flags。`transfer_id` 和 `checksum_algorithm` 不塞进热路径 DATA header，当前通过 `SessionInit` 控制帧 payload 协商；Phase 3C 允许 `SessionInit` 追加可选 `source_path` 供 RETR resume 校验，旧 payload 必须保持可解析。热路径解析不得使用异常、动态分配或序列化框架。Phase 2B 使用 `ChunkComplete` 帧把 chunk checksum 与 offset range 绑定，默认算法为 CRC32C；`--checksum none` 仅用于性能对照和兼容回归。Phase 2C 的 `--checksum-backend auto|software|hardware` 是本地实现选择，不改变 wire format。

Manifest flush 可以批量化，但必须满足：失败路径、resume 预检后、commit 前、`Failed`/`Committed` 状态转换前强制 flush；崩溃后允许重传未 flush chunk，不允许误提交或把未验证数据标记为完成。

控制面实现应保持三层边界：command parser 只解析文本和回复码，control session 只维护登录、TYPE、当前工作目录、passive listener、REST token 和 parallelism 状态，数据传输调度只通过内部 options/config 调用现有 framed sender/receiver 逻辑。控制面日志不得打印密码；STOR/RETR/SIZE/MDTM/LIST/NLST/CWD path 必须限制在配置的 `--root` 内，拒绝绝对路径、`..` 逃逸和符号链接逃逸。

目录传输客户端是 CPNetFlux-aware orchestration layer：它可以用 `LIST/NLST/SIZE` 构造目录计划，但文件内容只能通过现有 framed STOR/RETR 传输。默认拒绝 symlink；不保留权限、owner、xattr、ACL 或空目录；changed file 采用 fail-safe，不自动覆盖。

## 7. Git 规范

分支：`main`（稳定）、`dev`（开发主线）、`feature/xxx`、`fix/xxx`、`perf/xxx`。

提交格式：
```
<type>(<scope>): <subject>
```
type: feat / fix / perf / refactor / test / docs / build / chore

## 8. 测试规范

- 每个模块有单元测试。
- 集成测试覆盖跨模块交互。
- 性能测试用 Google Benchmark，结果可复现。
- 故障注入覆盖所有已知异常路径。
- 单元测试全通过才能合并。
- 数据帧解析、chunk/range 合并、manifest 状态转换必须有单元测试。
- 多连接传输测试必须覆盖乱序 chunk、短读短写、连接中断和重复 ACK。

---

## 9. 性能基准

### 分场景目标

| 场景 | 条件 | 目标吞吐 |
|------|------|----------|
| 专线：内存→内存（回环） | 100G loopback | ≥ 80Gbps |
| 专线：内存→内存（跨机） | 100G 直连 | ≥ 90Gbps |
| 专线：文件→文件（NVMe） | 100G 直连 | ≥ 70Gbps |
| 专线：文件→文件（并行 FS） | 100G 直连 | ≥ 80Gbps |
| 虚拟网络 | 25G VPC | ≥ 20Gbps |
| 广域网 | 10G, 50ms RTT | ≥ 8Gbps |
| 广域网（0.1% 丢包） | 10G, 50ms RTT | ≥ 6.4Gbps |

### 功能开销预算

| 功能 | 允许吞吐下降 |
|------|-------------|
| Checksum (XXH3) | < 5% |
| Checksum (CRC32C) | < 3% |
| TLS 1.3 数据面 | 20-40% |
| Manifest 持久化 | < 1% |

### 基线测试要求

每次性能测试记录：硬件环境、系统参数、测试参数、对比基线（iperf3/fio）、结果数据（平均吞吐、P50/P99、CPU 占用）。

Phase 1 基线扫描至少记录连接数、chunk size、block size、socket buffer、pending read/write 数。先做 memory-to-memory，再做 file-to-file，避免一开始把网络瓶颈和存储瓶颈混在一起。

### 性能回归规则

- 合并前运行基准测试。
- 下降 > 5% 需分析原因。
- 下降 > 10% 视为回归，必须修复。

---

## 10. 验证方案

### 基线验证

网卡吞吐、TCP 单流/多流、存储顺序读写、CPU checksum、NUMA 绑定影响。

### 功能验证

单文件上传/下载、大文件、空文件、文件不存在、权限不足、磁盘满、断点续传、checksum 失败、多连接并发、任务取消恢复。

### 故障注入

客户端/服务端进程退出、网络断开/高延迟/丢包、磁盘写失败、checksum 不一致、部分 chunk 失败、节点重启。

### 长稳验证

数小时大文件连续传输、多天批量任务、多任务并发、失败恢复后继续、内存/FD 泄漏观察。

### 验收指标

- 性能：内存到内存接近基线，大文件接近 100G，多流 > 单流，结果可复现。
- 可靠性：中断可续传，重启可恢复，checksum 失败可重传，chunk 不重复，状态不丢失。
- 兼容性：支持所需 GridFTP 命令，不支持的有明确错误。
- 可观测性：实时吞吐、连接级吞吐、chunk 进度、失败原因、重试次数。

---

## 11. 开发策略

### 聚焦核心路径

严格按 Phase 顺序。先让数据跑起来，再逐步加固。每 2 周一个可验证交付物。

### 用测试代替人力

关键路径（传输正确性、断点恢复）必须有测试覆盖。性能测试脚本化，一键回归。

### AI 辅助分工

- 协议解析、命令处理：AI 辅助生成。
- 性能关键路径（IO、buffer）：手写，AI review。
- 测试用例、文档：大量借助 AI。

### 第一版范围控制

- 只支持 Linux。
- 只支持大文件（> 1MB）。
- 不做小文件优化、加密、分布式。
- 不做完整 GridFTP Mode E、XIO/DSI 插件化、SPAS/SPOR、第三方传输。
- 不暴露 layout、striped、net stack、disk stack 等 GridFTP 历史参数。

### 设计原则

**性能：** 零拷贝优先、IO 批量化、无锁热路径、大块传输（1-4MB chunk）、预分配零 malloc。

**可靠性：** 幂等写入、先写后确认、原子提交（rename）、任意时刻 kill 可恢复。断点恢复以 manifest/chunk 状态为事实源，completed chunk 可合并为 range list 用于协商。

**工程：** 模块边界清晰可独立测试、接口先行、测试驱动关键路径、文档即设计。
