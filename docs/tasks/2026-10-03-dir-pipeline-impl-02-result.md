# DIR-PIPELINE-IMPL-02 执行结果

- 任务：DIR-PIPELINE-IMPL-02
- 输入基线：4f16a79d6e6ddda681683496e5c3aae79c16b231
- worktree：/tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-02
- 分支：codex/DIR-PIPELINE-IMPL-02
- live root /root/projects/CPNetFlux 未修改；未访问上海、未启动网络实验。

## 改动

- include/cpnetflux/core/io/file_transfer_server.h、include/cpnetflux/core/io/file_download_sender.h：listener API 增加可选 controlFd=-1，旧调用保持兼容。
- src/protocol/control/control_server.cpp：STOR/RETR 把当前控制 fd 传给 data listener。
- src/core/io/file_transfer_server.cpp：plain epoll 和 TLS poll 在等待 data stream 时监视控制 fd，识别 HUP/RDHUP/ERR/EOF；plain 等待增加 60 秒边界，数据流已全部接收后停止监听 control token。
- src/core/io/file_download_sender.cpp：下载 sender listener 同样监视控制 fd，并保持 60 秒等待边界。
- src/core/io/tree_transfer_client.cpp：PreparedTransfer 增加线程安全取消标志；ControlClient 提供 cancel/shutdown，命令与 read loop 检查取消；candidate 失败、worker 当前文件失败/异常和 join 前路径请求取消，候选线程边界仍捕获异常，depth=0 和 wire/manifest/resume/checksum 语义未改。
- tests/unit/tree_pipeline_state_test.cpp：增加取消请求后一次清理状态测试。
- 未修改 CMake（现有测试已注册）。

## 验证命令

- git rev-parse HEAD：退出 0，4f16a79d6e6ddda681683496e5c3aae79c16b231。
- cmake -S /tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-02 -B /tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-02/build -G Ninja -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF -DCPNETFLUX_ENABLE_TLS=ON：退出 0。
- cmake --build /tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-02/build --parallel 2：退出 0。
- ctest --test-dir /tmp/cpnetflux-runs/DIR-PIPELINE-IMPL-02/build --output-on-failure -R TreePipelineStateTest|TreeTransferOptionsTest|TreeManifestTest|TreeScanTest：退出 0，21/21 通过。
- git diff --check：退出 0。
- git diff --ignore-space-at-eol --stat：退出 0；源文件为功能级差异，无整文件 CRLF/LF 重写。
- 白名单路径检查：退出 0；源码/测试改动仅在本任务允许范围。listener 旧调用仍使用默认 -1，control server 的 STOR/RETR 使用真实控制 fd。
- 静态 grep -R control_id/controlId src include：无匹配。

## 资源/取消语义

候选控制连接在正常 handoff 后由当前文件处理继续拥有；当前 worker/file 失败、manifest 更新失败、candidate 失败或 worker 异常时，先设置取消并 shutdown 已发布的控制 fd，再 join preparation，随后才析构对象。listener 端在等待 data FD 时能观察控制连接关闭并返回 runtime error，释放 passive listener/临时状态。候选失败不推进为 Completed。

## 未运行与限制

- 未运行完整 CTest、tree upload/download smoke、depth=1 真实传输、hash/manifest on/off 对照、真实 passive-listener 断连注入、性能 A/B、跨域实验、SSH 上海或云端操作。
- ControlClient 仍沿用既有阻塞 DNS/connect/TLS 建连；取消在控制对象发布后可通过 shutdown 唤醒读写，发布前无法从当前对象取得 raw fd 来打断阻塞 connect/TLS。该平台限制意味着“全阶段有界取消”尚未被证明，交由 04 独立复核；不得据此宣称 QA 通过或生产 readiness。
- 现有状态测试验证模型级取消请求/清理，不覆盖真实 socket listener。完整网络链路和断连回收仍是 NOT_RUN。

## 交接

请 04 在独立 Linux 环境复核 listener control-fd 传递、控制连接 HUP/EOF 处理、candidate 取消与 join 顺序，并决定是否接受上述 connect/TLS 发布前限制。00 仅在 04 复核后收口。
