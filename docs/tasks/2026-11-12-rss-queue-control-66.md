# 阶段66：不改CPNetFlux运行时的RX队列分布对照

范围：在固定阶段65代码基础上，以系统现有iperf3 3.20作独立TCP诊断，验证“4条同速流集中到1个RX队列是否足以触发OOB，而分布到4个队列不会”。这是网卡/内核的隔离对照，不是CPNetFlux完整文件成绩，不代替GridFTP比较、稳定性或上机验收。

输入：8862b7b69c6547e6140e7a02715c5d60ca33e1ba。根目录只由当前总指挥串行写入，无子代理。先验证实际RSS未变，复用阶段65已验证预测器；通过每个iperf进程独立的服务/源端口控制四元组。不改RSS、ring、IRQ、网络、cache、THP/swap；不加产品knob，不改传输默认/性能配置，不启动RDMA或新M2大负载。

受影响：本计划/结果/冻结与README导航；证据脚本与数据只在 /home/Su/experiments/CPNetFlux/2026-11-12-rss-queue-control-66。源码、协议、产品测试和构建配置保持。所有修改先记录范围；试验前固定提交、推送、两端同提交、同配置构建和四个CP二进制SHA，额外核对iperf二进制及实际共享库标识、诊断脚本hash。

执行：先1条loopback小流校准--cport与3.20的按字节停止/JSON解析，要求上界和端口真实吻合。跨机最多2组、各4个独立单流进程：先分散到已验证队列[9,27,7,28]，通过后才允许集中到队列9。R2显式bind192.168.100.2发往R1显式bind192.168.100.1:49370..49373。每流目标128MiB、12Gbit/s、256KiB缓冲、interval0.1s、P1、无zerocopy/affinity/kernel pacing；两个组除源端口及由此决定的RX队列分布外配置相同。iperf启动/调度与CP的pacer并不相同，不外推CP根因已证明。

iperf3 3.20的-n可能在统计间隔末尾停止，不能把128MiB当作严格wire上限。单组网络字节警戒1GiB，观察进程发现后立即停止本组，绝对上界按2GiB计（观察/退出有延迟）；两组总不超过4GiB网络流量预算，实际记录收发bytes，不落地payload。每端诊断所有进程RSS合计<=512MiB，单进程<=128MiB；MemAvailable>=12GiB，owned swap=0，host pswpout无增量。不分配1GiB源或目标文件，8GiB任务资源门仍关闭。

任一网络/HCA/资源/端口/版本/JSON门失败立即停止当前组及剩余组，保留失败，不重试。观察前、中、后和settle的net/ethtool/HCA、hostswap、各进程RSS/FD/thread、软中断与IRQ计数、真实四元组、每队列packet/byte，保留CPU/NAPI观测适用边界。接收有效负载覆盖的共同时间必须可确认，分散组主队列与预测一致，集中组主要packet归属队列9；目标速率未实现或流不同步时标诊断不足，不能按不丢包证明反面。

验收：loopback实际port/byte校准；双端原始JSON端点/收发字节和预测一致、观测完整、主RX队列有效分布且有重叠负载；相关脚本离线测试通过；原2121服务身份保持，临时4个listener全清理，无新payload/凭据。只据实际证据收窄或否定假设，不从一个短对照宣称稳定性。结果冻结hash、明确下一步，提交并推送本轮文件、回读远端SHA。

未完成目标仍为：解释并修复网络失败→固定版本GridFTP正式配对→至少1800秒且至少100任务稳定性/恢复/取消/TLS及资源趋势→上机候选。阶段64 NO-GO不被本诊断覆盖。

脚本备份补充（跨机前）：新增 tools/perf/diagnostics/cpnetflux_rss_queue_control_66/{run66.py,rss.py,netprobe.py}，仅为固定阶段66科学诊断工具。netprobe保留资源/网络只读函数，不携带旧主动probe入口；它们随计划显式提交/推送，两端核对脚本hash后执行。并非产品运行时新功能。

停止后复核与有限续接（原分散组不重跑）：分散组R1网络/资源/端点与四个主队列均通过，R2脚本在发送accepted字节==接收delivered字节的假设处失败，一条差256KiB，原报告保留failure。iperf3 3.20服务端TEST_END分支先停止并统计/关闭数据socket，不保证把sender内核接受的最后buffer全部交给接收应用；因此这个相等关系不是raw负载诊断的可靠不变量。此为源码支持的统计边界解释，不能仅凭它断言旧差值的每个字节去向。
仅修正诊断oracle：分别记录sender/receiver与gap，要求receiver达到目标且不超过预算、sender不小于receiver、端点/四队列/并发负载/双端网络资源门保持；结果明确complete_file_validation=false。CP/GridFTP正式比较仍保留完整内容SHA门，不用此解释豁免。先观察新测试在旧oracle失败，再实现并双端离线通过；固定新提交/脚本hash后，仅允许剩余集中组一次，不重跑分散组、不追加第三组。任何本组错误仍停止。原停止事件、旧脚本SHA及失败不改写为pass，独立复核记录与原始数据分开。
