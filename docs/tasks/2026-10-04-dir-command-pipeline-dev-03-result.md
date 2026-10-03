# DIR-COMMAND-PIPELINE-DEV-03 实施阶段结果

- 分支：codex/DIR-COMMAND-PIPELINE-DEV-03
- 隔离 worktree：/tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-03/src
- 固定输入：753a67d415614cef0b10cde95275f3431fe11de0
- 结果状态：实现已写，待动态验证与 04 独立审查；不代表迁移完成、性能收益或传输正确性验收。

## 实施内容

1. pipeline 控制槽启用 30 秒响应 deadline，depth=0 和普通控制连接不设置该 deadline。多行控制回复共享单一 deadline。
2. deadline 读先 poll，再对单次读临时设置 O_NONBLOCK 并恢复 socket 原 flags；TLS 已解密数据通过 SSL_pending 直接读取。慢速分段回复不能靠每次阻塞读续期。取消、EOF、超时各返回明确失败状态，不再因读到零字节无限循环。
3. 抽取实际使用的控制回复码解析和 OPTS PIPELINE=1 的 200 应答检查；非 200 不会让 pipeline control slot 进入 ready。
4. 新增 socketpair 单测源：partial reply deadline、连续回复缓冲、peer EOF、shutdown 取消唤醒和命令拒绝。generation/file/path/direction identity 的既有单测仍在 tree_pipeline_identity_test.cpp。
5. 当前文件失败时的 candidate cancel、join 和 slot reset 保持既有实现；depth0/depth1 双向目录/sidecar 文件集合、hash、manifest、summary loopback 覆盖在先前 sidecar QA 记录中（固定提交 5410c2b596c7631de203f6949a63756f57cb8158，基于其对应的上一阶段提交）。本阶段未重新运行这些测试。

## 改动文件

- CMakeLists.txt
- include/cpnetflux/core/io/tls_socket.h
- include/cpnetflux/core/io/tree_pipeline_control_io.h
- src/core/io/tls_socket.cpp
- src/core/io/tls_socket_stub.cpp
- src/core/io/tree_pipeline_control_io.cpp
- src/core/io/tree_transfer_client.cpp
- tests/unit/tree_pipeline_control_io_test.cpp
- 本任务单与本结果文档

## 实际检查与限制

已完成 UTF-8、最终换行、行尾空白和必需符号静态门禁；既有 CRLF 源文件用 git -c core.whitespace=cr-at-eol diff --check 检查。未运行 CMake configure/build、CTest、socketpair 单测、loopback、故障注入或性能实验，原因是深圳已有实验活动，用户要求不让测试干扰该批次。此前出现过的全文件换行改写已纠正；TLS 头/源文件 diff 已恢复为局部修改。

单测源是静态新增，不等于其通过。断开/取消的服务端 listener 最终释放和 candidate join 的真实调度路径也尚未通过运行时故障注入证明。因此整体继续标记 blocked-validation。

## 用户手动验证命令

确认深圳现有实验结束后，在深圳执行下列命令；它们只用隔离源码及独立 build 目录，不从 /root/projects/CPNetFlux live root 取代码。CTest 会启动本机 loopback smoke，请勿与用户实验并行：

~~~~sh
cmake -S /tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-03/src \
  -B /tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-03/build \
  -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON \
  -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_ENABLE_IO_URING=OFF
cmake --build /tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-03/build --parallel 2
ctest --test-dir /tmp/cpnetflux-runs/DIR-COMMAND-PIPELINE-DEV-03/build \
  --output-on-failure -R 'TreePipeline|cpnetflux_tree_pipeline'
~~~~

回传原始命令输出和最终退出码后，再由 04 复核；在此之前，不把候选拒绝/取消的运行时清理或完整 pipeline gate 标成通过。
