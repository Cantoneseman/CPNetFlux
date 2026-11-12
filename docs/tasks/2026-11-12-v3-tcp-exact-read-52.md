# V3 TCP exact-read 52
日期：2026-11-12。输入：3798a49f05f39f81278610c7cd3379619ae3ce6a。
R770-1根目录单人串行；R2只接收固定提交。不启动RDMA或DEFERRED工作。
上一轮分类：PROGRESS，已完成Task51交付、双端同步及证据hash回读；M2仍开放。

## 目标和假设
当前PersistentDataSession::readNext先读64B头，再用readAll补足已验证长度；
FramedDataSocket::readAll以无flag的recv循环。256KiB帧不改变TCP分段，
可能为一帧负载重复进入recv/退出内核。检查是否有可实际消除的重复调用。
初步源码审查排除重复全帧初始化/vector复制；R1只有node0，无跨NUMA机制依据。
CRC扫描已走ISA-L，不把已有硬件校验重复开发。

## 本轮范围、非目标与文件
先在证据根创建一个独立C++探针，链接当前libcpnetflux_core.a，包装recv仅用于计数
和固定MSG_WAITALL候选；不改变仓库生产代码。127.0.0.1 TCP，现有writeData/readNext、
256KiB逻辑帧、逐帧CRC和offset核验，单次32MiB流式数据，三对交错(AB/BA/AB)。
每次只有一个连接，应用缓冲<2MiB；不是文件传输、不是100G成绩或M2样本。
接收SO_RCVBUF固定请求1MiB，记录实际值；不扫描参数。
保存实际recv调用、短读、成功字节、CRC、线程CPU/墙钟、进程/host资源原始记录。
MemAvailable>=12GiB、owned swap=0、host pswpout增量0；不创建源/目标大文件，
退出关闭自有socket，不操作2121或49337。无全局配置/线程调度变化。

只有实际probe证明重复recv减少，才进入一个默认关闭plain-TCP exact-read候选：
MSG_WAITALL依然可能短读；保留循环、错误/EOF/timeout/EINTR和取消shutdown语义，
不预测帧长度、不省略头校验/CRC、不增加payload缓存或队列。
TLS/RDMA readSome和控制面均不改变。必要的能力/统计接口在探针结果后冻结。
不得用probe速率代替完整V3任务，也不预先承诺40Gbps。

本阶段确定文件：本合同、证据根probe.cpp/probe.json/resource.json(后者不入Git)。
条件实现文件：include/cpnetflux/core/io/framed_data_socket.h、
src/core/io/framed_data_socket.cpp、src/core/io/persistent_tree_transfer.cpp、
tests/unit/framed_data_socket_test.cpp、CMakeLists.txt，以及复用的有界driver/测试。
实现前补充准确接口、预算、验收和故障用例；禁止顺便改接收backend或帧大小。

## 验收与停止
1. 当前源码/二进制hash固定；six local trials成功字节/CRC/frames一致，资源门通过。
2. 若重复syscall没有减少或增加额外搬运，不实现，记录结果并选择新工作量依据。
3. 实现需有旧代码失败的新行为测试；短读、EINTR、EOF、socket timeout、shutdown、
   socket move/close、默认off、TLS/RDMA拒绝/不受影响，以及持久化坏CRC/恢复验证。
4. 构建、相关CTest、commit+push/readback，再同步R2同提交同配置/二进制。
5. 最多一次<=1GiB固定可行性任务，沿用资源/网络停止门和清理；不与Task51跨提交算收益。
   达到>=40Gbps仅为正式矩阵准入，否则不自动追加8GiB、扫描或重复采样。
6. 按完整任务、CPU/GiB、内存、复杂度判定；无法证明收益时不默认晋级。

证据：/home/Su/experiments/CPNetFlux/2026-11-12-v3-tcp-exact-read-52。

## 机制确认与实现冻结
probe六次均CRC/内容通过、退出0、host pswpout不变、owned swap=0；
普通recv调用473/353/463，WAITALL均256，短读217/97/207降为0。
局部wall/CPU未形成稳定收益，故仅为机制准入，不是性能预算证明。
应用缓冲524292B，接收socket实际2097152B；RSS峰值8294400B，最低可用30622027776B。

固定实现：CPNETFLUX_TCP_EXACT_READ=off(默认)|on，只在receivePersistentTree启动解析；
on仅plain TCP且已协商262144逻辑DATA、<=4lanes，否则开始前拒绝。
FramedDataSocket::configureTcpExactRead(bool)设置任务局部状态；
仅readAll请求长度>65536且<=dataPayloadLimit时走recv(MSG_WAITALL)补足循环，
readSome、64B头、控制负载、TLS、verbs、readAllV不变。
短读继续读剩余；EINTR继续；EOF/errno传播，shutdown打断阻塞；timeout维持socket原语义。
没有新增fd/线程/队列/缓冲，仅常量计数。
TcpExactReadStats记录enabled/large_read_calls/recv_calls/short_returns/bytes/interruptions，
tree客户端lane_endpoints导出；失败readNext仍清payload不发布header。
配置在连接交给receiver前冻结；close清配置和计数，move转移状态且源失效。
关闭后后续新socket恢复原路径；不改变CRC/状态/租约/发布。

准确改动文件增加src/core/io/tree_transfer_client.cpp、
tools/perf/run_small_file_baseline.py、tools/perf/run_tcp_exact_read.py、
tools/test/run_tcp_exact_read_test.py；其余同上。
driver复用run_sender_pacing的唯一1GiB合同，pacing固定1.5e9/lane不变，
仅增加receiver exact-read，保留全部资源/网络停止门、源驻留/全SHA/chunk和cleanup。
测试在旧运行时通过现有receivePersistentTree行为断言MSG_WAITALL缺失而失败，
不以缺少API编译错误冒充红灯。后续测试覆盖具体短读/错误/timeout/shutdown/配置/状态清理。

## 执行记录
2026-11-12已观察真实V3接收测试在旧实现失败：5处大payload recv缺MSG_WAITALL；
代码实现后通过。新增exact-read故障测试目前进入相关回归。
实现没有合并header/payload，也没有新建用户read-ahead缓存：仅避免已知长负载的短读往返。
CMake为现有framed测试增加recv链接包装用于真实socket计数/定向故障。
大DATA smoke扩展--exact-read on，仅运行支持的大帧upload/download及queued/inline，
下载JSON要求65个大负载调用/准确字节；原off用例继续覆盖65532/262144两种帧。
坏CRC与超限header分别验证不发布目标/不开始payload读取。

R1构建与相关CTest 254/254通过(96.67秒)，其中exact-read smoke覆盖真实TCP双向传输、
queued/inline和准确JSON统计；启用CPNETFLUX_TCP_EXACT_READ=on的两个中断恢复回归2/2通过(6.81秒)。
这是相关回归子集，不是全仓或生产验收。下一步固定提交并推送，然后R2构建同样回归。

## 一次可行性后的统计修正范围
R2相关254项通过，双端同ac254455配置/二进制核对后，已完成唯一1GiB任务。
原始记录36.011Gbps，但新driver错误把lane_index当成全局连接索引而中止后处理：
实际四channel为0..3，每channel的lane_index均0。这是统计门的实现错误，不改运行时。
只修改run_sender_pacing.py及run_tcp_exact_read_test.py，测试改用实际channel/lane合同，
先在错误validator上失败再修复；离线复核原始只读receipt，不重跑传输、不覆盖failure.json/results.json。
源在finally已清理，所以被统计错误跳过的传输后source-check不伪装补测；目标全SHA和manifest原始记录保留。
验收：相关driver门、原始证据hash、全部可复核资源/网络/CRC/机制/清理，以及明确未执行项。

离线复核补充：窄窗rx_out_of_buffer +52、刷新尾窗missed +52，累计各+52，
网络门FAIL；36.011Gbps仅原始耗时，不是合格性能结果。只运行该一次跨机任务。
完整判定/未执行source-check/清理/下一步见[结果](2026-11-12-v3-tcp-exact-read-52-result.md)。
