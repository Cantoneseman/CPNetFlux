已完成 `LOWLEVEL-TREE-LOOKAHEAD-IMPL-REVISION-06-QA`，结论为 **BLOCKED**。

- 结果：[revision-06-qa-result.md](/D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-qa-result.md)
- 摘要：[revision-06-qa-last-message.md](/D:/Project/CPNetFlux/docs/tasks/2026-09-24-lowlevel-tree-lookahead-impl-revision-06-qa-last-message.md)

关键 blocker：

- `releaseCandidateControl` 仅接受 `ControlPending`。
- handoff 后状态为 `InUse`，取消/失败后为 `Cancelled/Failed`，导致 control counter、candidate storage 无法释放。
- 新增单测中的 handoff 与 failure/cancel 清理断言将失败。
- Windows 无 CMake/CTest/C++ 工具链，WSL 返回 `E_ACCESSDENIED`；动态构建、单测、CTest 和传输 smoke 均 `NOT_RUN/BLOCKED`。

未授权进入 05 固定 Linux 验收或云端验证；需先修复资源释放状态协议。