# CPNetFlux E0 / E1 基线实验结果

日期：2026-11-23
状态：E0 通过；E1 Stage A 正式矩阵完成；Stage B 因资源门未通过而停止，未完成验收。

## 任务记录

- 范围：复核双机准入和当前 V3 raw 数据路径；运行 E1 的 1 GiB 文件数/尺寸基线、输出存储诊断及有资源门约束的 8 GiB pilot；归档实际证据。
- 非目标：不改生产源码、协议、网卡或系统网络；不实现 WorkUnit；不运行 E2–E6；不把 TCP 描述为原生 verbs；不将资源门失败的 Stage B 结果作为有效性能数据。
- 验收标准：两端固定同一输入提交和二进制；样本有完整性、协议终态、资源及网络计数证据；预注册的 56 个 Stage A 样本全部有效；报告记录失败/受限项、输入、命令和证据路径；只提交本次报告。
- 受影响文件：本文件；仓库摘要 docs/tasks/2026-11-23-cpnetflux-paper-e1-baseline-result.md。实验数据及日志仅保存在 /home/Su/experiments/CPNetFlux/2026-11-23-paper-E1-v3-baseline-01/。

## 结论

E1 Stage A 的 56/56 个正式样本均通过完整性、V3 终态、资源、计数证据、源数据稳定性和字节数检查。固定总量 1 GiB 时，增加小文件数和任务规模会显著增加 T_ready：相对 1,024 小文件基线，4,096、16,384、65,536 小文件的中位数分别增加 31.37%、152.46%、671.80%。该结果支持“当前端到端代价随文件数上升”的诊断，但没有单独识别元数据、manifest、客户端事件记录、文件系统或 CPU 的因果份额。

Stage B 的一个 8 GiB pilot 实际传完并通过文件、目录、SHA/tree 与双方协议终态检查；但 R770-1 记录到 1 页 pswpout，违反预注册资源门。该样本的 T_ready 不纳入性能结论；另外三个 Stage B 布局及正式重复矩阵均未运行。因此 E1 Stage B 不验收，本报告不据此宣称 8 GiB 性能或 100 Gbps 能力。

## 输入与配置

- 方案：docs/ideas/2026-11-23-cpnetflux-paper-experimental-plan.md。
- 输入提交：1b131bca20c33b17a512ad8840ec50a0a7762807；R770-1、R770-2 在实验及构建时均为该提交。
- 分支：R770-1 codex/V3-GRIDFTP-OPTIMIZED-RERUN-01；R770-2 使用相同提交的 detached checkout。
- 构建：Ninja / RelWithDebInfo；tests、TLS、RDMA、ISA-L、io_uring 启用；desktop 与 cross-domain tests 关闭；两端 CMake configure/build 通过。
- R770-1 / R770-2 SHA-256 相同：server 7494921db066202ccdca7a952a3c6eb4ed5d0f81dc50a527a9d74cdd2fb83d49；tree download 831419e03b53f0db96d06ccfc26f760fc669307975d050ddf1cc3f8ec5688e35；tree upload 326305f4c53f43895d4f1d9c81335e0219d61a437876dc01bebe9ad86921bb6e；private tmpfs launcher dba043846ccf0206d1ed00c3436221ab92920174e412c5f4e42e116c97f369fb；unit tests 9f4b2d6a185790e0e482b3ff59653a5779489246f33103b3b2888d475e478aa6。
- 原始数据面：显式 bind 192.168.100.1 / 192.168.100.2，经 eno17095np0、100 GbE 直连；应用结果为 V3/TCP，非原生 RDMA verbs。compression、TLS 均关闭；CRC32C；1 MiB chunk、256 KiB DATA、64 KiB buffer；4 connections / file parallelism 4；verified_chunks；每 16 chunks 持久化；commit-sync none；worker 控制连接复用、tree 数据会话复用、dynamic file scheduling；应用 pacer 1.5 GB/s/lane，kernel pacing off。
- 这是 synthetic controlled data；未找到 WRF/RSL 文件，发现的数据集 ZIP 未用于本实验。T_ready 不是纯网络速率，底层测试目录位于 /home 的 ext4。

## Stage A 方法与结果

正式矩阵固定 1 GiB 逻辑总量，随机化 AB/BA 顺序；每项 7 个配对重复、共 56 个正式样本。T_ready 从外部源 SHA/tree 扫描开始，覆盖客户端启动至终止、目标端逐文件 SHA/tree 校验和源身份复核；排除合成数据生成、服务启停与日志取回。表中比值为候选布局相对 1,024 小文件控制组的 T_ready 中位数比值；95% 区间按预注册的配对 bootstrap 报告。

| 对比 | 控制组 T_ready 中位数 | 候选组 T_ready 中位数 | 候选/控制 | 配对中位差 | 95% 区间 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1,024 vs 4,096 小文件 | 3.3698 s | 4.4084 s | 1.3137 (+31.37%) | 1.0579 s | [1.2995, 1.3218] |
| 1,024 vs 16,384 小文件 | 3.3653 s | 8.5776 s | 2.5246 (+152.46%) | 5.1800 s | [2.4671, 2.5884] |
| 1,024 vs 65,536 小文件 | 3.3700 s | 25.9226 s | 7.7180 (+671.80%) | 22.5808 s | [7.6450, 7.7889] |

另一个尺寸诊断比较单文件 768 MiB 与 1 GiB：T_ready 中位数为 2.5127 s 与 3.1735 s，比例 1.2529（区间 [1.2349, 1.2759]）。它不是等字节文件数对照；字节量增加 33.3%，T_ready 增加 25.3%，只能视作尺寸伸缩诊断。

文件数对照中，65,536 小文件候选组的客户端 launch-to-exit 中位数为 16.46 s，客户端 server event log 中位数约 392.8 MB，客户端 JSON summary 中位数约 321.0 MB，manifest 写入累计中位数约 9.14 s。这些是观测到的相关阶段指标，不是互斥墙钟分解，不能直接相加或当作已证实根因。4,096 / 16,384 组也显示 log、summary、manifest 体积随文件数上升。

观测输出落盘影响诊断：1024 小文件组和 65,536 小文件组各做 3 次将详细日志/summary 输出重定向到两端 tmpfs 的样本。与 ext4 正式组中位数相比，T_ready 分别低约 0.60% 和 0.48%。这说明本次改输出存储的中位数影响较小，但详细事件生成、JSON 序列化及计时开销仍在，不能据此宣称没有 observer effect。

## Stage B 停止原因

预注册的 Stage B 要求 4 种 8 GiB 布局各通过 1 次 pilot 后才运行配对矩阵。第一轮 8 GiB 数据生成/哈希耗时 584.49 s，SSH 外层 session 重置发生在 server 启动后、payload 开始前；保留证据显示无认证和客户端事件。优化后的周期块生成器与这次输入树哈希一致，第二轮生成耗时 9.34 s。

第二轮上传 8,589,934,592 bytes，客户端返回码 0；8,193 个文件、258 个目录全部完成，V3/TCP、双方终态和外部完整性检查通过。任务 T_ready 记录为 34.9556 s，但 R770-1 的 pswpout delta 为 1 页（R770-2 为 0），所以 resource_pass=false。按预注册 stop gate，不把这个数作为可比较性能结果，不继续其他 Stage B 布局或重复样本。

## 网络与资源限制

- 接口 RX/TX ring 均保持 1024，实验中未做系统网络调优。netdev rx_errors/tx_errors、drops 及 mlx5 CRC/物理 discard 在样本记录中为 0。
- mlx5 rx_out_of_buffer 与 R770-1 rx_missed_errors 有对应观测；各组 control/candidate 计数和非零样本数分别为：4,096 组 19/0（1/7、0/7）；16,384 组 337/207（1/7、2/7）；65,536 组 106/188（1/7、2/7）；尺寸组 0/193（0/7、1/7）。计数无法逐次归因到 T_ready 变化，故不称网络干净，也不声称这些计数导致减速。
- 原生 HCA sysfs 计数不可用/为空；保留的是接口和 ethtool 证据。不能从本轮报告推断 HCA 层无错误。

## 门禁、失败记录与证据

- R770-2 全量 CTest：带进程级临时测试 token 执行，856/856 通过，0 failures，329.57 s。R770-2 证据路径：/home/Su/experiments/CPNetFlux/2026-11-23-paper-E1-v3-baseline-01/ctest-r2-full.log、ctest-r2-full.json。
- R770-1 首次全量 CTest：token smoke 通过；cpnetflux_source_port_plan_smoke 的重复试验端口集合发生一次交集（53429、41687 与 34979、41687），触发该测试的无重叠断言。该项单独重跑 1/1 通过；随后全量重跑 856/856 通过、0 failures、330.31 s。R770-1 证据路径：/home/Su/experiments/CPNetFlux/2026-11-23-paper-E1-v3-baseline-01/ctest-r1-full-rerun.log、ctest-r1-full-rerun.json。首次单项端口冲突作为测试偶发项记录，不归因为产品故障。
- 命令记录：Stage A 使用本轮冻结的 e1_matrix_runner.py、matrix-plan.json 和 matrix-order.jsonl；每个样本目录保存 command.txt 中的 server/client 实际 argv。双端全量门禁命令为 ctest --test-dir /home/Su/projects/CPNetFlux/build --output-on-failure -j 1；CPNETFLUX_TEST_TOKEN 仅在测试进程环境中临时设置，未写入文件、命令日志或仓库。
- 所有 Stage A 正式样本：matrix-final-analysis.json 标记 all_valid=true、56 rows；matrix-order.jsonl 的 SHA-256 为 9b542eed391a7afe66ab095717783f2db504840da22068fc0eb9d344ff5d8188；matrix-results.jsonl SHA-256 为 8dbe03f88ef8021b0235b1fbde8da93e7749f169d1fe586e8f141085d2b1e550。
- 失败/无效的诊断 harness 输出及中断尝试未删除或混入正式样本；包括 tree hash 表示格式修正、初次网络计数硬拒绝策略修正、tmpfs observer 早期 harness 错误及 Stage B trial 1 中断记录。正式结果以 frozen matrix order 和逐样本 trial-result 为准。
- 总索引：/home/Su/experiments/CPNetFlux/2026-11-23-paper-E1-v3-baseline-01/；环境/构建：e0-inventory.json、e0-build.json；输入：matrix-plan.json、matrix-preflight.json；样本：matrix-results.jsonl、cases/*/trial-result.json；网络计数：各样本 network_delta；Stage B：stageb-contract.json、stageb-pilot-interrupted.json、cases/stageb-8192-trial02/trial-result.json；observer 诊断：observer-tmpfs-analysis.json。

## 决定与下一步

E1 Stage A 支持继续评估文件数敏感路径，但当前证据还不能证明某个单一 CPU、manifest、存储或网络机制是主要根因。Stage B 因资源门停止；没有实现 WorkUnit，也没有进入 E2。后续阶段应由用户确认新的准入条件后另行执行。
