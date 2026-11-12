# V3 TCP exact-read 52：机制通过，网络停止门触发
日期：2026-11-12。输入3798a49f05f39f81278610c7cd3379619ae3ce6a；
运行时代码/唯一跨机任务ac2544558d71cf7fc44e7e07d57e5499871d4272。
R770-1串行开发；R770-2仅接收固定提交。

## 判定
工程实现和受影响回归完成；候选默认off。大负载每帧一次recv的机制已确认，
但唯一1GiB任务触发接收网络停止门，不是有效性能成绩，也没有吞吐收益证据。
M2仍OPEN；新8GiB正式矩阵0样本，无同提交A/B、无CV、无默认晋级。

## 改动与边界
CPNETFLUX_TCP_EXACT_READ=on仅允许plain TCP、256KiB逻辑DATA、最多4 channel。
已校验长度的大负载使用MSG_WAITALL补足循环；保留短读、EINTR、EOF、超时及shutdown取消，
64B头和小控制负载保持原路径。无新增fd/线程/队列/payload缓存；
CRC、租约、恢复、manifest和发布合同不变。TLS/原生RDMA路径不改。
新增常量统计，JSON按(channel_index,lane_index)定位实际接收端。
关闭方法为unset CPNETFLUX_TCP_EXACT_READ或值off；没有协议迁移。

32MiB本地探针三对交错：普通recv 473/353/463，WAITALL均256；
短读217/97/207→0，CRC/内容通过。墙钟/CPU未形成稳定收益，只构成机制准入。
探针RSS峰值8,294,400B，应用缓冲524,292B；没有源/目标大文件。

## 验证
- R1/R2 RelWithDebInfo构建完成、配置和三个运行二进制SHA一致。
- 两端相关CTest各254/254通过，R1 96.67s、R2 97.11s；不是全仓测试。
- R1开启候选的两项中断/receive-buffer恢复测试2/2，6.81s。
- 故障回归覆盖短读/EINTR、真实socket超时、shutdown中断、截断/坏CRC/超限帧、
  非法配置、默认off、move/close状态、真实TCP upload/download及queued/inline。
- 新机制测试在旧运行时失败后修复通过；不是以缺少API的编译错误当红灯。
- 下述channel/lane统计修正也先用实际合同复现失败，再修正；R1相关driver CTest 3/3，
  Python警告作错误语法检查和git diff --check通过。R2修正后门禁见最终delivery.json。

## 唯一1GiB任务与失败
固定四路、256KiB DATA、1MiB接收buffer请求(实际2MiB)、queued发送、
inline实验mmap接收、verified_chunks/CRC32C、TLS/压缩off、commit_sync=none。
Task51的每路1.5e9B/s应用DATA准入pacing保持不变；它不是wire packet pacing。
R770-2→R770-1，经192.168.100.2/.1显式bind、eno17095np0，不是原生verbs。

| 项目 | 原始观察与判定 |
|---|---|
| 原始完整进程时间/速率 | 0.2385353751s / 36.0111559484Gbps；仅记录耗时，不是通过网络门的性能结果 |
| exact-read | 4096次大负载/4096次recv、0短读/0 EINTR，成功读1,073,758,208B(含DATA CRC，不含头) |
| 正确性 | 下载进程exit0，目标全SHA通过，1024个verified chunks，8个range的mmap/解除映射统计可复核 |
| 源驻留 | 262144/262144页驻留，源进程swap0，seed后host pswpout增量0 |
| 资源 | receiver RSS采样峰216,604,672B、sender 14,745,600B；最低可用内存分别29,628,948,480B/29,043,777,536B，均高于12GiB |
| swap/CPU | owned swap峰0、两端host pswpout增量0；CPU采样下界receiver0.84s、sender0.77s，不是完整CPU测量或优化收益 |
| 窄接收窗口 | rx_out_of_buffer +52，rx_missed_errors当时+0 |
| 0.5s刷新尾窗 | OOB +0，missed +52；累计窗口两者均+52，网络门FAIL |
| 因果边界 | 聚合接口计数及异步刷新，不能证明归属本流/某RQ、也不能断言exact-read造成 |
| 矩阵准入 | 速率也低于40Gbps，且网络门失败；不运行8GiB、不继续扫描/重复小样本 |

首次driver在任务结束后的机制统计错误退出1：它把每channel内的lane_index误当全局索引；
实际channel_index=0..3，各自lane_index=0。原failure.json/results.json不改，
hash固定于original-sample-hashes.json。仅修正validator与测试，用既有原始证据离线复核；
没有重跑传输，也没有把离线复核写成原driver exit0。
离线复核明确检出网络门FAIL。被早期统计错误跳过的传输后source-check为NOT_RUN；
finally已清理源，不能补造该项通过。初始源哈希/驻留和目标全SHA是已有独立证据。

## 清理、交付与下一步
原始finally记录：目标slot、自有源/两端marker根删除。
现场复核两端/dev/shm均0B、49337未监听，原2121 PID/start身份保持。
代码先提交推送/核对两端后才互传；本次仅修正Python验收和文档，不改变受测运行时代码/二进制。
最终提交、GitHub SHA回读、R2同步、修正后门禁及证据hash写入证据根delivery.json。

当前不把exact-read推荐为性能配置。停止本候选的小负载/帧尺寸/rate/lane扫描，
不以一次网络事件证明因果，也不把Task51一次零事件当稳定性通过。
下一开发方向仍为TCP接收、CRC和新目标页管理之间的数据所有权/实际搬运工作量审查；
须找到区别于已失败population/direct-receive/collapse/queued-mmap的具体机制后冻结下一实现范围。
不新建通用profiler轮次，不靠增加并发、放宽网络/内存门或取消CRC达标。
原生RDMA、DEFERRED微批/策略记忆继续暂停。
宏观进度：历史M1为同提交8GiB中位34.159Gbps；本轮没有新的合格性能成绩，
40–50/70–85/100Gbps、长期稳定性、方向/TLS/磁盘/持久化门仍开放。

## 验收命令与证据
~~~bash
cmake --build build -j4
ctest --test-dir build --output-on-failure -R 'FramedDataSocketTest|SenderPacerTest|Persistent|Deadline|Timeout|Cancel|Budget|Publication|cpnetflux_large_data|cpnetflux_sender_pacing|cpnetflux_tcp_exact_read|cpnetflux_small_file_baseline_driver_test|cpnetflux_v3_.*resume|cpnetflux_publication_resume'
CPNETFLUX_TCP_EXACT_READ=on ctest --test-dir build --output-on-failure -R '^(cpnetflux_large_data_interrupted_resume_smoke|cpnetflux_large_data_receive_buffer_resume_smoke)$'
ctest --test-dir build --output-on-failure -R '^(cpnetflux_tcp_exact_read_driver_test|cpnetflux_sender_pacing_driver_test|cpnetflux_small_file_baseline_driver_test)$'
~~~
完整任务实际调用见experiment.log，只有ac254455同版本固定配置一次；
本结果不是当前HEAD自动复跑入口。证据根：
/home/Su/experiments/CPNetFlux/2026-11-12-v3-tcp-exact-read-52。
build/regression日志、红绿灯日志、build-receipts-matched.json、one-gib原始receipts、
one-gib/offline-review.json和cleanup-live.json均在该根。原始大payload已清理，不入Git。
