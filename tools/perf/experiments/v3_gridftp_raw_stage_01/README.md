# V3-GRIDFTP-RAW-STAGE-01 实验工具

这是 2026-10-22 R730→R750 有界实验的版本化脚本，不是部署工具。
运行记录固定版本、固定数据网口与测试端口；不会修改系统网络配置。

- `runner.py`：确定性数据、V3/GSI-private GridFTP、SHA-256 树校验、阶段与 CPU 记录。
- `sample.py`：约 0.5 秒采集 NIC、CPU、内存、进程 FD/I/O 和 TCP socket。
- `environment.py`：记录公开环境事实与 GSI 剩余有效期，不输出 proxy 内容。
- `analyze.py`：严格校验 36 个正式/配对样本以及 GridFTP per-file 日志覆盖，输出 CSV/JSON。
- `test_analyze.py`：并发区间、未知值和零字节文件的统计口径回归。
- `cleanup.py`：只清理本任务列明的两个运行目录中的 payload/凭据/临时落地文件；先拒绝仍有实验监听的情况。

输入准备必须由受权管理员在实验目录之外提供服务端证书、token 和 GSI 身份，
不得从仓库获取秘密；刷新 GSI proxy 后再启动。两端 Release 二进制 SHA 必须与结果报告一致。
两个 `credentials`/`tls` 目录及其内容只存在于独占运行目录，目录 700、秘密文件 600。
脚本目前含本轮 IP、端口及路径，重做其他环境前需要先审查这些常量和允许清理路径。

本轮选择 `CHANNELS=4 PENDING=8 FAST_POLICY=1`，每数据集三次。
`SYSTEMS=v3 SUMMARY_MODE=on RUN_TAG=formal-v3` 为阶段观测组，
`SYSTEMS=gridftp RUN_TAG=formal-gridftp` 为 GSI/private 对照，
`SYSTEMS=v3 SUMMARY_MODE=off RUN_TAG=formal-v3-no-summary` 为报告开销配对组。
运行前设置 `CPNETFLUX_RAW_BASE` 指向已核对的专属目录；三组必须串行启动。

`TRACE_SYSCALLS=1` 只用于独立诊断，不能混入正式耗时。
GridFTP debug 流在内存中归类后仅保存命令名/响应码，认证数据不落日志。
子阶段可以重叠，不能把它们累加当目录 wall time。
此驱动用于人工监督的固定规模实验，不是可无人值守的通用调度器；
stdout 流读取和异常清理仍有工具层局限，运行中必须跟踪真实进程。

分析现有已归档证据：

```bash
python3 -m unittest discover -s tools/perf/experiments/v3_gridftp_raw_stage_01 -p 'test_*.py' -v
python3 tools/perf/experiments/v3_gridftp_raw_stage_01/analyze.py /tmp/cpnetflux-runs/V3-GRIDFTP-RAW-STAGE-01/20261022T044500Z-v3-raw
```

证据目录中的 `peer/` 是 R750 脱敏证据，其他路径来自 R730；二者均不包含 payload、token、proxy 或 TLS 私钥。
