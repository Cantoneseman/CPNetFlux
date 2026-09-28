# LOWLEVEL-TREE-LOOKAHEAD-OPS-08

## 目标

在 04 revision-07 静态 PASS 后，由 05 运维在深圳、上海做只读资源预检，并在隔离 Linux 环境构建和测试实现 worktree `D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true` 的精确未提交内容。

## 输入与边界

- 固定源码基线：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`。
- 实现 worktree：`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`，branch `codex/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05`。
- 不得修改本地 main、共享 index、远端历史 `/root/projects/GridFlux-Beta`，不得进入或触碰 `/root/projects/CPSS(DCC)`，不得启动性能矩阵、跨域传输、清理或停止既有服务。
- 源码工件必须保留完整工作树差异和 SHA-256；若无法形成可追溯工件则阻塞，不把 dirty 云端树当作输入。
- SSH 使用 `D:\Software\Git\usr\bin\ssh.exe`、用户现有配置、`BatchMode=yes`、`StrictHostKeyChecking=yes`；别名仅为 `gridflux-beta-shenzhen`、`gridflux-beta-shanghai`。

## 验收

1. 两端只读核实 hostname、挂载点 `/` `/tmp` 空间、CPU/内存、已有 PID/监听端口、compiler/CMake/Python/GTest/spdlog/zlib/OpenSSL/liburing；每个相关挂载点须有至少 10 GiB 加本任务构建峰值余量。上海不足则整体 blocked。
2. 在不改历史目录的隔离运行根中部署可审计的源码归档或等价差异快照，记录来源 worktree、固定基线、工作树 status、归档 SHA-256、构建参数和二进制 SHA-256。不要把凭据写入日志。
3. 只配置 `CPNETFLUX_BUILD_TESTS=ON`，按实际可用依赖构建；运行新增 `TreeLookaheadTest`、`TreeTransferOptionsTest`、受影响 tree 单测和完整 CTest。逐项记录 pass/fail/skip/block，skip 不算 pass。
4. 验证默认 lookahead depth=0、`reliableCandidateMemory=false` 的保守回退；不得启用未获批的生产 lookahead 或性能实验。
5. 交付构建命令、退出码、完整日志路径、测试清单、磁盘前后、残留 PID、工件/二进制 SHA-256。缺任何一项则说明阻塞。

## 输出

仅写 `docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-08-result.md` 和同名 `-last-message.md`；不改代码，不提交 Git。
