已完成 `LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-06`，修改位于隔离 worktree：

`D:\Project\CPNetFlux\build\LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true`

已加入协作停止、连接超时、异常终态通知、一次性 index 归还、真实资源计数与 fail-closed 深度门，并补充状态竞态与资源回滚测试。生产 scheduler 因候选线程/TLS 内存无法证明满足上限，保持 effective depth `0`。

结果文件：

- [详细回执](D:/Project/CPNetFlux/build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true/docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-result.md)
- [摘要](D:/Project/CPNetFlux/build/LOWLEVEL-TREE-LOOKAHEAD-IMPL-05-true/docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-last-message.md)

固定 HEAD 和空 index 已确认，`git diff --check` 及静态白名单检查通过。Windows 无 CMake/CTest/C++ 工具链，WSL 为 `E_ACCESSDENIED`，因此动态构建、测试、Linux FD 断言和 loopback 均为 `NOT_RUN`，需由 04 独立 Linux 复核。