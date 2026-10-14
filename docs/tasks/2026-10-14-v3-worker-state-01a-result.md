# V3-WORKER-STATE-01A 结果：目录模式降级可观测

日期：2026-10-14
输入提交：b13b574886ed9e1930eaca36f9ce703a696c4cb0
实现提交：1817d8385211df55c1457bedb181fe5f85a6250c
权威源码：/root/projects/CPNetFlux

## RED

更新 pipeline loopback 断言后，旧构建失败并报告 requested_mode/actual_mode/fallback_reason = per_file/per_file/空。这证明解析阶段把默认 persistent-tree 请求改成 per-file 后，JSON summary 丢失了原始请求和降级原因。

## GREEN

- cmake --build /tmp/cpnetflux-delivery-build -j2：通过。
- cpnetflux_unit_tests --gtest_filter=TreeTransferOptionsTest.AllowsConcurrentWorkersForDepthOnePipeline:TreeTransferOptionsTest.ExplicitPersistentTreePipelineCombinationIsRejected：2/2 通过。
- ctest --test-dir /tmp/cpnetflux-delivery-build -R '^cpnetflux_tree_pipeline_sidecar_smoke$' --output-on-failure：1/1 通过；上传和下载 pipeline 均检查 requested=persistent_tree、actual=per_file、fallback_reason 非空，同时保留文件 hash 与计数验证。
- CPNETFLUX_TEST_TOKEN=V3CompositionTemporaryToken20261014 ctest --test-dir /tmp/cpnetflux-delivery-build --output-on-failure：299/299 通过；FileIoTest.IoUringContextReadWriteSmokeWhenAvailable 因环境没有 io_uring 能力而 skip。
- 实现提交已推送；git ls-remote origin refs/heads/main 回读 SHA 为 1817d8385211df55c1457bedb181fe5f85a6250c。

解析结果保留原始 data-session-reuse 请求。默认 pipeline 兼容路径执行 per-file 时明确给出 fallback；显式同时请求 persistent tree 和 pipeline 仍在网络连接前拒绝。没有改变协议、range、校验、恢复或连接行为。

## 未完成

global scheduler、control pipeline 与 persistent data worker 尚未共用 dispatch lease/cancel/recycle 契约，见父任务 V3-WORKER-STATE-01。未生成新 DEB；V3 整体未通过公开稳定版验收。

## 回滚

代码回滚使用 git revert 1817d8385211df55c1457bedb181fe5f85a6250c；保留验收记录及 V3-COMPOSITION-01/02。
