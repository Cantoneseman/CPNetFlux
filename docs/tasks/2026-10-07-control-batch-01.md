# DIR-CONTROL-BATCH-01：控制命令批处理

- 输入提交：`99858c041e4bbf9a1eae659252b4a9daf880f94d`
- 目标：在不改变服务端协议语义、数据帧、manifest、checksum 或 resume 的前提下，把每个文件的 `EPSV` 与 `STOR/RETR` 放入同一个控制写入窗口，减少一个跨域 RTT。
- 范围：CPNetFlux tree client 的 ControlClient、上传/下载普通 worker 和 depth=1 candidate 准备路径；服务端继续按命令顺序回复。
- 非目标：本阶段不实现异步 server transfer、回复乱序、跨控制连接共享，也不修改 GridFTP 对照。
- 失败处理：任一批量回复解析失败立即走现有错误/取消路径；后续重新连接仍使用原同步命令。
- 验收：构建、控制命令/流水线单元测试、tree upload/download/parallel/control-reuse/pipeline smoke；短时深圳→上海 A/B，记录 phase timing、控制连接、结果和独立 manifest。
- 成功标准：目录结构和完整性不变；depth=0 的跨域墙钟不劣于当前基线；depth=1 仍受多 worker 回退保护，不因本改动重新引入额外流水线线程。

## 实际验收结果

- Shenzhen Release 构建完成；`run_gridftp_tree_pipeline_sidecar_smoke.py` 通过，定向 CTest 21/21 通过。
- 同一 128×1 MiB、file_parallelism=8、worker reuse、anonymous/TLS off 的深圳→上海短 A/B：候选 depth=0 wall 10.582519 s、depth=1 wall 10.686009 s；旧 guard 分别 10.833100 s、10.933076 s，外部 wall 改善 2.31%/2.26%。
- 两个候选 case 均 128/128、exit=0、control_connect_count=8；源与两个目标目录的 128-file SHA-256 manifest 均为 `308558804785d73f347364703c4564569a45cdf7b165296740676e30d81f5ea4`。
- 原始结果与日志回收至 `D:\Project\CPNetFlux-evidence\DIR-CONTROL-BATCH-PERF-01`；单次 A/B 不是统计充分结论。
