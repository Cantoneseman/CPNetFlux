# V3发送等待修复51：工程与可行性记录

日期2026-11-12；输入提交eea5a3de8dcc42cbeff47f33dd8ef184bc426026。
状态：R1实现及回归完成，等待固定代码提交后R2同步和唯一1GiB可行性样本；尚无新吞吐结果。
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
- R1相关回归241/241通过，95.50秒：覆盖完整持久化路径、CRC、租约、取消/期限、预算、
  中断恢复和publication；不是全仓库全部测试。
- 显式开启候选的四项真实loopback回归4/4通过：大帧传输、中断恢复、接收buffer恢复、发布恢复。
- git diff --check通过。构建为既有RelWithDebInfo配置；下一步R2同配置构建再核对三个二进制。
- 两份2026-10-14未跟踪文档保留；测试产物和探针不入Git。

## 可行性边界与下一步

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
