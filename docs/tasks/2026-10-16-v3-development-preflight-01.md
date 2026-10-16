# V3-DEV-PREFLIGHT-01：开发前检查与基线准备

- 日期：2026-10-16（UTC+08:00）
- 输入提交：0f7009bb8367f2484515c2d0562ec73a21112c1e
- 执行者：00 总指挥，深圳根目录串行；不派单其他聊天。
- 权威仓库：/root/projects/CPNetFlux；当前分支 main。
- 路线：R2026-10-16-V3-UNIFIED-MULTILINK；下一实现任务 V3-WORK-UNIFY-01。

## 范围

核实 Windows 到深圳/上海的非交互 SSH、主机身份、磁盘和内存；核实深圳 HEAD/dirty/index/worktrees、GitHub remote 与实时远端 SHA；检查相关现有服务、端口和公开证书/GSI proxy 元数据；检查 Linux 构建依赖，在本任务专用目录执行当前提交的核心 CMake/Ninja 构建和全量已注册 CTest，保存结果与交接。

## 非目标

不修改产品源码、协议、桌面实现或默认值；不做跨主机数据传输或性能实验；不安装软件、不刷新凭据、不改服务配置、不重启或停止既有服务、不清理历史目录或 worktree。不触碰 GridFlux-Beta、CPSS(DCC)、其他项目及其备份。不以取消旧迁移门替代历史迁移验收。

## 允许路径

- docs/tasks/2026-10-16-v3-development-preflight-01.md（本任务）
- docs/tasks/2026-10-16-v3-development-preflight-01-result.md（结果）
- docs/tasks/2026-10-16-v3-work-unify-01.md（下一阶段实施任务准备）
- /tmp/cpnetflux-runs/V3-DEV-PREFLIGHT-01-20261016/（新建的构建、测试临时文件、日志和证据；不进 Git）
- Windows 仅保存上述文件和小型证据的校验副本，不作本地开发/Git 操作。

## 保护已有状态

保留根目录现有 BOARD.md 修改和两个 2026-10-14-v3-worker-state-01b 未跟踪任务文件，记录其 SHA-256 并在结束时复核；不提交这些已有文件。保留登记的四个其他 worktree。源文件必须与输入 HEAD 一致；不 reset/clean、不切换分支，不使用 git add .。

## 验收

1. 两端 SSH 使用 BatchMode=yes、StrictHostKeyChecking=yes，记录返回码和主机身份。
2. 深圳 main/HEAD 与 GitHub origin/main 实时 SHA 一致，index 无无关内容；记录现有 dirty、worktrees、锁和相关活动进程。
3. 在 /tmp/cpnetflux-runs/V3-DEV-PREFLIGHT-01-20261016/build 运行：
   - cmake -S /root/projects/CPNetFlux -B /tmp/cpnetflux-runs/V3-DEV-PREFLIGHT-01-20261016/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=ON -DCPNETFLUX_BUILD_DESKTOP=OFF
   - cmake --build /tmp/cpnetflux-runs/V3-DEV-PREFLIGHT-01-20261016/build --parallel 2
   - ctest --test-dir /tmp/cpnetflux-runs/V3-DEV-PREFLIGHT-01-20261016/build --output-on-failure --parallel 1 --timeout 180
4. 测试仅使用临时 loopback fixtures；临时 token 仅在内存中注入，日志脱敏。可选 io_uring 不可用须单列 skipped，不能记为实际后端通过。桌面构建不在本次核心基线范围。
5. 两端每盘保留至少 10 GiB，本任务磁盘预算上限 6 GiB；TMPDIR 指向本任务目录，保留日志，不做跨任务清理。
6. git diff --check、源码前后 manifest、三个既有 dirty 文件 SHA-256、既有服务 PID 和监听端口复核。
7. 仅提交本任务明确文档，推送已核实的 git@github.com:Cantoneseman/CPNetFlux.git，记录 commit/push 返回码和远端 SHA 回读。

## 回滚和交接

本阶段无产品代码改动。失败保留日志并注明原因，不在预检中修复产品行为。文档需要撤销时仅撤销本阶段明确提交，不回滚既有 dirty。V3-WORK-UNIFY-01 的实现须由后续任务执行；统一 executor 验收前保持 10-link 性能实验关闭。
