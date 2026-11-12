# 阶段72：TCP序列/重传与NAPI调度服务取证

输入提交5cb3d3fe9a908bf19babacd48f323e97f9fae97c；产品运行时仍为42718ac。

## 任务范围和假设

71已经证明TX提交成批及真实RQ布局，但post间隔包含无数据，软件cur_sz非硬件剩余量，
没有序列级唯一字节覆盖或真实重传事件，故不足以决定修复。
本轮单人串行实现只读BPF：双向目标TCP序列区间/初始序列和发送端tcp_retransmit_skb；
R1 __napi_schedule/__napi_schedule_irqoff至mlx5e_napi_poll入口延迟、poll耗时、work/budget、队列包数变化。
假设：数据重传是否有明确flow/序列证据，接收活跃期是否伴随长时间已调度未执行或满预算连续工作。
没有证据前不认定TX突发、CPU/IRQ、UMR、固件是唯一根因。

先exact-env dry-run及序列合并/回绕/重叠/缺口的离线校验，再4流双向合计4096B校准（每流960B前向+64B回包）。
工具校准通过、过滤与资源/清理可验证后，只允许一次1GiB机制诊断，沿用71固定应用配置。
这不是新的吞吐候选或参数扫描，不与71按OOB数量或速度作收益对比。

## 非目标和不变量

无产品源码/协议/默认值改动；不改系统网络/ring/RSS/IRQ/RPS/THP/cache/swap/凭据/信任。
不降低native客户端NoNewPrivs，root探针从外层Su监测器启动。
不重启RDMA/微批/策略记忆，不跑8/64GiB，不提前做正式CP/GridFTP或稳定性。
保留2121原服务及无关dirty文件；原实验冻结目录不写入。
仅R1写项目根，R2接收固定提交/构建并保存自己的原始观测。

## 验收

1. 依本机BTF生成typed字段/offsetof，编译附着成功；-k报告读错误，有限时长和map预算，禁止unsafe。
   双向校准所有内容SHA、字节、四tuple及RX q7/9/27/28对应；完整SYN/序列区间无缺口，工具无丢失/读错误。
2. 唯一1GiB前双端同已提交SHA/配置/四二进制、MemAvailable>=12GiB/MemFree>=2.25GiB，
   驻留物化源与新目标、显式192.168.100.2->.1 bind和完整路由/接口计数；root资源监测包含探针和真实client/server。
   host pswpout增量0、owned进程及leaf cgroup swap0；实际cgroup无有限硬上限不冒称有限。
3. 固定4lane、256KiB DATA、1MiB chunk、CRC32C/verified_chunks/checkpoint16、queued sender/inline+mmap receiver，
   应用pacer1.5e9B/s/lane、64MiB/lane、接收请求1MiB；TLS/压缩/kernel pacing/exact-read off、commit_sync=none；不沿用70绑核。
4. TCP序列可回绕，按SYN ISN归一化并合并范围；区分唯一字节、重复提交、重传事件及失败重传err。
   NAPI间隔只在同机解释，不能冒充硬件到包时间或跨机单向延迟；捕获开销未隔离则无性能声明。
   发现覆盖不足、map饱和或监测异常，停止本轮大负载，不补跑。
5. 网络非零仍FAIL，B/C不运行；工具一致性通过不代表网络/性能/上机通过。
   清理本轮owned listeners/processes/fixtures/mounts，冻结原始小证据、离线审计、提交推送及远端SHA回读。

## 文件和证据

项目受影响文件：本范围、72结果/冻结清单、README.md、docs/README.md、docs/ROADMAP.md、
docs/DECISIONS/2026-11-12-field-trial-freeze.md。没有产品源码文件。
脚本/探针/原始证据：/home/Su/experiments/CPNetFlux/2026-11-12-tcp-napi-sequence-72。
验收命令：bpftrace exact-env --dry-run、离线序列测试、bounded calibration、一次有条件1GiB、
只读audit、CMake build及四binary SHA；相同产品源码/二进制明确复用68双端各156项回归。

## 退出及完整目标

本轮只减少机制缺口；不把观察到的关联当唯一因果，不凭数据添加新的pacer或配置扫描。
用户要求仍为：开发阶段闭环后最新五对正式CP/GridFTP、目录/反向/安全场景，随后>=1800秒且>=100任务
常驻服务和故障/取消/恢复/TLS/重启/坏数据，最后上机报告。所有项真实验收前目标继续开放。
