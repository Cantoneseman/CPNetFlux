# V3发送等待修复51：工程与可行性记录

日期2026-11-12；输入提交eea5a3de8dcc42cbeff47f33dd8ef184bc426026。
状态：ENGINEERING PASS / M2 NOT ADMITTED。代码修复、双端验收及唯一1GiB可行性样本完成；
36.478Gbps未达40Gbps准入，不运行新8GiB正式矩阵。
本轮不改默认，不恢复原生RDMA、不启动DEFERRED工作单元/策略记忆。

## 定位与修复

Task50中每lane规定跨度178.829ms，实际238.996–291.797ms，不能直接归因为某项等待。
独立无payload探针2048次准入：相同规定跨度357831976ns，std_sleep_for实际472595117ns，
timerfd实际375977628ns；CPU10.503→12.555ms。只证明等待机制成本，不是整任务收益。
探针原始文件在证据根wait-probe.cpp/json，不作跨提交性能对照。

修复为每lane懒创建一个CLOCK_MONOTONIC/TFD_CLOEXEC阻塞timerfd；无新增线程/payload缓存，
四lane最多四个fd。off及首帧不创建fd；UniqueFd析构归还。
等待请求仍≤1ms；发送间隔、无追赶规则、取消/总deadline检查保留。
create/arm/read错误回传，EINTR时检查取消后继续原timer等待，不多提交。
JSON receipt新增wait_backend及create/arm/read计数；实机driver验证每lane恰好一个fd、
实际arm/read且所有DATA帧被限速。不给系统/线程设置timer slack，不改共享网络。

## 已执行验收

- 新增六项C++回归在旧实现上按预期失败：fd生命周期、耗尽、取消、等待Status失败、
  实际arm/read系统错误和EINTR取消。新增Python机制门有五个负例先失败。
- 修复后直接相关CTest 31/31通过。
- 两端相关回归各241/241通过，R1 95.50秒、R2 96.03秒：覆盖完整持久化路径、CRC、租约、取消/期限、预算、
  中断恢复和publication；不是全仓库全部测试。
- 显式开启候选的四项真实loopback回归4/4通过：大帧传输、中断恢复、接收buffer恢复、发布恢复。
- git diff --check通过。构建为既有RelWithDebInfo配置；两端同提交、同配置及三个二进制SHA已在互传前核对。
- 两份2026-10-14未跟踪文档保留；测试产物和探针不入Git。

## 可行性合同

固定提交、双端构建/配置/SHA一致后，只运行唯一1GiB四路TCP样本；
驻留tmpfs源、全新tmpfs目标、256KiB DATA、原接收buffer/CRC/verified_chunks/非持久化/TLS off。
MemAvailable≥12GiB、owned swap=0、host pswpout增量=0、显式bind和网络停止门不变。
退出后移除marker自有payload、停止49337临时服务并核对原2121进程身份。
不把跨提交单次差异叫作优化收益，旧M1 34.159Gbps保持历史成绩。
≥40Gbps只是未来正式矩阵的准入；未达则不启动8GiB五对，不扫描rate/lane/frame/wait参数。

证据根：/home/Su/experiments/CPNetFlux/2026-11-12-v3-pacing-wait-51。
复验命令：

~~~bash
cmake --build build -j4
ctest --test-dir build --output-on-failure -R 'FramedDataSocketTest|SenderPacerTest|Persistent|Deadline|Timeout|Cancel|Budget|Publication|cpnetflux_large_data|cpnetflux_sender_pacing|cpnetflux_v3_.*resume|cpnetflux_publication_resume'
CPNETFLUX_SENDER_PACING_BYTES_PER_SECOND=1500000000 ctest --test-dir build --output-on-failure -R '^(cpnetflux_large_data_smoke|cpnetflux_large_data_interrupted_resume_smoke|cpnetflux_large_data_receive_buffer_resume_smoke|cpnetflux_publication_resume_smoke)$'
~~~

## 唯一实机样本与完整验收

实测输入52acbeb9a4142f7c89cc22678a80778795a66246；GitHub push及SHA回读一致，R2 fast-forward后构建。
两端运行二进制SHA-256：
- server：87b50d531c49c887b4b74a3359b7ac8da87948984ab6c2d3d169ccbbc8a34831
- download：2fbffa89b40e20f05ad023b9f12255bfecdc8f582bcb411fcea71f022247f275
- upload：db738c355abbecfcf8caf047bb08e3694b7bc3c875e52d821447df13fcbd888b

| 项目 | 结果 |
|---|---|
| 实际成功有效字节 | 1073741824（1GiB），单文件，8 ranges |
| 完整T_ready / task_rate | 0.235480193980秒 / 36.478374027Gbps |
| 完整性 | 独立源/目标SHA一致、1024 verified chunks、V3终态成功 |
| 帧/等待机制 | 四lane各1024帧，4096帧；各创建一个timerfd，arm/read合计各3879次 |
| 规定间隔 / 实际准入跨度 | 每lane178.828584ms；lane0/1/2/3为217.411221/213.003823/210.060528/222.417160ms |
| 资源 | 接收最低MemAvailable29631787008B；发送服务29005144064B；owned swap=0，两端整轮host pswpout增量=0 |
| CPU采样下界 | 接收0.84、发送服务0.77 CPU秒/GiB；不是精确完整CPU开销或同提交改善率 |
| 源驻留 | 262144/262144页；源准备也通过内存/换出门 |
| 网络 | 窄窗口、settled窗口、整任务及R2服务窗口OOB/missed增量0；不证明历史OOB归因或稳定性 |
| 清理 | 两端marker自有root移除、/dev/shm均0B、49337不再监听；原2121 PID/start身份保持 |

接收RSS采样峰值267132928B，发送服务14761984B；RSS不包含全部tmpfs文件容量。
源/目标SHA为a3bae2a374729a3b7959246e62cbc1a0cfc212bd4cd15992d718bea8513994f6。
wait_ns四lane之和358389957ns含并行重叠，不能当作可消除墙钟。
Task50的28.221Gbps、历史M1的34.159Gbps与本次提交/规模/源字节不同，不能相减宣布修复收益；
本次没有同提交对照、重复样本、CV或正式性能晋级。默认继续off，不推荐新的性能默认配置。

原始one-gib/analysis.json、results.json、pacing-receipts.json、源准备、两端资源/路由/接口和cleanup均保留。
peer-validation保存R2构建/测试/源准备/现场收口文件，无payload。
audit_acceptance.py从原始成功字节复算速率、验证同提交二进制、receipt、资源/网络及清理；
acceptance.json记录输入hash及ENGINEERING_PASS_M2_NOT_ADMITTED，不将未运行项记为pass。
原始日志不修改。下一次复现必须使用新证据目录和新marker根，不覆盖本轮结果。

~~~bash
python3 tools/perf/run_sender_pacing.py --evidence /home/Su/experiments/CPNetFlux/2026-11-12-v3-pacing-wait-51/one-gib --payload-root /dev/shm/cpnetflux-s0-wait51-52acbeb9 --commit 52acbeb9a4142f7c89cc22678a80778795a66246
python3 /home/Su/experiments/CPNetFlux/2026-11-12-v3-pacing-wait-51/audit_acceptance.py
~~~

## 对照路线后的下一设计边界

本轮已解决普通sleep造成的多余等待机制，并没有解决全部TCP大文件工作量。
以本次1GiB计，40Gbps需要T_ready≤0.214748365秒，比当前少20.732ms（约8.80%）；
这是验收缺口而不是已找到的优化预算，不把并行CPU样本、lane跨度差额或等待总和算作墙钟收益。
pacer固定四lane总准入上限48Gbps，保留其诊断意义，不靠提高rate/加lane继续追100G。

下一步仍在TCP主线，先针对接收端页面管理与数据搬运检查具体可减工作：
核对当前256KiB DATA下页缓存写入与mmap写入、已有BufferedFileWriter/rangeWriteBatch路径及所有权边界。
旧聚合写已能减少syscall，不能把改一个batch数包装为新机制；
需要区分用户VMA首次触页、实际页面分配/清零和额外用户态copy，并把准备计入完整任务。
已失败population/direct-receive/seed+collapse、queued mmap、source-mmap和仅CPU收益的MSG_ZEROCOPY候选不原样重跑。
本轮没有据此修改接收backend、增加观测器、跑新矩阵或承诺收益；缺少可减工作时如实记录，不能自动扩大实验。

中长期原生RDMA需重审MR预算和buffer所有权，其旧四连接池约8.219GiB/endpoint，
不是直接开关；继续按用户决定暂停。小文件微批和DEFERRED统一工作单元不替代大文件M2。
当前总体进度：三阶段及可靠性工程完成、历史M1保留；等待修复完成；M2、网络稳定性与100G目标仍开放。
