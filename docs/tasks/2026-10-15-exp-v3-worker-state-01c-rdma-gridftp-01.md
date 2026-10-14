# EXP-V3-WORKER-STATE-01C-RDMA-GRIDFTP：R730/R750 RoCE 链路对比

日期：2026-10-15
状态：完成
输入提交：922401b49450b47366a026a8285bc72fb52067dd
权威源码：/root/projects/CPNetFlux
实验 worktree：/tmp/cpnetflux-runs/EXP-V3-WORKER-STATE-01C-RDMA-GRIDFTP/src

## 范围

- 在 R730（192.168.100.1）和 R750（192.168.100.2）的专用 /30 100GbE RoCE 网口上部署 01C 的隔离 CPNetFlux 实例。
- 保持现有 GridFTP GSI 服务不变，复用已核验的 R750:22522 服务与 GSI 证书链。
- 先完成网卡、路由、TLS/token、manifest/checksum、双向 loopback 预检，再运行与既有工况一致的三类 workload：large single 256MiB、dense 128x1MiB、mixed 256MiB；双方向、三次重复、总逻辑流量控制在 10GiB 内。
- CPNetFlux 使用 V3-WORKER-STATE-01C 的动态队列、持久 tree、shared dispatch window、CRC32C、范围/恢复语义和阶段计时；记录 requested/actual/fallback 及链路层。
- GridFTP 使用现有 GSI 认证和同一数据集，以独立服务端口/工作目录运行，不修改系统服务。

## 非目标

- 不把 ib_write_bw/ib_read_bw 的原生 verbs 结果当作 CPNetFlux 结果；01C 当前源码没有 libibverbs/rdma_cm 数据通道，本次 CPNetFlux 走 RoCE 网卡承载的 TCP framed data path。
- 不修改深圳权威根目录 dirty 文件、既有 GridFTP 服务、桌面 agent、其他项目或历史 payload。
- 不在本阶段新增 native verbs 传输、global scheduler 与 persistent tree 的未验收协议组合；不做跨域、长时间、RDMA fabric 结论。
- 不打印或提交 token、私钥、GSI proxy、证书内容和大 payload。

## 验收标准

1. 01C Release 构建、定向测试、全量 CTest、git diff --check 通过。
2. 两端安装目录、版本 SHA、运行配置和端口可回读；RoCE rdma link/route/MTU/100GbE 状态可回读。
3. CPNetFlux TLS/token/auth probe、双向 manifest/hash/finalize 预检通过，summary 明确记录 shared_persistent_dispatch、dispatch window、checksum 和 phase timing。
4. GridFTP GSI proxy 在实验窗口有效；双向三类 workload 每配置 3 次，任何失败停止扩大矩阵并保留日志。
5. 产出逐次 raw summary、聚合 CSV/JSON、环境快照、命令行和结论；CPNetFlux 与 GridFTP 只比较同工况有效样本。
6. 代码/任务文档提交到 codex/EXP-V3-WORKER-STATE-01C-RDMA-GRIDFTP，push 后回读远端 SHA。

## 允许路径

- 本 worktree 内 docs/tasks/2026-10-15-exp-v3-worker-state-01c-rdma-gridftp-01.md 及对应结果文档。
- 本 worktree 内新增的实验脚本/聚合器（仅在现有脚本无法表达 01C 组合时）；不修改核心协议代码。
- 远端实验临时目录：/tmp/cpnetflux-v3-worker-state-01c-*、/tmp/gridftp-v3-worker-state-01c-*，完成后按证据需要清理 payload，保留小型日志/summary。

## 回滚

停止本任务启动的进程，删除两端任务临时目录和 worktree 外的测试 payload；保留现有 GridFTP 服务和用户文件。代码回滚使用 git revert，不得 reset/clean 深圳权威工作区。