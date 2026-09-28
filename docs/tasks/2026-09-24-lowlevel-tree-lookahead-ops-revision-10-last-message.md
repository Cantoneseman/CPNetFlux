OPS-REVISION-10 已执行。**定向 23 项全部通过**，包括 #180/#181。修正 shell 脚本行尾后的完整 CTest：默认 io_uring 关闭时为 **223 pass、0 fail、1 skip**；在独立构建中启用 io_uring 后为 **224 pass、0 fail、0 skip**，io_uring smoke 也通过。原始精确源码归档的完整 CTest 曾有 3 个由 CRLF 引起的脚本失败；原始日志已保留并在回执中说明。**这不是 04 的 QA 放行**，仍需独立复核证据。

完整 328 文件源码快照及 8 项修改的逐文件 hash 已核验，`CMakeLists.txt` 的 source/test 注册也已确认。精确归档 SHA-256：`28C072007375D113CAC24522B09D9D592277CC4AD08BCF150D6D70FAF02FDD9A`；Linux 行尾规范化归档 SHA-256：`8F367127D1AC17E41EA5A1B03D1F82FCC1E725F900418526069D3A7E05B5A7B2`。

- [执行结果](D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-revision-10-result.md)
- [交接摘要](D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-lookahead-ops-revision-10-last-message.md)
- [证据索引](D:/Project/CPNetFlux/build/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-artifacts/evidence-index.json)
- [本地证据包](D:/Project/CPNetFlux/build/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-artifacts/LOWLEVEL-TREE-LOOKAHEAD-OPS-REVISION-10-evidence.tar.gz)，SHA-256：`2AF5C2DC166615FD4F0924BA335408624E8620B437B07B3528A98E08674A07C6`

深圳构建留在新的隔离 run-root，空间仍高于保留门槛；上海仅做只读复核。既有 GridFTP 服务未改动，也未运行跨域性能实验、清理或提交 Git。回执 UTF-8 与 `git diff --check` 检查通过，暂存区为空。
本轮（2026-09-24 10:30–10:37 UTC）只读复核已存在的远端源码树、完整证据包和实时资源；8 个指定文件 hash、328 文件全树 manifest、CMake 注册、远端证据 tar、本地 evidence bundle SHA 均一致。定向 23/23（含 #180/#181）通过；精确源码 full CTest 保留 3 个 CRLF wrapper 失败与 1 个 io_uring skip；规范化默认配置 223 pass/1 skip；io_uring ON 224/224。两端 SSH 通过严格 host-key 校验，现存 revision-10 runroot 无活动进程引用。本轮没有重复构建或测试，也没有覆盖/改动任何云端文件；详细实时快照已附在结果回执。该复核不构成 04 QA 放行。