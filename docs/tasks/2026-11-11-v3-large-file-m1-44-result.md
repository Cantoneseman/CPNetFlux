# 8 GiB完整大文件M1验收44结果

日期：2026-11-11
状态：PASS
任务合同：[M1-44计划](2026-11-11-v3-large-file-m1-44.md)
实验输入提交：506b63be71bc5aea5eca92cab549c6d4adb2ff51
分支：codex/V3-GRIDFTP-OPTIMIZED-RERUN-01

## 结论

M1正式门通过。R770-2驻留tmpfs源传到R770-1全新tmpfs目标，8 GiB单文件、4路TCP、完整V3任务，五对交错default/combined共10个样本全部完成。combined中位任务速率34.159 Gbps，超过30 Gbps门槛；相对default中位28.126 Gbps，任务速率中位变化+21.451%，五对均改善。

这只证明本提交、本负载和本TCP/tmpfs实验合同通过M1。combined同时启用了256 KiB DATA和1 MiB TCP接收缓冲，本轮没有拆分两者各自的8 GiB贡献；40–50、70–85、100 Gbps、磁盘持久化、原生RDMA和生产readiness仍未验收。两项机制保持显式实验配置，不据此改默认值。

## 固定条件与结果

提交、RelWithDebInfo配置、TCP运行路径及运行时SHA在两端一致。目标数据量为8,589,934,592字节，8,192个verified chunks，4路TCP、CRC32C、1 MiB恢复chunk、checkpoint16、实验mmap接收、TLS/压缩关闭、commit_sync=none。R2源SHA-256为f03e5250deb5d64c1e1fdb2190fee31c0fdaefcd1ffeb366bde6a278867a044d，2,097,152个4 KiB页面全部驻留。

| 配对 | default Gbps | combined Gbps | combined配对提升 |
|---|---:|---:|---:|
| 1 | 27.391177 | 32.881294 | 16.697% |
| 2 | 29.039485 | 35.044445 | 17.135% |
| 3 | 28.125991 | 33.428638 | 15.863% |
| 4 | 28.213643 | 36.006887 | 21.644% |
| 5 | 28.104136 | 34.159410 | 17.727% |

default中位2.443273秒/28.125991 Gbps，CV 1.852%；combined中位2.011729秒/34.159410 Gbps，CV 3.238%。五对改善、两臂CV不超过5%、combined中位不低于30 Gbps。DATA帧中位数由139,264降至32,768（-76.5%）；接收端CPU采样下界中位由7.99秒降至6.77秒（-15.3%）。两臂同时改变帧尺寸和接收buffer，不能把这些差异归因给单个机制。

## 正确性、网络与资源门

- 10/10任务结果为pass；每个目标SHA与期望SHA相同，8,192个verified chunks完成，8个range平衡并回到零。
- 10/10 rx_missed_errors增量为0；R1每样本host pswpout增量为0，R2服务端任务前后也为0。
- R1接收端最低MemAvailable为21,956,263,936字节；R2服务端最低为21,413,867,520字节，均高于12 GiB。两端owned swap峰值均为0。
- 每个输出槽均按receipt移除；cleanup receipt确认R1本地payload根、R2源和R2 payload根均已删除。复核时两端payload根不存在，实验进程及49335监听均不存在。
- R1/R2在固定提交上完成py_compile、large-data脚本测试8/8、shared baseline脚本测试9/9、CMake构建及相关CTest 5/5；运行时三份二进制SHA一致。

因此performance_gate_pass、network_gate_pass、cleanup_gate_pass及m1_pass均为true。M1-41仍是因资源门中止且没有性能样本的独立旧尝试，本结果不回写、不替代，也不重跑M1-41。

## 后续工作

保持M1 combined为显式性能基线，不晋级默认。下一步先在不超过1 GiB的受限V3 TCP诊断中启用现有\`CPNETFLUX_RECEIVER_PROFILE=1\`，记录DATA分配/复用、逻辑copy、CRC、writer线程CPU和物理写调用，区分接收端成本；该profile会增加观测开销，吞吐不作为性能结果。基于证据只选择一个机制，再另立固定对照、资源预算、完整性/网络/清理门和停止条件。不要将两项combined改动的收益拆分归因，也不要进行无界参数扫描。原生RDMA优化仍暂停。

## 证据

- R1证据目录：/home/Su/experiments/CPNetFlux/2026-11-11-v3-large-file-m1-44
- R2源驻留和服务端回执：同目录下peer/source.json及peer/large-data-m1.server.done.json
- analysis.json SHA-256：dc7f6d054392625875d1e7a0a0eff33a7e725848bbb441495ca5af962f781994
- results.json SHA-256：6741a6a3f1d4d93586e4abcdf55b8a776031622b0ecaaadf70ca0e9b73084a7f
- cleanup.json SHA-256：3e3bf47b24d2a48a8bbc3d8c6f4e8ddf3fc0f9be5baf450412e345d3f57005cc
- matching-builds.json SHA-256：c4e80fbd1b6ed88c94e1e81c363ab4eed6298a3f72c7b1c95aa5db193a3f97e3
