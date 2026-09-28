LOWLEVEL-SENDV-IMPL-01 已在独立 worktree 完成源码与窄测试改动：

- Worktree：`C:\Users\12563\AppData\Local\Temp\cpnetflux-lowlevel-sendv-impl-01\worktree`
- 分支：`codex/LOWLEVEL-SENDV-IMPL-01`
- 输入/当前 HEAD：`3b0820dab6dc149f549bd3e81ef403ea7953c4e9`
- 未提交 Git，未触碰共享根目录或共享 index。
- 实现 `FramedDataSocket::writeSegments`，plain TCP 使用 `sendmsg`/双 `iovec`/`MSG_NOSIGNAL`，正确处理短写、跨 header/payload 推进、EINTR、零写和 errno。
- upload/download 仅 DATA frame 在 plain TCP 且 payload 非空时走新路径；TLS、空 payload、SessionInit、ResumeResponse、ChunkComplete、Fin、Complete 等保持原路径。
- 新增真实 socketpair 与可控 sendmsg 测试，覆盖完整字节拼接、真实 64-byte `FrameHeader`、header/payload 单段、header partial、跨 iovec partial、payload partial、EINTR、zero write、peer close/EPIPE 和 `MSG_NOSIGNAL`。
- `git diff --check`、UTF-8、尾换行及私有 index 检查通过。

尚未完成 Linux 编译和运行验收：Windows 无 `cmake`/`ctest`，WSL 返回 `E_ACCESSDENIED`，Docker daemon 不可用。因此单元测试、file/resume/checksum/TLS smoke、strace、before/after 指标均保持 `NOT_RUN`，不能宣称优化已通过。

结果报告位于 worktree 的 `docs/tasks/2026-09-23-lowlevel-sendv-impl-01-result.md`。CLI `--output-last-message` 已执行但因 `%USERPROFILE%\.codex\state_5.sqlite` 只读及 app-server `E_ACCESSDENIED` 退出码 1，摘要文件未生成。