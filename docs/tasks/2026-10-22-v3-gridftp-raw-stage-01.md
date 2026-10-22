# V3-GRIDFTP-RAW-STAGE-01：无压缩 V3 与 Globus GridFTP 阶段耗时对照

状态：执行中  
权威工作区：深圳 `/root/projects/CPNetFlux`，分支 `codex/V3-DESKTOP-NETWORK-BINDING-01`  
源冻结点：运行时代码 `55f7a21bb970cc0528d21a52901627aa96660b44`；当前 HEAD `3dd7bffeb012bfb65d6b7ad6aa8acf2834ddb171` 仅增加文档，已核验 `src/`、`include/` 和 `CMakeLists.txt` 无差异。

## 范围

在 R730→R750 直连数据网口上，对最新可验收 CPNetFlux V3 和 Globus GridFTP
进行无压缩性能对照。V3 固定为 `persistent_tree`、动态文件调度、范围传输、
8 个并发通道、worker control reuse、tree data-session reuse、CRC32C，明确
`--compression off`；GridFTP 使用 GSI 控制认证、私密数据通道及 8 路文件/数据
并行。两端使用同一确定性约 10.13 GiB 输入集，单向运行，按数据集各重复三次。

数据包括 128 MiB/2048 文件 dense 目录、4 GiB 随机大文件、4 GiB 重复内容大文件、
约 2 GiB/514 文件 mixed 目录。记录每次扫描、控制准备、数据连接、队列等待、
首个 payload、文件读写、payload I/O、file-result、manifest/checksum/finalize、
transfer-complete 等能被该实现直接观察的阶段；GridFTP 侧保存客户端 debug 和
服务端 per-file transfer 日志，并对无法拆出的控制/传输阶段明确标为合并区间。
双方另采集 CPU、RSS、磁盘、NIC、丢包/错误、连接和 GSI 身份/时限。

## 非目标

- 不修改 CPNetFlux runtime、agent、桌面端、正式安装或长期运行服务。
- 不进行 native RDMA verbs、跨域、公网或阿里云结论；数据接口走普通 TCP。
- 不改系统认证、防火墙、路由、网卡或 GridFTP 永久配置。
- 不把认证、空间、构建或校验失败计为吞吐样本。
- 不在结果中把重叠的并发阶段累加值误作总 wall time，也不把 GridFTP 服务端
  per-file 区间与 V3 单调时钟字段假设成同一计时边界。

## 验收

1. 固定源代码/二进制 SHA；R730/R750 运行时二进制一致。
2. GridFTP GSI proxy 有效期覆盖实验，权限 600；GridFTP GSI/private smoke 通过。
3. V3 JSON summary 显示 V3 transport、persistent tree、动态调度和压缩关闭；
   range/file identity、校验及提交结果通过。
4. 每个数据集的双方正式样本均完成三次，文件数、字节数和 SHA-256 tree hash 一致。
5. 结果保留原始 JSON、客户端/服务端日志、环境采样和阶段汇总；分别报告有效
   wall time、逻辑吞吐、网络计数器和阶段统计口径。
6. 实验进程、临时监听、token、TLS key、GSI proxy 和 payload 收尾；仅保留本任务
   `/tmp/cpnetflux-runs/V3-GRIDFTP-RAW-STAGE-01/<run-id>` 中的非凭据证据。
7. 任务与结果文档在本分支提交、push，并回读 GitHub SHA。

## 允许路径与回滚

Git 改动限于本任务及结果文档；必要时可增加可复用、经脚本门禁验证的实验工具。
运行文件、payload、日志、临时证书和 token 仅放两端本任务专属 `/tmp` 目录。
回滚只停止本批确认归属的 PID/端口并删除本批目录及凭据；不覆盖或清理其他
dirty 文件、正式 agent/desktop 或未知实验目录。
