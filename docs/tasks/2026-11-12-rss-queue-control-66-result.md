# 阶段66：接收队列聚集的受控隔离实验

结论：**RAW TCP QUEUE CONCENTRATION REPRODUCED / CP RUNTIME FIX OPEN**。在不经过CPNetFlux文件读写、CRC或三阶段处理的原始TCP负载中，将四条流集中到R1同一RX队列，可复现OOB/missed；分散到四个队列的短负载未触发这两项计数。下一步优先验证软件连接分布，不能继续仅按lane数量和平均吞吐配置并发。

这不是完整CPNetFlux性能样本，未修复运行时、未重启正式比较、未运行长期稳定性。阶段64仍NO-GO（1/5有效配对），上机NOT_READY，M2未晋级。阶段65观测机制保持，原生RDMA/微批/策略记忆仍暂停。

## 固定输入与唯一变化

CP输入 `8862b7b69c6547e6140e7a02715c5d60ca33e1ba`。诊断初始提交 `97e2de6054fe5a6e9e615ca85e0a57981b4bcc0c`；停止后统计oracle修正 `b1e820a24cb87c356b3787e9e8debc0c4cde0696`，两次均推送并回读，跨机前两端固定对应提交、同配置构建、四个CP二进制hash一致。src/include/tests/CMake运行时树保持阶段65；没有新增产品配置或修改传输热路径。

两端iperf3=3.20，二进制及实际依赖库SHA一致；脚本/RSS快照hash一致。四个独立P1进程，R2显式bind192.168.100.2，R1显式bind192.168.100.1:49370..49373。每流目标128MiB、12Gbit/s、256KiB buffer、interval0.1s，未使用zerocopy、CPU affinity、内核pacing或文件输入输出；仅变更源端口及由此决定的RX队列。没有改RSS/ring/IRQ、网络、cache、THP或swap。

| 组 | 源端口（依次对应49370..49373） | 预测且由主要RX计数验证的队列 | R1 OOB/missed增量 | 状态 |
|---|---|---|---|---|
| 分散 | 30026、30030、30001、30014 | 9、27、7、28 | 0 / 0 | 四流完成；原发送字节相等oracle失败保留 |
| 集中 | 30050、30011、30004、30021 | 9、9、9、9 | 310 / 310 | 监控停止，失败保留，不追加样本 |

分散组每个目标队列约104k个包，接收应用总600,571,904B，每流11.958–11.981Gbps，仅用于核对施加负载；R1接口rx_bytes增量628,354,964B。集中组队列9为141,957个包，占全部RX包99.97%以上；接口rx_bytes增量214,905,326B，未完成目标负载即停止。集中组不提供吞吐成绩，不能与分散组计算性能收益。两个组在R1分别有6/5个采样时刻四条数据连接同时established，R2分别8/7个；短时观察不能替代稳态/CV或多次因果复核。

两端host pswpout、owned swap均无增量；四进程合计观测RSS峰值约20.4MiB，远低512MiB限额。queue9的buffer allocation/cqe/wqe错误计数没有新增；集中组page-pool recycle/refill工作有增量，但不据此宣称内存分配失败。原始TCP路径已经足以出现故障，不需要先发生CP文件写入或CRC开销。

## 统计门失败与复核

loopback校准前两次因参数限制失败：发送端不接受接收超时参数，按字节运行的duration0又被server-max-duration拒绝；原输出均保留。固定小流端口在清理后的TIME_WAIT阶段不能裸bind，校准改用OS选定的两个空闲端口；最终64KiB收发一致、实际cport吻合，临时服务清理。跨机两组不重跑。

分散组R2原脚本报告 `AssertionError()`，原JSON/脚本/版本/失败不修改：其中一条sender accepted=150,208,512B、receiver delivered=149,946,368B，差262,144B。另三条一致；客户端记录的receiver字节与服务端实际接收统计逐流一致，网络门两端为0。

[iperf3 3.20服务端源码](https://raw.githubusercontent.com/esnet/iperf/3.20/src/iperf_server_api.c)在TEST_END时停止、统计并关闭数据socket，不能把sender accepted等同于receiver delivered。这个行为支持“结束统计边界不同”的解释，不能仅凭源码断言差值每个字节的具体去向。[官方用法](https://software.es.net/iperf/invoking.html)也将它定位为网络负载工具，不能当成完整文件校验工具。

先补测试观察旧oracle失败，再修正为分别记录sent/received/gap且complete_file_validation=false；目标接收量、真实端口、负载重叠、资源/网络/预算门保持。双端6项离线测试通过。按书面停止后复核，只继续原计划剩余的一次集中组，没有重跑分散组。两版run/quick_network/identity/connected_count函数AST相同，改动只涉及结束后的解析与测试，实际发包命令/监控循环未改变；CP/GridFTP正式内容SHA门没有放宽。

集中组R1检测missed后终止4个server，R2随之退出1；全部原输出保留。R2错误退出JSON中出现不可信的巨大end seconds，故丢弃其速率和时长，不当作任务完成或性能。最终settle后OOB和missed各+310，无后续增量；不继续本候选负载或调参。

## 证据、验收与清理

证据位于 `/home/Su/experiments/CPNetFlux/2026-11-12-rss-queue-control-66`。`analysis66.json`是独立复核，原报告在spread/concentrated及对应report文件中；`run66-original.py`保留初始oracle，receipt分别固定两组实际输入，原停止不是“改成成功”。R2归档peer66.zip SHA-256为 `b9e9188f935600bb34aae0a3ae89336b60af264e79daea97f57810047baa6f43`；无凭据或数据payload。

```sh
python3 /home/Su/experiments/CPNetFlux/2026-11-12-rss-queue-control-66/run66.py selftest
python3 /home/Su/experiments/CPNetFlux/2026-11-12-rss-queue-control-66/verify66.py
```

verify66离线验证冻结hash、两版/双端pin、队列预测/真实端口/计数、并发采样、资源、原失败及中止边界，不发送负载。没有产品运行时改动，不重复上一阶段的完整CTest；双端6项诊断脚本测试通过，四个CP目标同配置构建成功且hash保持阶段65。原2121服务身份不变，临时iperf进程和49370..49374 listener均无残留，没有落地大payload；本次两组R1接口总接收843,260,290B，低于原定4GiB网络预算。

## 对根因与下一步的影响

现在有证据支持“接收队列聚集是一个可独立触发同类故障的条件”，比阶段64仅凭包数推测更具体。但原失败样本缺少四元组，且当时疑似2流聚集，本次是4流聚集、单个短对照，因此还不能宣称旧CP样本因果已逐一证明，或分散后永远不丢包。

下一阶段先设计并实现有界、可观测的TCP数据连接源端口选择/布局能力，保留默认语义，避免将每lane速率预算误认为每RX队列预算。需先用小流证明实际四元组与预期布局一致，并覆盖端口占用、TIME_WAIT、取消/重连、多任务复用和不支持路径；不能固定四个端口后忽略长期复用与恢复。只在候选工程门通过后，用固定CP配置的有限完整任务验证内容、性能、网络及资源门，不能以raw iperf结果直接晋级。

软件修复及完整任务网络门通过，才串行续接“配置和实验”重新完成正式GridFTP配对；比较后做至少1800秒且至少100任务稳定性、恢复/取消/TLS与资源趋势，最后给上机结论。8GiB物理空闲内存门仍未准入，本轮未分配8GiB文件或恢复M2扫描。
