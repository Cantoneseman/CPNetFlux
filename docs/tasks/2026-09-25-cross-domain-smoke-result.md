# 2026-09-25 深圳—上海最小跨域 smoke 实验结果

- 状态：真实传输已执行；单文件通过，目录上传通过，目录下载发现内部 manifest 语义问题；未启动完整矩阵。
- 固定输入：CPNetFlux commit `70fe441580f274712e11c110fc9251a6d422306d`，深圳 Release 构建，`CPNETFLUX_BUILD_TESTS=ON`、`CPNETFLUX_ENABLE_IO_URING=OFF`。
- 拓扑：Windows 仅发起 SSH；运行器/客户端在深圳 `gridflux-beta-shenzhen`；CPNetFlux server 在上海 `gridflux-beta-shanghai`；上海控制端口 `22110`，数据端口窗口从 `24100`；旧 GridFTP 端口和历史目录未触碰。
- 配置：POSIX、worker control reuse、scheduler off、compression off、checksum none、data TLS off；本轮为链路/目录语义 smoke，不是最终安全或性能验收。

## 结果

1. 单文件上传：64 MiB，1 文件，深圳→上海。client/server 均返回 `pass`；端到端 `5.25661 s`，约 `102.1 Mbps`；源和上海目标 SHA-256 均为 `c9b647616d1585b5aa00e976b859119a8d2d8e741c027bb6042e3ffdfbae27b5`。
2. 多文件目录上传：128 文件、总 64 MiB，4 路 file parallelism，深圳→上海。client 返回 `pass`；端到端 `13.0228 s`，约 `41.2 Mbps`。排除内部 `.cpnetflux.*`/`.part.*` 后源和目标 canonical tree hash 均为 `efd1beda67df748af3d08196acd56d355143c847bb67c4cf153d5b93be36ab72`，业务文件均为 128 个、64 MiB。
3. 多文件目录下载：上海→深圳。业务文件排除内部 metadata 后仍为 128 个、64 MiB，canonical tree hash 与源一致；但客户端总计报告 256 个文件、67,198,976 字节，说明目标目录中的 checkpoint manifest 被目录扫描/下载流程当作普通文件处理。该 case 不能记为干净的目录功能通过。

## 证据与限制

- 远端运行根：深圳/上海均为 `/tmp/cpnetflux-runs/CLOUD-GITHUB-01`；server 使用隔离 `server-root`，实验结束后 server 已停止。
- 上传过程中未打开完整校验，符合当前 smoke 目标；独立 SHA-256/tree hash 在传输后执行。
- 这次没有比较 GridFTP，也没有进行 100 Mbps 多次重复或长时间矩阵；结果只证明新云端构建可跨域通信，并定位出目录 metadata 边界问题。

## 下一步

先修正或明确目录扫描对 `.cpnetflux.*` checkpoint 文件的过滤/manifest 生命周期，再以相同 128-file case 重跑双向；目录语义通过后才做 worker/file-parallelism 的性能 A/B。单文件 102 Mbps 结果可作为当前 POSIX 基线，不据此宣称接近 GridFTP。
