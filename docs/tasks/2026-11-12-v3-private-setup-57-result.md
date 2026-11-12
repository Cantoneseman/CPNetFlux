# Task57 / 交付C：必要准备优化、单次40Gbps准入与冻结

日期：2026-11-12。状态：ENGINEERING COMPLETE / FEASIBILITY ADMITTED / M2 OPEN。
本轮完成Task57的版本、配置、回归、原始证据与清理冻结；唯一1GiB完整目标任务为
**40.865345Gbps**，通过单次可行性准入。正式8GiB M2矩阵尚未运行，不称稳定40Gbps。
合同见[Task57开发与单样本合同](2026-11-12-v3-private-setup-57.md)，
精确版本、配置和证据SHA见[冻结清单](2026-11-12-v3-private-setup-57-freeze.json)。

## 代码与机制

输入aa2ed81d45bd1f8d7b0f98a51e8772715b9e1182。两个实现阶段均在互传前完成
提交、GitHub推送/SHA回读、R770-2同步构建及二进制核对：

- 740396b2ebdce92023d439f6cc3cb0fe5b6bf358：增加独立Linux原生准备监督器
  cpnetflux-private-tmpfs-launcher，显式--native-setup且仍要求--enable-private-tmpfs。
  默认保留旧Python准备；新路径直接做身份/路径/inode/已有挂载/evidence检查、
  私有namespace和1280MiB tmpfs挂载，保留实际mountinfo证明。
- 118211fe89e1cd36d5f2942a9386c8f698da4f6b：修复结构化回执和控制端计时在合并
  stdout/stderr时的并发混写。共享锁覆盖参与的完整记录写入和flush，保留原stream归属；
  pacing/trace仍stderr，timing/sender-work仍stdout。协议字段、控制逻辑与速率配置不变。
  这是观测正确性修复，不宣称它带来吞吐收益；不保证任意第三方错误输出都不会插入。

Su父进程在目标根mkdir之前启动计时。root监督器只负责限定准备、降权与回收，
不执行payload/SSH/SHA；子进程真实/有效/saved uid/gid均为1002、附加组为空、
NoNewPrivs=1，只运行固定脚本，不接受任意命令，不设置setuid或常驻daemon。
正常/失败/取消/超时都回收自有后代后卸载；内层TERM grace 30秒和最多5秒回收，
给原外层45秒强杀留余量，禁止lazy umount掩盖活跃进程。所有必要检查仍计时。

五对最终setup-only对照的旧准备中位53.395822ms、native中位7.617807ms；
逐对节省均为正，配对节省中位44.378176ms，超过预先28ms机制门。
这只证明必要后端启动工作减少，不是每字节传输CPU或网络数据通路的加速。
该五对输入为740396b；后续日志修复没有改变准备源码/CMake及helper二进制，
离线验证逐项核对后复用机制证据。早期timeout等待放大的启动审计和防护补齐前结果
均保留，但不作为最终门的替代。

## 回归与原始失败

两端固定118211f后，相关CMake构建成功、CTest集合各**261/261通过**；
范围覆盖帧、V3大负载、预算/期限/取消、persistent、恢复/发布、pacing、private tmpfs。
这不是全仓库全部测试。实际native生命周期测试两端均运行，没有按条件跳过。

每端另执行kernel pacing真实本机四路V3 upload/download smoke连续10次，全部通过。
新增并发日志单测以8线程共128条32KiB记录验证合并stream后的完整记录集合；
既有要求stderr的pacer接口单测保持不变并通过。

实际native验收包含setup-only、64MiB probe、预期失败、超时、内部取消、
逃逸进程组后代、128MiB本机V3、外部取消，共8种生命周期；
8个危险参数拒绝、symlink回执拒绝及私有namespace内既有挂载拒绝均通过。
身份、NoNewPrivs、后代回收、卸载、host状态及payload删除证据保留在双端bundle。
预期失败/超时/内部取消的remaining_names在卸载前如实记录probe.bin；
该文件随私有tmpfs卸载销毁，host-after确认最终根目录移除，不能写成卸载前已经为空。

原始失败没有覆盖：
R2首次相关10/11，kernel pacing JSON在字符131被timing记录插入；
有界本机复现第4次保留了坏行。首次将输出全部改为stdout后，R1为259/260，
旧stderr兼容测试失败；最终使用共享输出锁且保留stream后才得到两端261/261。
driver选路与已有挂载拒绝也保留先失败后修复记录。
最终验收不以这些早期失败输入代替118211f回归。

## 唯一跨机完整任务

R770-2→R770-1，192.168.100.2/.1显式bind，接口eno17095np0，四路V3 TCP。
源1GiB全页驻留；目标全新私有tmpfs，1280MiB上限、huge=within_size；
256KiB有效DATA+4B CRC，1MiB chunk，CRC32C/verified_chunks，checkpoint16，
commit_sync=none。queued sender/inline实验mmap receiver，64MiB/lane映射窗口、
总映射256MiB、payload预算64MiB，SO_RCVBUF请求1MiB、实际每路2MiB。
内核TCP ceiling每路2e9B/s，四路配置合计上限64Gbps，只是配置回读，不是精确整形。
TLS、压缩、应用pacer和exact-read关闭；详细receiver/range profiler关闭，
仍有10ms资源保护及低频ss观测开销。当前ceiling配置也不能用来验收100Gbps。

| 计时/验收项 | 原始结果 |
|---|---|
| 完整目标任务 | **0.210200957s / 40.865344828Gbps** |
| client创建至成功退出 | 0.201789698s / 42.568746933Gbps，仅子窗口 |
| 必要私有准备 | 0.007740488s，计入完整任务 |
| 目标目录创建 | 0.000670771s，计入完整任务 |
| 完整性 | 源/目标独立SHA树一致，传后源校验通过，1024 verified chunks |
| 源驻留 | 262144/262144个4KiB页驻留 |
| 目标页型 | 13次实际目标VMA采样，ShmemPmdMapped峰值194MiB，不是全文件覆盖率 |
| 两端最低MemAvailable | 接收29491879936B、发送29000687616B，均≥12GiB |
| 换页 | 自有负载swap=0，两端host pswpout增量0；不包含旧服务既存swap |
| 网络 | 接收窄窗/settled/整体OOB和missed增量0，发送服务窗口missed增量0 |
| 清理 | 源/目标及自有路径已移除、进程回收、私有挂载已卸载，host状态未变 |
| 准入与M2 | 单次可行性formal_admission=true；m2_pass=false；新正式8GiB样本0 |

完整目标任务分母=从mkdir前起的必要namespace/准备+目标目录创建+client创建至成功退出；
源驻留准备、外部SHA/观测、清理分别记录，与Task56合同相同，没有提前触页或移走必要工作。
1GiB数据由8MiB随机块重复128次并完整物化，压缩关闭；
SHA树为ffbea30bb56d5168183f9ff356ed6cfc2da3e80a963dfa002b354491fd3510dd。

analysis.json是子进程阶段输出，其final_admission_pending_external_unmount=true仅记录
当时等待外层卸载；最终namespace/host-after及acceptance.json已经确认清理并完成准入。
不能把这个中间字段误读为最终清理仍未完成。

40Gbps预算为214.748365ms，本次余量只有4.547408ms（约2.12%时间预算）；
单次结果没有A/B、CV、持续稳定性或历史OOB逐流/RQ因果证据。
不得跨Task56/历史M1不同提交或负载计算收益，不将零事件等同根因关闭。
本成绩只适用于上述TCP/plain/内存文件后端，不代表默认、磁盘持久化、TLS或原生RDMA性能。

## 交付C与回退

冻结清单固定源目录对象、构建配置、四个二进制SHA、测试及原始证据SHA。
离线验收命令（不产生新payload）：

    python3 /home/Su/experiments/CPNetFlux/2026-11-12-v3-private-setup-57/verify-delivery.py

关闭--native-setup即恢复原Python准备；不传--enable-private-tmpfs不使用私有实验后端；
关闭--kernel-tcp-pacing或不设其环境变量即退出内核ceiling候选。
日志正确性修复保留，默认传输协议、缓冲预算与生产选项没有晋级。
若需源码恢复，先保留当前修改，再从明确GitHub备份恢复指定文件，不删除实验或无关项目。

收口现场两端/dev/shm使用0B、自有cpnetflux-s0路径为空、49337无监听；
原2121仍为R1 PID126766/start1191040、R2 PID164834/start1679976。
NIC/RSS/IRQ/qdisc/MTU、全局mount/THP未改；两份既存未跟踪Task01b文档保留。
最终文档提交、GitHub SHA回读与R2同步记录在实验根delivery.json，
验收在delivery-acceptance.json；304个原始证据SHA与194个本地链接通过离线门，避免冻结清单自引用最终提交SHA。
Windows只复制小报告、清单及回执到_codex_stage57，不覆盖源码树。

## 后续方向与未完成项

1. 下一开发项转为正式M2运行能力与合同：当前driver固定1GiB、私有挂载固定1280MiB，
   必须先增加有界8GiB支持、全持有者内存核算和失败清理测试，再固定同提交对照/候选、
   必要准备分母及恢复门；不能直接把现有脚本参数改大就开跑。
2. 沿用[阶段验收合同](2026-11-11-performance-stage-closeout-handoff.md)的8GiB至少五对交错，
   候选中位≥40Gbps、CV≤5%、至少4/5改善及正确性/资源/网络/恢复全门。
   两端同提交/构建/四二进制SHA匹配、源驻留、MemAvailable≥12GiB、
   owned swap=0及host pswpout增量0；任一硬门失败停止，保留失败样本。
   本轮不自动开始该矩阵，不追加当前1GiB或rate/lane/frame扫描。
3. 正式结果若不足，依据完整任务的新成本证据选择TCP搬运/接收页管理的一个结构性机制；
   不原样重跑已关闭的population/direct-receive/queued-mmap候选。
   原生RDMA、微批、统一工作单元/策略记忆继续暂停。

历史已验收M1仍为506b63b的五对8GiB组合中位34.159410Gbps。
本轮C完成工程交付与单次准入证据冻结；40–50正式M2、70–85/100Gbps、
网络长期稳定性和生产验收仍未完成。
