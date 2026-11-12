# 阶段64：交付C复核与正式比较中止结果

2026-11-12。受测提交 cc499b4aba246e7bbfcd94b2bd1edcf770d0a825。
**工程闭环保持；正式比较 NO-GO；稳定性未开始；上机候选未准入。**
[范围](2026-11-12-field-trial-comparison-64.md)与[冻结清单](2026-11-12-field-trial-comparison-64-freeze.json)。
本次没有修改运行时、默认配置或四个二进制，不重启40G/M2调优、原生RDMA或延期研究。

## 本次完成的交付工作

补齐两端连续资源监测，覆盖明确属于本次实验的父进程和子进程，保留20ms请求周期的实际时间戳、
MemAvailable/Free、owned swap、RSS/FD/线程和host pswpout。真实64MiB子进程测试两端通过。
监测周期包含proc扫描开销，不等于严格50Hz，也不等于能捕捉每次瞬态峰值；RSS不含所有tmpfs页。

首个CP样本因外部监测器提前退出被剔除，未计作正式成绩。无payload复现确认sudo的real UID=1002、
effective UID=0，普通Su不能读取其fd目录；旧监测器误用real UID判断。已修复特权控制进程分类，
仍禁止漏测非特权数据进程；两端真实sudo回归、终态监测报告重复读取及原始失败保留检查通过。
旧样本的内部40.614Gbps仅留原始记录，不能补算为有效配对。原/新harness均冻结，不覆盖旧行。

两端固定提交、同配置和四SHA已核对；GSI proxy在本次比较前分别就地刷新为12h并验证至少1h。
双向8MiB tmpfs认证、SHA、226、零网络/换页与清理检查通过，只作smoke。
GridFTP正式行补双端226/字节/实际STREAMS回执；Windows只转发管理命令，payload仍经
192.168.100.1/.2直传。未添加SSH密钥、修改网络/IRQ/RSS/THP/cache/swap或删除历史数据。

## GridFTP有界配置筛选

共同参数：GSI控制认证、数据通道未请求加密（未启用-dcpriv）、cc=4、TCP buffer请求1MiB（服务日志2MiB）、
block=256KiB、fast、压缩off；源为完全写入并驻留的1GiB tmpfs文件，目标每次新建。
以下是每档一次pilot，不是统计成绩或全局最优证明。

| streams | 完整目标任务秒 | 原始Gbps | 网络门 |
|---|---:|---:|---|
| 4 | 0.671844585 | 12.785598 | PASS |
| 8 | 0.741402847 | 11.586056 | PASS |
| 16 | 0.924462065 | 9.291819 | PASS |
| 32 | 0.974641463 | 8.813430 | FAIL：R1 OOB/missed各+760 |

32流触发即停止、保留失败，没有重跑32流。审查后按预定性能方向，从有效且已测候选中选取最快4流，
再开启独立正式配对阶段；不是把CP降档或用失败配置反复碰运气。选择边界见pilot-stop-review.json。
四流只是本次有界集合的选择，不能写成GridFTP在所有网络/规模上的最强配置。

## 正式1GiB配对的实际结果

CP配置保持阶段63：4 TCP、256KiB逻辑DATA+CRC、1MiB chunk、CRC32C/verified_chunks、
checkpoint16、queued发送/inline mmap接收、native私有tmpfs huge=within_size、
应用pacer每lane 1.5e9 B/s、kernel pacing/exact-read/TLS/压缩off、commit_sync=none。
每路映射64MiB，payload预算64MiB，接收buffer请求1MiB。未晋级默认。

两工具使用同字节、完全物化驻留源；文件原始SHA c5c96262f86d1a67182a41c48c34742e95c8b21e9b7df7afda209cc4edd33bf8，
官方树SHA c4f0cf04d7d559bf85bff30621969a2ab87fb86c76fb33ac8d85b0240a135c0d。
CP完整时间包含必要namespace/setup+目录+client启动至退出；Grid包含目录+client启动至退出。
源准备、SHA、观测与清理单列。CP私有目标与Grid普通tmpfs后端、安全语义差异均保留：
CP anonymous/CRC clear，Grid GSI控制与数据保护请求；不是安全等价或磁盘持久化对照。

| 正式顺序 | 完整秒 | 原始Gbps | 判定 |
|---|---:|---:|---|
| 第1对CP | 0.213292959 | 40.272940 | 全门PASS，单次样本 |
| 第1对GridFTP | 0.703118497 | 12.216909 | 全门PASS |
| 第2对GridFTP | 0.709915582 | 12.099938 | 全门PASS，尚未配齐 |
| 第2对CP | 0.261767137 | 32.815176 | 网络FAIL，剔除性能成绩并中止 |

**仅1/5完整有效配对，第二对不完整。** 不计算正式中位收益、CV或胜率，不发布速度倍数。
40.272940不是8GiB M2，也不是稳定40G；原始失败32.815176不是合格吞吐。
之后第3–5对、目录/反向/安全组均NOT_RUN（比较硬门停止）；8GiB仍NOT_RUN（物理free门）。

## 失败归因及下一项工作

失败CP的R1窄窗OOB+1275、missed+882，刷新尾窗missed再+393，最终两者均+1275。
成功CP主要数据队列12/15/23/27各约18.63万包；失败为4/16/23约18.63/37.13/18.63万包，
与两路落到同一接收队列的假设一致。队列16 pp_alloc_empty/slow各+3，
buff_alloc_err/wqe_err均0；广窗TCP重传+3、timeout+1。
这些不是逐流/RQ丢包证明，不能把聚合OOB归到队列16，也不能说已证实内存分配失败。
本次Grid32流也触发OOB，说明该事件不只在CP出现，但不能据此免除CP验收。

失败行的外部双端观察正常（R1 298/R2 287次采样）、host pswpout增量0、owned swap0、
最低MemAvailable约27.9GB；内容/1024 verified chunks、4096帧、应用pacer、实际PMD、
native setup及卸载/清理均通过。当前停止原因是网络稳定性，不是本次磁盘或内存资源门。

下一步先补有界数据连接四元组/实际RSS映射的可核对证据，确认是否确为流聚集及何时耗尽RQ；
已有汇总日志没有端口，不能凭包量反推出每一lane。先无payload/小控制连接验证观测合同，
再明确一个可证伪的接收服务/突发机制假设与有界修复；不直接扩队列/增线程/降速扫参或改系统RSS。
只有新修复完成回归、提交推送、双端构建/SHA后才能新开比较批次，旧失败不变。
本报告没有实现或验证RSS分流、跨lane统一整形，也不承诺这些设计必定解决问题。

## 验收与交接状态

| 项目 | 状态 |
|---|---|
| 已做开发阶段闭环 / 当前运行代码 | 阶段60/63工程闭环保持，当前四二进制未改 |
| 新观测器实际父子进程/特权回归 | 双端通过；原始失败样本留存 |
| 同版本GridFTP新比较 | 已实际启动，1/5配对，网络门NO-GO |
| >=1800秒且>=100任务 / 常驻服务趋势 | NOT_STARTED，先决比较未通过 |
| 新双机取消/RST/恢复/坏CRC/重启/TLS现场组 | NOT_RUN，不能由历史loopback回归代替 |
| 上机说明正式验收 / production readiness | NOT_READY |
| 当前交付C材料/冻结/现场清理 | 本轮证据收口完成；不表示整个用户目标完成 |

两端新payload和partial已清理，/dev/shm使用0；2817/2818临时Grid服务、49337及46164控制转发均终止。
原2121 PID/start_ticks保持：R1 126766/1191040，R2 164834/1679976。
host pswpout保持R1 4693038、R2 2292432。E61凭据仅保留原隔离目录；私有relay token已删除。
历史数据、E62冻结154份证据及独立CPSS项目未改变；E62离线验证再次通过。

本轮只改实验harness与交付说明。阶段63相关CTest两端7/7是此前同源对象证据，本轮没有重跑并冒称新通过。
本轮执行真实双端父子进程/特权回归、11个实际实验行的证据审核和离线门禁；
最终GitHub推送回读/R2同步/Windows副本以实验根delivery64.json为准。
权威证据根 /home/Su/experiments/CPNetFlux/2026-11-12-field-trial-comparison-64。
复核：python3 此根/verify64.py。整个目标仍OPEN，不因交付C文档生成标为完成。
