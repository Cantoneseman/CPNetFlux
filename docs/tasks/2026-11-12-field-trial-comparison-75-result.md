# Stage 75 result and serial return

阶段结论：**A_PASS；B 因实验脚本配置错误在 payload 前失败；整体 NOT_READY，用户目标 OPEN。** 本轮已经结束，写入权交回 root，禁止据本记录继续启动 payload。

## Scope and fixed input

任务范围、非目标与验收先行记录于 `serial-receive.json`、`scope.md` 及输入提交内的 `docs/tasks/2026-11-12-field-trial-comparison-75.md`。本聊天只写阶段75实验目录与获准的阶段74 `finish-request.json`；未修改项目源码、Git index、分支、网卡或其他系统设置。临时 RX=4096 的实施和回滚由 root 阶段74控制器独立负责。

- 双端输入提交：`837616dfe9715c6fabba5cdc361b05a5fa5213ba`；运行时代码基准 `42718ac26be24d53ec17525a7b1b3bcb1e340931`。R1 分支 `codex/V3-GRIDFTP-OPTIMIZED-RERUN-01`，R2 同提交 detached。
- 两端固定构建成功、配置相同、四个二进制 SHA-256 一致，见 `fixed-build.json`、`fixed-build-r2.json`、`fixed-build.log`；RelWithDebInfo，RDMA/IO_URING/ISAL/TLS ON，Desktop OFF，版本0.1.0。启用 RDMA 构建不表示本次走 verbs；本次实际数据面为 V3/TCP。
- 数据方向 R770-2 → R770-1，显式数据地址及本地 bind 为 `192.168.100.2` / `192.168.100.1`；管理 SSH 独立。路由、接口、HCA 和实际四条 lane 证据在 `formal/`、`observations/`、`rows/`。固定源端口计划 SHA-256：`0d02d1ea31c8b51381901df356b39d5b5aa7525c5996da2767ae024a71611a85`。
- 同一完整分配且 mincore 确认驻留的 1 GiB 源文件复用于全部 A 样本；SHA-256 `e810157d6e5442b9b7b4e6b73834fbcc0a545fbac051f119aaee23ad1edca455`。其内容按官方 fixture 的8 MiB随机块重复128次生成，压缩关闭；未宣称独立随机整文件。fixture生成与驻留检查不计入任务时间。

## A: five complete pairs passed

每次传输1 GiB，CPNetFlux和GridFTP各5次，累计有效payload 10 GiB。顺序为 CP→Grid、Grid→CP，逐对交替；无 payload 重试、筛选或补跑。

| Pair | CPNetFlux task Gbps | GridFTP task Gbps |
|---|---:|---:|
| 1 | 31.738363 | 12.584699 |
| 2 | 33.337974 | 12.351519 |
| 3 | 29.435458 | 11.665792 |
| 4 | 30.783956 | 12.405003 |
| 5 | 31.074866 | 11.932503 |

| Tool | n | Median Gbps | Sample CV (n-1) | Population CV (n) |
|---|---:|---:|---:|---:|
| CPNetFlux | 5 | 31.0748663 | 4.560% | 4.079% |
| GridFTP | 5 | 12.3515187 | 3.095% | 2.768% |

两种 CV 算法均满足5%门槛；`A-status.json` 原 `cv` 字段为 population CV。中位数比值 2.515874 仅属于本次限定的 clear-data 配置比较，**双方认证、校验和内存写入语义不同**，不得解释为等安全配置的通用产品倍数或加密性能。

CPNetFlux使用匿名TCP控制、明文数据、CRC32C verified_chunks、commit_sync=none、私有tmpfs/mmap（hugewithin_size），4 lanes、256 KiB DATA、1 MiB chunk、64 KiB buffer，queued sender/inline receiver，每lane应用限速1.5e9 B/s，exact-read/kernel-pacing关闭。GridFTP保留GSI身份认证、明文数据及普通tmpfs，冻结参数 `-fast -p 4 -cc 4 -tcp-bs 1048576 -bs 262144 -r -cd`。精确 argv/env 以 `formal/*.json`、`commands/`、`rows/` 和 `peer-rows/` 为准。

CP任务分母包含RSS/fields/plan前置核验、私有命名空间/tmpfs设置、目标目录准备和客户端启动至退出；Grid任务分母包含目标目录准备和客户端（含GSI认证）启动至退出。外部SHA、观察采集、源文件生成与清理单列。内层旧 `admission/no A/B` scope 标签是复用驱动的单次诊断标签，阶段75结论由全部10行和外层审计决定。未单独测纯payload稳态或文件系统读写上限；这些是短任务吞吐，并非物理磁盘持久化吞吐。

每个CP样本完整接收1073741824 B、1024个verified chunks，completed_files=1、failed_files=0、控制终态成功且外部SHA一致。Grid两端均CODE=226、精确1 GiB且外部manifest/SHA一致。所有A窗口的已检查NIC/HCA错误、drop、missed/OOB与主机pswpout增量均为0，owned process/cgroup swap为0；采样期间双端RX4096/TX1024。实际lane计划队列/接收CPU一致（9/27/7/28），允许的预先冻结源端口候选绑定次数为1..3，并非全部首候选成功。

资源汇总见 `A-resource-summary.json`：R1/R2最低MemAvailable分别29051658240/28793196544 B，峰值owned RSS分别328806400/74354688 B。8 GiB是观察停止门槛；实际cgroup memory.max/high均为max，不宣称设置了硬限制。

## B: harness failure before payload; C and later groups not run

首个 `B-dense-1-cp`（2048×64 KiB）退出码2，日志 `experimental download write must be pwrite or mmap`。本实验脚本误设 `CPNETFLUX_EXPERIMENTAL_DOWNLOAD_WRITE=off`；源码 `tree_transfer_client.cpp:3253` 的合法值为pwrite/mmap，目录续测应使用pwrite。这是本轮实验脚本配置错误，**没有证明生产传输故障，也没有产生B性能样本**。

证据：`B-rows/B-dense-1-cp.json`、`samples/B-dense-1-cp/client.log`、`B-invalid-env-source-evidence.json`、`B-status.json`。`B-config-correction.json`仅记录下轮合法参数，未应用或重跑。dense其余轮次、tiny、mixed、反向和安全组未完成；C同一常驻服务≥1800秒/≥100任务及故障组均NOT_RUN；8/64 GiB均NOT_RUN。不能据本轮宣称64 GiB验收、100 Gbps/1 Tbps能力或实地部署就绪。

## Rollback, cleanup, checks and ownership

B失败后立即发出获准finish信号，阶段74控制器以owner request结束，无rollback/heartbeat错误；双端RX/TX已恢复1024/1024，RSS/队列/IRQ相关关键配置与原值一致，原2121服务PID/start identity保留。`rollback-verification.json`、`postflight-r1.json`、`postflight-r2.json`与最终只读 `release-check.json`记录证据。

临时服务/监听、私有挂载、源/目标payload与临时凭据已移除，两端没有阶段75负载进程。清理时错误调用一次不支持的 `source-check` action，被参数解析拒绝且未改数据；后续官方cleanup成功，原错误保留于 `cleanup.json`，`cleanup-final.json`记录无未解决残留。

最终 `audit75.py`只读回放22/22通过，当前14个实验Python文件语法检查通过（`script-compile.json`）。首次审计误加“所有源端口首候选成功”的合同外条件，原失败保留于 `audit-first.json`，更正说明见 `audit-correction.json`；未更换样本或改原始证据。此次收尾未新增CMake/CTest，不将此前构建收据称为新测试；生产源码未改。

复核命令（无payload）：

```sh
cd /home/Su/experiments/CPNetFlux/2026-11-12-field-trial-comparison-75
PYTHONDONTWRITEBYTECODE=1 python3 -B audit75.py
sha256sum -c SHA256SUMS
```

完整文件校验清单为 `SHA256SUMS`（不包含其自身）。R2证据已复制到 `r2-evidence/`，无凭据、构建产物或大payload。源码tracked diff保持空，R1原有两份无关untracked worker-state文档保留，R2工作区保持原状。**本阶段新证据尚未提交/推送GitHub或归档Windows**，由root接回后串行处理，不能称GitHub备份完成。

写入权已释放，root可接回。下一步由root制定新的有界B/C合同：目录使用pwrite，明确计时、认证/校验语义和停止条件；在任何新传输前重新获得有效实验窗口，不复用已结束的阶段74租约。


Root交接后复核：499文件SHA与22项只读审计回放通过；本仓库结果及freeze随后由root提交并推送，原实验writer的NOT_DONE属于交还时状态。GitHub只备份小结果/冻结摘要，完整原始证据另作Windows本地归档。
