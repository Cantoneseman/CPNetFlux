# V3-WORK-UNIFY-02 首个可审查子阶段结果

- 输入：a70bae16abd2f8e22984c2581e0d86e2f21be549；工作分支：codex/V3-WORK-UNIFY-02。
- 目标：接通 persistent + global scheduler 的共同 ready/lease 生命周期，并覆盖 upload/download 的 observable worker、lease、epoch、retry 状态。

## 已实现

- DynamicFileQueue 支持 scheduler attach、ordered scheduler plan 到完整 V3 identity 的绑定、connection epoch、adaptive worker admission 和 receiver-side claimIncoming。scheduler 只规划、反馈和记录事件，不拥有活动 lease。
- persistent upload sender 与 download receiver 共用 WorkLease 状态。receiver 以 FILE_BEGIN 的完整 identity 建立 incoming lease，在 result 写回后完成 commit；旧 lease、旧 worker/epoch 仍由 queue identity 校验拒绝。
- scheduler event 现在带 executor、lease、worker、connection epoch、generation、attempt；executor failure、retry、complete 分别记录，retry 计数回写 LinkManager。
- persistent + global 允许与 shared control dispatch window 组合，fixed/adaptive 策略仍保留，summary 记录实际 scheduler policy/mode。

## 验收证据

- Release/TLS/io_uring ON 构建：cmake --build /tmp/cpnetflux-runs/V3-WORK-UNIFY-02-20261017/build -j 2，通过。
- 聚焦回归：36/36 通过（queue、incoming lease、persistent session、tree options）。随后最新代码变更后的 queue/options 聚焦回归 33/33 通过。
- 全量 CTest：CPNETFLUX_TEST_TOKEN 仅注入进程环境后，335/335 通过；未注入 token 的首次运行只有既有 token-auth 与 event-log 两项因环境变量缺失失败，补注入后均通过。
- scheduler smoke：run_gridftp_tree_scheduler_smoke.py 通过；新增 CTest 门禁 cpnetflux_v3_unified_executor_smoke 通过。
- dynamic queue smoke：mixed 57 files / 24,315,364 bytes 通过；large 129 MiB range case 通过。
- 自定义 loopback：persistent + global/adaptive upload/download 的三方 tree hash 一致；upload/download 均 actual_mode=persistent_tree、actual_file_scheduling=dynamic、result=pass，scheduler event 文件分别产生 6、4 条；global/fixed upload/download 也通过。

证据根目录：/tmp/cpnetflux-runs/V3-WORK-UNIFY-02-20261017/。

## 边界

本结果不宣称独立 work_executor 已抽取，也不宣称自动 transport reconnect/retry、manifest 对账重连、跨机性能、十链路、LinkSet/RDMA 或生产 readiness 已完成。现有 disconnect/reopen/requeue 是可调用原语；自动故障恢复和缺失工作重派仍是下一步。真实 GridFTP 对比前按用户要求刷新 proxy；本轮未刷新。
