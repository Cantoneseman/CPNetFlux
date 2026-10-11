# EXP-V3-GRIDFTP-01 启动回执

- 输入 CPNetFlux 源码提交：b3e4dc0e8468cad4d09d7d4a47c8dc69fd15c55f（V1-RETIRE-01）
- 实验脚本提交：2a682ccc782bd0954d87e64cc88c9fa2459d3788（GitHub remote 分支同 SHA）
- 深圳 runner：PID 993271；supervisor PID 993269；状态文件 /tmp/cpnetflux-runs/EXP-V3-GRIDFTP-01/run-20261011-02/runner.pid
- 上海 peer：PID 1471402；临时服务端口 CPNetFlux 22421、GSI 28411；peer 自动上限 2400 秒
- 运行目录：/tmp/cpnetflux-runs/EXP-V3-GRIDFTP-01/run-20261011-02
- 日志：深圳 runner-2.log、results.jsonl、summary.json；上海 peer-launch-2.log、peer/services.log
- 启动时间：2026-10-11T05:44:37Z
- 前台预检：GSI proxy timeleft 38146 秒；深圳/上海可用空间分别约 11.7/34.8 GB；CP TLS 与 GridFTP GSI PRIVATE 64 KiB 读写及 SHA256 通过；129 MiB 四 range 预检通过。
- 约束：3轮交替性能矩阵、single/dense/mixed；skew small-file static/dynamic；range 1/4通道；中断、checkpoint、损坏块、resume。所有 payload 和证据只在上述 run 目录；不停止既有 2811/22410 服务。
- 本回执只证明 runner 已启动，不预先解释性能结果；完成/失败由 runner 的 summary.json 和 results.jsonl 判定。
