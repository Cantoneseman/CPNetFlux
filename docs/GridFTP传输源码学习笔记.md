# GridFTP 传输源码学习笔记

## 1. 阅读范围

本笔记基于 Grid Community Toolkit 源码仓库：

- 仓库：`https://github.com/gridcf/gct`
- 本次阅读 commit：`1be1f66dd4fdb25100091fe7ebd2a506762494e2`
- 关注范围：GridFTP 传输控制、数据通道、文件 DSI、客户端传输属性与断点续传。

重点源码文件：

| 模块 | 文件 |
|------|------|
| 服务端命令解析 | `gridftp/server-lib/src/globus_gridftp_server_control_commands.c` |
| 服务端控制状态 | `gridftp/server-lib/src/globus_gridftp_server_control.c` |
| 服务端数据调度 | `gridftp/server/src/globus_i_gfs_data.c` |
| 服务端 DSI 接口 | `gridftp/server/src/globus_gridftp_server.h` |
| 文件 DSI | `gridftp/server/src/modules/file/globus_gridftp_server_file.c` |
| FTP control 数据通道 | `gridftp/control/source/globus_ftp_control_data.c` |
| stripe layout | `gridftp/control/source/globus_ftp_control_layout.c` |
| 客户端传输 | `gridftp/client/source/globus_ftp_client_transfer.c` |
| 客户端属性 | `gridftp/client/source/globus_ftp_client_attr.c` |
| restart marker | `gridftp/client/source/globus_ftp_client_restart_marker.c` |

本笔记只提炼行为、结构和设计启发，不复制 GridFTP 具体实现。

---

## 2. 总体传输模型

GridFTP 的传输路径可以拆成四层：

```text
控制连接 FTP/GridFTP command
        |
        v
server-control 命令解析与状态管理
        |
        v
server data layer 创建 send/recv/list/stat 操作
        |
        v
DSI 后端执行存储读写，并通过 data channel 收发数据
```

其中最关键的分离是：

- 控制面负责命令解析、参数协商、认证、路径、错误码、数据连接准备。
- 数据通道负责真正的 TCP 连接、并行流、stripe、读写回调。
- DSI 负责后端存储，默认 file DSI 只是其中一种实现。

对 GridFlux 的启发：可以保留这种分层思想，但不要复刻 Globus 的复杂状态机。GridFlux 应直接定义自己的 `Frontend -> SessionManager -> TransferEngine -> StorageAdapter` 路径。

---

## 3. 控制命令到传输任务

### 3.1 命令注册

服务端在 `globus_gridftp_server_control_commands.c` 中注册命令表，传输相关命令包括：

- `PASV` / `EPSV`
- `SPAS`
- `PORT` / `EPRT`
- `SPOR`
- `REST`
- `RETR`
- `STOR`
- `ERET`
- `ESTO`
- `OPTS`
- `SIZE`

`RETR`、`STOR`、`LIST`、`NLST` 等会进入同一个传输命令处理函数，然后根据命令名决定操作类型：

```text
STOR / ESTO / APPE -> server recv
RETR / ERET        -> server send
LIST / NLST / MLSD -> server list
```

对 GridFlux Phase 3：第一版只需要保留 `USER/PASS`、`TYPE I`、`SIZE`、`PASV/EPSV`、`REST`、`RETR`、`STOR`、`OPTS PARALLELISM`、`QUIT`，其余命令先返回 `502`。

### 3.2 `OPTS RETR` 并行参数

GridFTP 支持通过 `OPTS RETR` 设置传输参数，源码中解析的典型字段包括：

- `stripelayout`
- `parallelism`
- `packetsize`
- `windowsize`
- `blocksize`
- `markers`

其中 `parallelism` 会写入服务端控制 handle 的传输选项。源码里还有限制过大 parallelism 的逻辑。

对 GridFlux 的建议：

- Phase 1 命令行优先支持 `--connections`。
- Phase 3 再映射 `OPTS RETR parallelism=N` 到内部连接数。
- 建议设置上限，例如 `1..64`，默认 `4` 或 `8`，并在日志中记录实际生效值。

### 3.3 `PASV` / `EPSV`

源码将 `PASV`、`EPSV`、`SPAS` 统一放进 passive data setup 路径。不同点是：

- `PASV` 返回 IPv4 host/port。
- `EPSV` 返回扩展格式，适合 IPv6 或只暴露端口。
- `SPAS` 是 striped passive，可能返回多个地址。
- `EPSV ALL` 会让服务端进入 passive-only 语义。

对 GridFlux 的建议：

- 第一版支持 `EPSV` 优先，`PASV` 兼容。
- 不做 `SPAS`，因为 GridFlux 的多流可以由一个控制会话内部创建多个数据连接，不必暴露完整 striped GridFTP 语义。

---

## 4. `REST` 与断点续传

### 4.1 Stream mode

`REST` 参数如果不包含 `-`，源码按 stream mode 处理，语义是：

```text
REST offset
```

服务端内部会记录一个 range：`0..offset`，表示这部分已经完成，后续 `STOR` 或 `RETR` 从对应偏移继续。

### 4.2 Extended block mode

`REST` 参数如果包含 `-`，源码按 range list 解析，例如：

```text
REST 0-1024,2048-4096
```

服务端会把多个已完成或待处理区间存进 range list，后续传输根据 range list 计算读写范围。

### 4.3 对 GridFlux 的启发

GridFTP 的 REST 模型是“协议层记录 restart marker”，而 GridFlux 更适合“manifest 是事实源”：

- manifest 记录每个 chunk 的状态、offset、length、checksum、重试次数。
- REST 只作为兼容入口，把 offset 转换成 chunk 状态。
- 对普通客户端：`REST offset + STOR/RETR` 可以恢复单偏移。
- 对 GridFlux 自家客户端：优先用 manifest 做 chunk 级续传。

建议 Phase 2 的断点恢复不要只依赖单个 offset。单 offset 对大文件多流传输不够精确，chunk manifest 更稳。

---

## 5. 服务端数据层

### 5.1 data operation

服务端数据层 `globus_i_gfs_data.c` 把控制层请求变成内部 operation。关键路径：

```text
request_recv -> 创建 recv operation -> 绑定 data handle -> 选择 DSI -> 授权 -> 调用 dsi.recv_func
request_send -> 创建 send operation -> 绑定 data handle -> 选择 DSI -> 授权 -> 调用 dsi.send_func
```

该层负责：

- data handle 查找和状态切换。
- range list、partial offset、partial length 保存。
- node/stripe 信息保存。
- DSI 选择。
- 传输开始、完成、事件、错误回调。

### 5.2 DSI 隔离

DSI 接口定义在 `globus_gridftp_server.h`，核心函数包括：

- `send_func`
- `recv_func`
- `list_func`
- `stat_func`
- `active_func`
- `passive_func`
- `command_func`

默认 file DSI 注册了 `send`、`recv`、`event`、`command`、`stat`、`realpath` 等回调。

对 GridFlux 的启发：

- `StorageAdapter` 应该像 DSI 一样隔离存储实现。
- 但 GridFlux 不需要保留 DSI 插件 ABI，早期用 C++ 接口即可。
- 热路径不要用虚函数这一点已经写入 `ENGINEERING.md`，可以用模板或静态多态包住 POSIX file adapter。

---

## 6. range 计算与 stripe

服务端数据层有两个重要 helper：

- `globus_gridftp_server_get_write_range`
- `globus_gridftp_server_get_read_range`

它们负责把 restart、partial transfer、stripe layout 转换成 DSI 看到的读写 offset 和 length。

核心思想：

- DSI 不直接理解 `REST`、partial、mode S/E 的协议细节。
- 数据层把这些协议语义翻译成“从哪里读、写到哪里、长度多少”。
- striped/partitioned 模式下，根据 node index、node count、stripe block size 计算当前节点负责的数据区间。

对 GridFlux 的建议：

- `SessionManager` 统一生成 chunk plan。
- `TransferEngine` 只消费明确的 `ChunkTask { file_id, offset, length, checksum_state }`。
- `StorageAdapter` 不理解协议，只执行 `pread/pwrite`。
- 不要把 REST、chunk 分配、stripe、存储 IO 混在同一模块。

---

## 7. 数据通道与多连接

`globus_ftp_control_data.c` 是数据通道核心。它维护：

- `globus_ftp_data_connection_t`
- `globus_ftp_data_stripe_t`
- `globus_i_ftp_dc_transfer_handle_t`

结构关系：

```text
transfer_handle
    |
    +-- stripe[0]
    |      +-- connection[0..N]
    |
    +-- stripe[1]
           +-- connection[0..N]
```

每个 stripe 有：

- 空闲连接队列。
- 已建立连接列表。
- 待执行 command queue。
- listener handle。
- parallelism 配置。
- EOF/EOD 状态。

### 7.1 stream mode

stream mode 类似普通 FTP 数据连接。源码中如果 mode 是 stream，服务端会强制 `nstreams = 1`。

### 7.2 extended block mode

extended block mode 支持：

- 多连接。
- stripe。
- offset-aware 写入。
- EOF/EOD 分离。
- 连接复用。

GridFTP 的并行传输主要建立在 extended block mode 上。

对 GridFlux 的取舍：

- 不必实现完整 Mode E。
- Phase 1 可以直接设计自有二进制 data protocol，每个 frame 带 `transfer_id/chunk_id/offset/length/flags`。
- 这样既能支持多连接，也能避免 GridFTP Mode E 的历史状态机复杂度。
- Phase 3 GridFTP 兼容层只做控制面映射，不暴露完整 Mode E 语义。

---

## 8. file DSI 读写流程

默认 file DSI 在 `globus_gridftp_server_file.c` 中实现。

### 8.1 接收上传 `STOR`

上传路径大致是：

```text
file_recv
  -> 获取 optimal_concurrency 和 block_size
  -> 初始化 monitor 和 buffer 池
  -> get_write_range
  -> 打开目标文件
  -> begin_transfer
  -> 注册多个 data read
  -> read callback 中写文件
  -> 所有 pending read/write 完成后 close
  -> finished_transfer
```

注意点：

- file DSI 用 monitor 记录 pending reads/writes、buffer list、offset、错误状态。
- 上传时先从数据通道读，再写本地文件。
- `expected_checksum` 可用于收尾校验。

### 8.2 发送下载 `RETR`

下载路径大致是：

```text
file_send
  -> 获取 optimal_concurrency 和 block_size
  -> 初始化 monitor 和 buffer 池
  -> 打开源文件
  -> get_read_range
  -> 文件 read callback
  -> register_write 到数据通道
  -> write callback 回收 buffer 并继续 dispatch read
  -> close
  -> finished_transfer
```

注意点：

- 文件读和网络写通过 callback 串联。
- buffer 复用，pending 数控制并发。
- range 由上层数据层计算，file DSI 不直接解析 REST。

对 GridFlux 的建议：

- Phase 1 也采用固定数量 buffer + pending IO。
- 先实现简单版本：每连接一个 worker，每 worker 一个 buffer pool。
- 后续再做跨连接 chunk 调度和 NUMA 绑定。

---

## 9. 客户端视角

客户端属性中有这些关键传输参数：

- parallelism
- layout
- tcp buffer
- net stack
- disk stack
- storage module
- striped

parallelism 当前主要支持 fixed 模式。restart marker 支持两类：

- stream offset。
- extended block range list。

restart marker 的 range 插入逻辑会合并相邻或重叠区间，这个设计值得借鉴。

对 GridFlux 客户端：

- manifest 中的 completed chunk 可以合并成 ranges，便于减少恢复协商体积。
- 对 CLI 暴露简单参数：`--connections`、`--chunk-size`、`--resume`、`--verify`。
- 不建议早期暴露 layout、striped、net stack 这类 GridFTP 历史参数。

---

## 10. GridFTP 值得借鉴的设计

### 10.1 控制面和数据面解耦

GridFTP 的控制命令不会直接读写文件，而是变成内部传输操作。GridFlux 应保持这个边界。

### 10.2 Storage Adapter / DSI 思想

不同存储后端通过统一接口接入。GridFlux 可保留思想，但不需要兼容 DSI ABI。

### 10.3 range list

断点续传和 partial transfer 不应只有单 offset。range list 对多流、乱序、失败重传很有价值。

### 10.4 buffer 和 pending 并发

file DSI 用固定 buffer、pending read/write 数控制流水线，这与 GridFlux 的高性能目标一致。

### 10.5 连接级状态

GridFTP 对每条数据连接维护状态、EOF/EOD、复用和 callback。GridFlux 也需要明确的 `ConnectionContext`，避免把连接状态散落在回调里。

---

## 11. GridFlux 应避免的历史包袱

### 11.1 完整 Mode E

Mode E 支持强，但状态机复杂。GridFlux 不需要完整兼容 Mode E，内部可用更直接的 chunk frame 协议。

### 11.2 SPAS/SPOR/第三方传输

这些是 GridFTP 高级能力，Phase 1-3 不应实现。

### 11.3 XIO/DSI 插件复杂度

Globus 通过 XIO 和 DSI 支持大量扩展，灵活但复杂。GridFlux 初期应保持静态、可测、可优化。

### 11.4 控制面兼容泛化

GridFTP 源码里有大量历史命令、站点命令、不同服务器兼容逻辑。GridFlux 只实现项目 Profile。

### 11.5 安全体系混入数据面

GSI、DCAU、PROT、PBSZ 等安全能力复杂且影响性能。GridFlux 应先明确是否需要数据面加密，再单独设计。

---

## 12. 建议的 GridFlux 内部传输协议

GridFlux 不必照搬 GridFTP data channel。建议 Phase 1 自研数据帧：

```text
FrameHeader
    magic
    version
    type              DATA / ACK / FIN / ERROR
    transfer_id
    chunk_id
    offset
    length
    flags
    header_checksum
payload
```

基本流程：

```text
client control
  -> create transfer
  -> negotiate connections/chunk_size/file_size
  -> open N data connections
  -> dispatch chunk tasks
  -> DATA frames carry offset
  -> receiver pwrite by offset
  -> ACK per chunk
  -> manifest marks completed
  -> final checksum
  -> atomic commit
```

这样比复刻 GridFTP Mode E 更直接，也更适合 100G 优化。

---

## 13. Phase 对应建议

### Phase 0

- 只搭工程骨架。
- 可先定义 `Result<T>`、`ErrorCode`、`Buffer`、`ChunkRange` 的头文件。
- 不写网络传输。

### Phase 1

- 实现自研 TCP 多流数据面。
- 用固定 chunk + offset frame。
- CLI 参数支持文件路径、连接数、chunk size、block size。
- 先 memory-to-memory，再 file-to-file。

### Phase 2

- manifest 作为恢复事实源。
- chunk 状态建议：`Pending`、`InFlight`、`Completed`、`Failed`、`Verified`。
- 支持 completed range 合并，用于恢复协商。

### Phase 3

- GridFTP Frontend 只做兼容子集。
- `REST offset` 映射到 manifest/chunk plan。
- `OPTS RETR parallelism=N` 映射到 connections。
- 不支持命令统一 `502`。

---

## 14. 最小实现边界

第一阶段不要做：

- GSI。
- `PROT` 数据面加密。
- `DCAU`。
- `SPAS` / `SPOR`。
- server-to-server 第三方传输。
- 目录递归和小文件聚合。
- 完整 Mode E。
- XIO 插件化。

第一阶段必须做：

- 多 TCP 连接。
- offset-aware frame。
- 固定 chunk。
- buffer pool。
- 基础吞吐指标。
- 可复现 benchmark。
- 与 `iperf3`、`fio` 对照。

---

## 15. 给云端 Codex 的开发提示

后续让 Codex 开发时，可以附带这段约束：

```text
请参考 docs/GridFTP传输源码学习笔记.md。
GridFTP 源码只用于理解传输模型，不复制实现。
GridFlux 内部数据面不复刻 GridFTP Mode E，而采用自研 chunk frame 协议。
控制面只在 Phase 3 提供 GridFTP-compatible 子集。
Phase 1 重点是 TCP 多连接、固定 chunk、offset-aware 写入、buffer pool 和吞吐指标。
```

