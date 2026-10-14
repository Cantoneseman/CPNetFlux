# V3-WORKER-STATE-01C 结果

日期：2026-10-14
分支：codex/v3-worker-state-01c
输入提交：41eb6e74e32cf13590776bba28560bece85db41b

## 已实现

- 统一 control pipeline 与 persistent data worker：depth 1/2/4 映射为共享持久队列窗口，DynamicFileQueue 负责有界入队、slot/lease 独占和完成/取消回收。
- persistent 组合不再降级到 per-file；静态调度组合明确报错，避免固定分片与共享队列混用。
- JSON summary 报告 control_pipeline_actual_mode=shared_persistent_dispatch、dispatch window、容量和高水位。默认目录路径也报告该实际模式。
- 补充 parser、队列 credit、loopback、动态目录和稳定性矩阵断言。

## 默认策略

默认目录路径已经固定为 V3 安全组合：persistent tree、动态文件调度、CRC32C、pending window=2，并使用共享持久 dispatch。--data-session-reuse off 仍保留为显式兼容/回滚入口。TLS、data TLS、压缩和 global scheduler 不默认启用，因为它们分别依赖证书/密钥、对端能力、CPU/数据特征或链路容量策略。

## 验证

- Release CMake build：通过。
- 定向 CTest：22/22 通过。
- 全量 CTest（CPNETFLUX_TEST_TOKEN=ctest-only-token-20261014）：299/299 通过；1 项 io_uring 可选测试按环境 skip。
- mixed、dense 128×1MiB、large 129MiB range 动态队列脚本：通过。
- V3 稳定性矩阵 16 轮（TLS control/data/handshake RST + persistent TLS/checksum/range/resume）：通过。
- git diff --check：通过。

## 限制

此改动统一的是持久 worker 的调度/背压语义；它没有新增逐工作项的控制报文预创建协议。工作项身份仍由现有 transfer/file/range/generation/attempt/checksum/commit 状态验证。
