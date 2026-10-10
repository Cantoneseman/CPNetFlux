# V3-RANGE-01 结果

日期：2026-10-11
状态：代码与测试完成，代码提交已生成，待 push 与远端回读

## 输入与工作区

- 权威源码：/root/projects/CPNetFlux
- 输入提交：79fbe43ee456ea95e3ac23ccbece2124d0eb4873
- 分支：codex/V3-RANGE-01
- worktree：/tmp/cpnetflux-runs/V2-COMPLETE-01/src
- 构建：/tmp/cpnetflux-runs/V2-COMPLETE-01/build

## 已完成

- 自适应 range planner：按文件大小、chunk size、通道数、RTT/BDP、磁盘窗口和调度开销决定是否切 range；小文件保持文件级并行。
- V3 目录：动态元数据环状队列、range identity/generation/attempt 校验、按 offset 写临时文件、缺失 range resume、skip、checksum、最终 manifest/atomic publish。
- 单文件兼容整合：共用 planner；DATA/ChunkComplete 使用 header range_id/attempt；ChunkComplete 新载荷携带 canonical range_id/attempt，旧 32 字节载荷仍可解码；服务端按范围身份校验、记录 manifest 并拒绝混用。
- 阶段计时继续记录 queue wait、first payload、payload I/O、file result/complete wait、manifest/checksum/finalize/wall 等字段。

## 验证

- cmake --build /tmp/cpnetflux-runs/V2-COMPLETE-01/build -j2：通过。
- ctest --test-dir /tmp/cpnetflux-runs/V2-COMPLETE-01/build --output-on-failure -I 1,238：238/238 通过；io_uring 可选测试按环境跳过。
- 目标 range、单文件、目录回归：33/33 通过。
- 全量 CTest（排除已知环境/外部阻塞的 token 与 data TLS smoke）：269 项中 267 通过，event-log 因 CPNETFLUX_TEST_TOKEN 未设置失败，alpha soak 因既有 tiny resume 的 protocol_error 失败；完整日志 /tmp/cpnetflux-runs/V3-RANGE-01/verify-ctest-external-excluded.log。
- 原始全量 CTest 曾在 cpnetflux_gridftp_data_tls_smoke 阶段超时停止；日志 /tmp/cpnetflux-runs/V3-RANGE-01/verify-ctest-full.log。
- 目标日志：verify-ctest-unit-final.log、verify-v3-range-final-targeted.log。

## 边界与后继

旧 V1 SessionInit/DATA/ChunkComplete 状态机仍作为显式兼容 fallback；本提交统一其 range planner、range identity、attempt、checksum/resume 和提交校验语义，不删除旧状态机。删除 V1 或把单文件完全切换到持久树 FILE_BEGIN/DATA/FILE_END 需另立任务，先完成使用者审计和兼容门。

## 提交记录

- 代码 commit：5dc6036c3be5c0f51a194d7571078cfa3e8537f5
- 文档记录 commit：待生成
- push：待执行
- 远端 SHA：待回读
