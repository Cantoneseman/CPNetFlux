# V3-WORKER-STATE-01C 决策补充

日期：2026-10-14
状态：已验证
上游决策：docs/DECISIONS/2026-10-14-v3-stable-composition.md
输入提交：41eb6e74e32cf13590776bba28560bece85db41b

## 决定

control pipeline 与 persistent data worker 使用同一 DynamicFileQueue dispatch/lease 语义。control-pipeline-depth=1|2|4 只代表持久 worker 的受界预取窗口，不建立第二套按文件准备控制报文的状态机；静态文件调度组合明确拒绝。工作项的身份、generation、attempt、checksum、commit、取消和断线回收沿用 V3 事务状态。

默认目录传输使用 persistent tree、动态文件队列、CRC32C 和 data pending window=2。未显式给出 pipeline depth 时，depth=0 表示使用该默认 pending window；summary 仍报告 shared_persistent_dispatch。显式 data-session-reuse off 是兼容/回滚入口，不是默认路径。

TLS/data TLS、压缩 auto 和 global scheduler 不默认打开：它们分别需要凭据或证书、对端能力、CPU/数据特征或链路容量策略，必须由部署/实验配置显式选择并在 summary 中报告实际能力。

## 证据

V3-WORKER-STATE-01C 的定向 CTest 22/22、全量 CTest 299/299、mixed/dense/large 动态队列回归和 16 轮 TLS control/data/handshake RST 矩阵均通过；io_uring 测试只因环境能力缺失而 skip。详见 docs/tasks/2026-10-14-v3-worker-state-01c-result.md。

## 回滚

对实现提交执行 git revert；保留测试和结果文档。不要 reset/clean 权威工作区。
