# ENV-PREFLIGHT-02：深圳/上海跨域实验只读准入复核

- 状态：ready
- 路线版本、任务版本：`R2026-09-23.3 / v1`
- 发起人：00；执行：05 云端运维与发布；复核：00/04
- 目标：更新两台现有 100 Mbps 跨域服务器状态，判断之后能否安全承载隔离 Linux build 和小型双向 pilot。
- 非目标：不构建、不运行 CPNetFlux/GridFTP transfer、不清理、不修改服务器/服务/进程/目录/配置；不复制凭据、不查看受保护项目；SSH 可用不代表资源通过。
- 输入：执行前记本地实时 HEAD/status；历史见 `docs/coordination/receipts/05-operations.md`、`ENVIRONMENT.md`。目标仅是 `gridflux-beta-shenzhen` 和 `gridflux-beta-shanghai`，不是广州—深圳 100G 现场。
- SSH：使用 `D:\Software\Git\usr\bin\ssh.exe -F "$env:USERPROFILE/.ssh/config"`、BatchMode、StrictHostKeyChecking=yes、ConnectTimeout；不打印配置/密钥/token。系统 SSH 曾返回 255。
- 只读允许：hostname/time/df/inode/CPU/memory/load/依赖版本/listener/有限进程名和 PID/属主、获准实验 run roots 归属。完全跳过 `/root/projects/CPSS(DCC)`；不递归未知项目，不读 cmdline/environ/凭据，不扫清理整个 `/root/projects`。
- 边界：`/root/projects/GridFlux-Beta` 是 dirty 历史目录，只读；保留现有 GridFTP 服务及端口；运行目录拟用 `/tmp/cpnetflux-runs/<task-id>`，创建前须检验所有权、挂载点和空间。
- 验收：按主机给出当前可用量及 build/archive/peak payload/evidence 预算；每个相关 mount 保留 10 GiB；核对服务 owner、活动实验和端口、认证/数据 TLS条件、编译器/CMake/依赖及 io_uring 真实状态。归属/空间/权限任一不明则 blocked。明确未写入、未启动 workload。
- 产物：新增 `docs/tasks/2026-09-23-env-preflight-02-result.md` 保存详细回执；不得覆盖或改写既有 05 总回执，也不改 BOARD/ROSTER。

## 派给角色的消息

续接既有 05 聊天只读核查两端。不得越过准入步骤开始 build/transfer；记录实际命令、退出码和资源，不泄露认证信息。最终完整报告写入本任务 result 文件。
