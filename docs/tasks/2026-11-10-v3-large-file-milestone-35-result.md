# Milestone35：大负载DATA与有界TCP接收缓冲结果

日期2026-11-10。输入6c1533525c991a8523fa1e872f3671588c740d46。
R770-1唯一写入，R770-2只接收固定提交；未用子代理、worktree或桌面。

## 结果与范围

交付2、3已经实现并完成各自同提交五对1GiB完整任务对照；交付4完成受测范围集成/恢复冻结，
正式8GiB M1仍为BLOCKED_ENV。新功能保留显式选项，未改变默认DATA大小、I/O、TLS或持久化策略。

| 独立同提交比较 | 对照中位Gbps | 候选中位Gbps | 中位任务速率变化 | 配对时间改善 | 候选T_ready CV |
|---|---:|---:|---:|---:|---:|
| 73d77c9：65532B→262144B逻辑DATA | 27.331 | 35.821 | +31.06% | 5/5 | 5.176% |
| 8d833b7：固定262144B，接收buffer off→1MiB请求 | 34.111 | 36.952 | +8.33% | 4/5 | 4.714% |

两组基线、提交不同，不能把两个百分数相加或连乘声明累计收益；36.952Gbps是第二组
组合候选的1GiB结果。它不是8GiB、磁盘持久化、TLS、原生RDMA、生产或100Gbps成绩。
旧scope17的27.291Gbps/8GiB保持原结论，不与本轮小负载直接算增益。

合同：R770-2真实驻留tmpfs源→R770-1全新tmpfs目标；4 TCP lanes、8个128MiB ranges，
queued发送/inline实验mmap接收、CRC32C/verified_chunks、1MiB chunk、checkpoint16、
TLS/压缩off、commit_sync=none。T_ready由创建客户端到终态退出计时，包含初始化/发布/收尾。
外部SHA在计时外，两臂相同；每臂5次，交错顺序AB/BA/AB/BA/AB，全样本保留，无补样。

## 机制与可用接口

`--data-frame-bytes 262144`仅用于V3 TCP、控制/数据TLS off、compression off、CRC32C、1MiB chunk。
默认65532B，DATA wire上限分别65536/262148B；控制帧仍65536B、FILE_BEGIN8192B。
XCPNETFLUX V3协商必须确认data_bytes和max_data_bytes；旧peer不支持时payload前拒绝，
每次重连重新协商，数据socket限制固定。接收前先验证类型/长度，保持range/attempt/missing-mask边界。
一个1MiB chunk实际从17帧变4帧；1GiB从17408变4096，减少76.47%逐帧工作。
接收进程CPU采样下界中位1.01→0.85s/GiB，RSS采样峰值232.8→244.8MiB；
CPU为10ms采样下界，不能当作精确CPU/GiB或等同墙钟降幅。
总用户payload所有权4*(depth2+6)*262148=8388736B，约8.0001MiB，预算64MiB不变；
映射仍64MiB/lane、并发总256MiB，实际映射生命周期/写次数均验证。

第二机制根据现存ss中的小接收窗口和rwnd_limited背压选择，未新增通用热点审计。
`CPNETFLUX_TCP_RECEIVE_BUFFER_BYTES=1048576`只设置plain TCP数据接收socket；
最多4 lanes，单socket实际SO_RCVBUF读回2MiB，总kernel buffer上限8MiB，独立于用户payload预算。
默认off不设置SO_RCVBUF，保留内核autotuning；实测入口原值131072B。
lane_endpoints报告实际读回值。非支持值、TLS/RDMA组合或超过4 lanes明确拒绝。
设置在接收入口、payload前；不改控制socket、sysctl、网卡或系统服务。
Linux窗口缩放是在建连时确定的，本候选在已建连socket设置，不能声称重新协商窗口缩放或消除背压。
第二组CPU采样中位0.86→0.82s/GiB，RSS峰值244.4→246.2MiB。

## 正确性、资源与失败门

R1/R2原型各150项相关CTest通过；buffer机制各152项通过。
冻结R1相关153/153（21.41s），含大帧+buffer双向中断恢复；另14/14兼容回归（27.64s），
包括树上传下载/并行/恢复、receiver policy、service restart、timeout、坏checkpoint和冲突幂等。
最终R2冻结结果见freeze-tests-r2.log与冻结回执；没有在此预先声明未读取结果。
相关构建均RelWithDebInfo、RDMA/TLS/ISA-L/io_uring=ON，三运行程序二进制SHA两端一致后才互传。

覆盖新/旧大小、双向queued/inline、小尾帧、空文件、实际帧数/内容SHA、能力缺失/不支持组合、
控制与DATA分类型限额、未协商DATA、截断、缺CRC、跨chunk、坏CRC不认证/不发布、
payload预算和shared stage形状、512MiB中断后仅跳过已认证chunk、最终发布及已有TLS回归。
首次红灯：旧程序504拒绝大帧；buffer红灯为旧程序缺少容量字段。
开发中修复测试登录/显式worker配置、下载whole-file帧数统计选择及socket释放后的统计时机；
首次测试失败不冒称软件性能失败，所有正式性能样本保留。

两组20次均源驻留、SHA和1024条verified chunks通过；owned swap0、host swap-out增量0，
MemAvailable≥12GiB。每次输出清理，批次结束两端源与payload根清理，原生产服务PID/start_ticks未变。
准备源的实际进程PID1285270/session-26037.scope及祖先memory.max/high均max；
写/SHA过程中scope swap0，1GiB材料化驻留正常。只解释本次资源归属，不证明旧8GiB换出根因已解决。

大帧r2/r5的rx_missed_errors分别+206/+367；buffer候选r1为+238，其余对应任务无新增。
这些是接口范围计数，尚无流级因果归属；不能归因于候选缺陷或宣称网络稳定通过。
历史scope17的+94仍开放。第一组候选CV>5%，第二组对照CV5.354%也需明确，
不能仅凭候选CV4.714%宣称全场景稳定。收益证据支持保留显式实验，不支持默认晋级。

## 冻结、回退与后续

- 保留：显式256KiB DATA与显式有界TCP接收buffer；默认不启用。
- 回退：去掉--data-frame-bytes、取消buffer环境变量（或off），保持V3和完整性语义。
- 8GiB M1：BLOCKED_ENV；scope34受载驻留/换出问题没有新充分条件，不重复同前提8GiB实验。
- 下一结构性任务：先定位接口rx_missed计数与CPU/IRQ/队列服务关系及可控资源窗口，
  在不改共享网络策略前保留证据；受控驻留条件恢复后做同提交8GiB五对正式门，
  不以1GiB成绩替代。再针对大帧后的剩余页分配/搬运关键路径设计一种机制，向40–50Gbps推进。
- RDMA评估：TCP大帧证明逐帧工作量值得降低，但不能直接放大verbs receive。
  重启前须完成capability/MTU与帧分段映射、注册池复用和预算、CQ批量完成、
  inflight借用/flush/peer mapping故障恢复，并接入同一V3文件提交/恢复合同。
  100G raw verbs上限需与完整文件T_ready分开测。RDMA运行时优化仍暂停，
  建议在TCP受控大文件/网络事件闭环后以独立目标重启；当前收益不证明已需要或已达到100G。

## 证据与复现

证据根两端`/home/Su/experiments/CPNetFlux/2026-11-10-v3-large-file-milestone-35`。
原型`data-ab/{analysis,results,matching-builds,server}.json`；buffer对应`receive-buffer-ab`；
源准备进程/cgroup/驻留证据在R2各批次peer；每任务receipt、压缩summary/log在R1 samples。
`prototype-analysis.json`、`receive-buffer-analysis.json`汇总保留接口事件和资源范围。

```bash
# 先核对两端固定输入、同构建与SHA；payload-root必须未存在
python3 tools/perf/run_large_data_ab.py --action matrix \
  --evidence /home/Su/experiments/CPNetFlux/2026-11-10-v3-large-file-milestone-35/data-ab \
  --payload-root /dev/shm/cpnetflux-s0-large-data35 \
  --commit 73d77c9050c1eae96141d3a46856da20e256acdb
python3 tools/perf/run_large_data_ab.py --action matrix --mechanism receive-buffer \
  --evidence /home/Su/experiments/CPNetFlux/2026-11-10-v3-large-file-milestone-35/receive-buffer-ab \
  --payload-root /dev/shm/cpnetflux-s0-receive-buffer35 \
  --commit 8d833b72a33733ad031ea68ac52d7e720f3a86bd
```

以上为已执行命令记录，复现应使用新的证据目录，不能覆盖已有证据或重复复用payload目录。
最终提交/推送SHA回读、两端冻结哈希及清理回执保存于证据根freeze-backup/closure，不写入凭据或payload。
