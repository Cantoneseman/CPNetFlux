# V3-GRIDFTP-RAW-STAGE-01：无压缩 V3 / GridFTP 阶段对照结果

日期：2026-10-22；状态：实验完成，发现性能和资源释放缺陷，不是生产就绪验收。

## 版本与环境

- 权威工作区：深圳 `/root/projects/CPNetFlux`，`codex/V3-DESKTOP-NETWORK-BINDING-01`。
- 运行时代码：`55f7a21bb970cc0528d21a52901627aa96660b44`；初始 HEAD `3dd7bffeb012bfb65d6b7ad6aa8acf2834ddb171` 只增加文档。实验计划提交 `c74eeacd05b327519023badcac657d662b07647b`。
- 复用已有 Release / TLS ON / io_uring compiled ON 构建，`src/`、`include/` 与权威源码逐文件比较无差异。本轮不改 runtime、agent、UI、正式安装、路由或防火墙。
- 服务端二进制 SHA-256：`6633aad6915683a1bae13e1c890b6256d4f268956fed1532af1ccb99a978ad6e`。
- 上传客户端 SHA-256：`9deb6e94873b35d7e058a0bb8ae9b096e5f687f5bc01c9a8fe5e0dfd6512d5d3`。两台机器对应二进制 SHA 一致。
- R730 发送：双 Xeon E5-2680 v3、48 逻辑 CPU；R750 接收：双 Xeon Silver 4314、64 逻辑 CPU。
- 数据地址 `192.168.100.1` → `192.168.100.2`，接口分别 `enp130s0np0` / `enp177s0np0`，MTU 9000，接口协商 100000 Mb/s；10 次 ping 平均 RTT 0.102 ms。
- 这是本地直连网口上的普通 TCP，既不是原生 verbs，也不是深圳—上海跨域实验。5 秒 iperf3 单流 / 8 流接收分别 **32.245 / 32.534 Gbps**，不视作长期带宽上限。
- GridFTP 客户端 10.10、服务端 13.22；刷新 12 小时 GSI proxy，权限 600，CA/主机名/身份核验后执行 smoke。正式 GSI/private 全部成功，无认证失败混入吞吐。
- 输入总计 **10,875,830,272 bytes = 10.12890625 GiB**：dense 2048×64 KiB；两份各 4 GiB 大文件（确定性伪随机 / 重复内容）；mixed 2 GiB 大文件 + 512×8 KiB + 1 个空文件，共 514 文件。
- 每样本前校验源、结束后对接收目录计算同口径 SHA-256 树哈希；源树哈希生成与外部接收端 SHA 校验均不计入传输 wall。未清页缓存，发送侧正式样本物理读取采样为 0，属于该输入规模下的热缓存实验，不能外推冷盘持续吞吐。

## 配置筛选与正式矩阵

V3 使用真实 `persistent_tree`、`actual_transport=v3`、动态环状队列（summary 的 `actual_file_scheduling=dynamic`）、共享工作租约、动态大文件 range、worker 控制连接复用、tree 数据连接复用、CRC32C、TLS 控制/数据加密、全局 fixed 调度；`--compression off`，阶段观测组压缩尝试 / 压缩帧均为 0。
4 GiB / 2 GiB 大文件实际各 8 个 range，dense 小文件保持文件级并行；正式 summary 每样本控制/数据连接均为 4，不是每文件建连。`control_pipeline_depth=0` 是旧控制命令流水线配置，实际为 `shared_persistent_dispatch`，不表示 worker/tree 复用关闭。

筛选 V3 4/8/10 路、GridFTP 4/8/10/16 路；16 路 V3 被 CLI 的最大 10 路约束拒绝，记为配置探测失败，无性能值。4 路综合最佳，V3 4/8/10 路差异很小，GridFTP dense 增加并发明显更慢。这里只称“本轮已测配置中的最佳”，没有穷举全部参数。

正式 V3：4 路、pending window 8、chunk 4 MiB、POSIX I/O；接收端 `manifest-flush-policy=final_only`、`final-verify-policy=verified_chunks`、`commit-sync-policy=none`、preallocate off；全局 work item min/max 均 512 MiB、capacity 100 Gbps、default RTT budget 160 ms。160 ms 是固定调度模型参数，**不是实测 RTT**。最终批次完整性审计仍由现有代码执行；此配置不承诺每个 checkpoint 已 fsync。

GridFTP 使用 `-dcpriv -fast -cc 4 -p 4 -r -cd -rp -rst -vb -dbg`，服务端 GSI 与 encrypt-data；`-fast` 启用 MODE E 和数据通道复用。debug 在内存中过滤后只保存命令名 / 响应码，认证交换内容不写日志。双方均没有显式逐文件 fsync 保证。

三组依次运行，各 4 数据集×3 次，共 **36/36 成功**：V3 阶段观测组、GridFTP 对照组、V3 去掉 JSON summary 的配对组。另有独立参数筛选、失败复现与 strace 诊断，不混入正式统计。没有比较压缩版本。

## 性能

下表为三次中位数，时间从客户端进程启动至退出，不含独立 SHA-256 树校验。V3 无 summary 组继续保留相同传输策略和阶段计时，避免“软件 CRC32C 重扫源目录”主导进程退出时间。GridFTP 本轮未启用应用层逐文件 checksum，私密数据通道保持 GSI 保护；双方同样另做 SHA-256，不能宣称两者事务/恢复/校验功能成本完全相同。

| 数据集 | V3 无 summary 秒 | V3 Gbps | GridFTP 秒 | GridFTP Gbps | V3 / GridFTP 耗时 |
|---|---:|---:|---:|---:|---:|
| dense，128 MiB / 2048 文件 | 19.964 | 0.054 | 1.958 | 0.548 | 10.20 |
| 伪随机大文件，4 GiB | 9.390 | 3.659 | 6.296 | 5.458 | 1.49 |
| 重复内容大文件，4 GiB | 9.291 | 3.698 | 6.374 | 5.390 | 1.46 |
| mixed，约 2 GiB / 514 文件 | 5.780 | 2.978 | 4.183 | 4.115 | 1.38 |

带 JSON summary 的 V3 完整 CLI 中位耗时依次 **22.525 / 20.419 / 21.276 / 12.749 秒**；summary 自报 core elapsed 依次 **20.561 / 8.610 / 9.014 / 6.126 秒**。二者不是同一边界，主表不能用 core elapsed 冒充进程 wall。
core 结束到进程退出的区间中位数依次 **1.853 / 11.809 / 11.984 / 6.565 秒**。core 与此区间各取中位数，不要求它们之和严格等于另一列的中位数。
独立接收端 SHA-256 中位约 dense 0.55–0.59 秒、4 GiB 5.55–5.74 秒、mixed 3.01–3.05 秒，两端程序均单列记录。

## 阶段与瓶颈证据

以下阶段数据取 V3 阶段观测组三次中位数。control/data/complete wait 以及 per-file/range 指标是并发线程累计区间，存在重叠，**不得相加求 wall 或百分比**；queue_wait 是每个 work item 排队累计，更不代表目录多等待了上千秒。

| 指标（秒） | dense | 4 GiB 伪随机 | 4 GiB 重复 | mixed |
|---|---:|---:|---:|---:|
| scan | 0.065126 | 0.000134 | 0.000130 | 0.017204 |
| control_prepare 累计 | 0.249407 | 0.259471 | 0.267374 | 0.257354 |
| data_connect 累计 | 0.080582 | 0.090746 | 0.102313 | 0.103041 |
| transfer_complete_wait 累计 | 1.921778 | 0.048598 | 0.048721 | 0.499132 |
| 发送端 manifest 累计 | 77.530 | 0.004756 | 0.005310 | 5.035 |
| 发送端 payload_io 累计 | 0.509 | 31.947 | 33.839 | 17.013 |
| 接收端 write_range / write 累计 | 0.189 | 19.148 | 20.707 | 10.136 |
| 接收端 checksum 累计 | 0.171 | 4.392 | 4.536 | 2.434 |
| 接收端 finalize 累计 | 0.946 | 0.003106 | 0.003238 | 0.205 |

所有逐文件/逐 range 的 queue_wait、first_payload、read/write、payload_io、file_result、manifest、checksum、finalize 的 count/sum/p50/p95/max 见附件；未观测的值保留 null。接收端 write_range 区间包围整个 `RangeFileCoordinator::write`，含协调器锁等待与 checkpoint 处理，不是物理磁盘写服务时间。

1. **小文件主要卡在发送端整树 manifest 保存。** `tree_transfer_client.cpp` 的 callback 在全局 `manifestMutex` 下更新状态并每次 `saveManifest`；`tree_manifest.cpp` 每次序列化全目录并原子替换。dense 单文件 manifest 阶段中位约 38.34 ms，发送端约 20.31 user CPU 秒 / 22.53 wall 秒；接收端 per-file write 累计仅 0.189 秒。独立 strace 复现观察到 **4098 次 rename、15862 次 futex**，符合每文件 begin/complete 重写整树的代码路径。strace 诊断耗时不作性能样本，futex 统计不作 CPU 火焰图或 wall 百分比。
2. **观测输出有显著额外计算。** `writeJsonSummary` 先确定 elapsed，再调用 `computeTreeVerificationHash` 全量读源，后者固定 `ChecksumBackend::Software`。4 GiB 在 core 后多约 11.8–12 秒；去掉 summary 后进程耗时降至约 9.3 秒，直接配对支持该判断。应将报告耗时单列、复用已有校验证据或选择适当后端，不能为了好看的数字改计时边界。
3. **大文件剩余差距在 V3 数据路径，尚无证据精确归因到单个函数。** 去 summary 后仍比 GridFTP 慢约 1.46–1.49 倍。每 lane 的 payload pipeline 预算只有 131064 bytes（约两个最大 frame），4 GiB 观测到约 6.4 万次 producer wait；接收端 write_range 累计约 19–21 秒，源码逐帧持有共享文件协调器锁。这些是下一轮应测的流水线背压、共享写锁、重复校验和 frame 处理热点，尚未通过单变量消融或 CPU 栈采样证明各自贡献。
4. **目录批次持有已完成文件句柄，是已复现的稳定性缺陷。** 默认 `ulimit -n=1024` 下，dense 两次在约 1000 文件失败；采样 FD 757→815→877→935→996，随后失败。源码 `integrityChecks` 捕获 `shared_ptr<FileTransaction>`，事务持有文件至批次审计。将本批 runner/V3 supervisor 的 soft limit 提至 8192 后 dense 全过，正式接收端峰值 **2064 FD**。这只是实验规避，**没有修复释放逻辑**；不能因高 limit 成功而宣布默认环境稳定。
5. **本轮不是 100G 链路打满。** 无 NIC 新增错误/丢包，系统整体 iowait 很低，CPU 总体空闲不表示单线程或全局锁没有瓶颈。额外 1 GiB direct-I/O 短探测：R730 写/读约 1.1/1.1 GB/s，R750 写/读约 6.0/2.5 GB/s；可能受 RAID/controller cache 影响，只作本轮短探测，不能当物理介质持续性能。正式输入热缓存，不能据此排除冷盘瓶颈。

GridFTP 每文件 START/DATE/STREAMS/NBYTES/CODE=226 日志与 12 样本文件数、字节数全覆盖。提供文件区间 union/span 和进程 wall 的剩余区间；不把剩余全部叫作 GSI 认证耗时，也不把与 V3 不同边界的 START/DATE 冒充相同 payload I/O 阶段。

## 验证、归档与清理

- 分析脚本 6/6 单测、Python compileall、36 个正式样本严格审计和 GridFTP 文件日志覆盖通过。
- 原始环境采集器给 `grid-proxy-info` 多传了不适用的 `-bits`，该条 exit 1 已原样保留，不冒称采集成功；正式前单独 `-timeleft` 读取为 39364 秒，之后所有 GSI/private 样本通过。归档脚本修正为 `-all`；R730 没有 GridFTP server 二进制是角色分工事实，R750 承担对照服务。`globus-url-copy -version` 的版本写在 stderr 且返回 1，原始事实保留。
- 定向 CTest 114/114 通过。第一次全量并行轮 413/416：2 项缺测试 token 环境变量、1 项端口占用。补临时测试 token 后串行全量 412/416，4 个恢复测试报告端口占用；随后这 4 项单独串行重跑 **4/4 通过**。不将两次有失败的全量轮写成 416/416 全绿，不在本任务擅自停止原有测试代理或服务。该构建此前验收记录不替代本次失败事实。
- 原始脱敏证据在深圳 `/tmp/cpnetflux-runs/V3-GRIDFTP-RAW-STAGE-01/20261022T044500Z-v3-raw`，`peer/` 为 R750。保留两轮全量及定向/补跑测试日志于父目录的 `V3-GRIDFTP-RAW-STAGE-01-*.log`。
- 原始 R730 归档 SHA-256：`d5588e68d8f3ce85a4630b46da857a333e7ec3cd7eebbdba2cdae42a019eceb8`；R750：`bbd2a22f60ce113fce2c3e7d6493f8d80156d667194fbe7def930e94de9ae808`。归档在深圳 `/tmp/cpnetflux-runs/cpnetflux-raw-r7{30,50}-evidence.tgz`，不入 Git。
- Git 附件目录 `docs/experiments/2026-10-22-v3-gridftp-raw-stage-01/` 包含 36 行案例、三次中位数、发送端阶段 CSV、双端阶段/资源诊断 JSON 和输入哈希；工具位于 `tools/perf/experiments/v3_gridftp_raw_stage_01/`。
- 本批端口 34261 / 32811 / 35201 已关闭；两个预备/正式目录的 payload、接收落地数据、token、GSI proxy、TLS 私钥全部清除，`cleanup-final.json` 实查为 true。R730 释放约 10.1 GiB。未停止原有 desktop/agent 或其他服务，未修改或提交已有 UI/打包 dirty 成果。

建议后续顺序：先修复事务完成后 FD 释放并加低 nofile 回归；再处理 summary 的额外软件 CRC 扫描；再把目录 manifest 更新改为有界批量/增量持久化且保留恢复事实；最后对 range 共享写锁和 payload pipeline 做单变量消融。每项单独冻结版本，复用本轮数据和验收口径，避免一边改 runtime 一边声称仍在比较同一版本。
