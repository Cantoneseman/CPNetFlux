# V3 Frame Reuse 01 结果

日期：2026-11-06。输入 `53dadfcff94924d3cbe7018a8ae20610ce36031d`；
范围/基线 `59d8984034a57ace61f386b957dc42e8a38e3a98`；候选 `5ef3c683f44cd0b263b2faba3234549b952aa135`。
R770-1 单人免密 SSH 开发，R770-2 只同步固定提交构建。无子代理、并行工作树或桌面操作。

## 实现和所有权

readNext 保留原 owning Result API，新增调用者提供 PersistentFrame 的 overload。
接收连接只有一个帧 owner，FILE_BEGIN、DATA、FILE_END、DIRECTORY_END 均复用。
完成本帧同步 CRC/解压/whole 或 range 写入后才读取下一帧；不跨异步边界保存 span。
压缩解码仍使用独立 bounded vector，未借用 RDMA 注册槽，不称零拷贝。

先验证头部再分配；仅容量不足时创建精确长度的新 vector 并 swap，避免几何扩容。
初始为空的连接帧保留容量 <=65536 字节；增长瞬间旧/新分配短暂共存，不能把保留
容量说成整个进程的严格内存上限。相同满帧 resize 不再逐帧分配或初始化整个向量；
短帧后变长仍会初始化新增有效区域。读失败清空有效 payload 和 header，只发布完整帧。
无协议、默认校验/checkpoint/commit、队列、注册池或网络配置变更。

receiver_payload_observation 升为 v2：frame_payload_allocations 为 DATA 容量增长次数，
frame_payload_reuses 为未增长的 DATA 次数；allocated_capacity_bytes 是增长时新容量
合计，peak_capacity_bytes 是 DATA 保留容量峰值。旧 capacity_bytes 仍为各 DATA 所见
容量之和，既不是累计分配量也不是 RSS。控制帧增长、解码和 RDMA 分配不在这些 DATA
分配计数内；profile 默认关闭。每 GiB 候选 DATA 分配 1、复用 17407、分配新容量和
保留峰值均为 65536 字节；CRC 与 write 字节仍均为 1073741824。

## 回归和固定版本门

先收紧独立 new 包装器的真实 4 MiB+37 字节传输回归，旧实现按预期以 69>6 失败：
64 个满帧分配加 5 个 chunk 复核缓冲。候选满足 <=1+5 的预算并连续 20 轮通过。
没有取消复核或只改 profile 计数。另增四项 caller-owned 测试：逐步增长/短帧/空控制帧/
跨文件复用，非法和超限头在分配前拒绝，截断头/半 payload 清空，shutdown 唤醒读取。
已有八种 CRC/none、raw/压缩、观测 on/off wire/output oracle 继续通过。

cmake --build build -j4、git diff --check 通过；R770-1 相关 191/191，再全量
547/547（257.71 秒）；R770-2 相关 192/192
（58.47 秒），均无跳过。CTest 串行，使用新测试 token 和
显式 CPNETFLUX_TEST_RDMA_BIND；token 不入日志/仓库。原始命令和输出在 acceptance。
首次全量为546/547，peer_mapping_smoke在控制面目录请求阶段失败（拒绝原因未保留）。带trace复现1轮、无trace但保留控制响应的候选5轮及优化前对端5轮均通过；随后保留逐用例证据的全量复跑547/547通过。未修改控制路径，根因尚未确认，不能称已修复。生成的CTest命令仅临时追加测试原有--evidence-dir并已按原字节恢复，记录在full-rerun-test-evidence-override.json。诊断fixture清理收据保留。
源码提交、推送并回读 GitHub SHA 后才同步对端；每次互传核对同提交、无 tracked dirty、
八个二进制 SHA-256、CMake 配置和版本一致。最终文档提交 SHA 见外部 github-backup.json。

## 七个内存实验及结果

单 lane，R770-2 -> R770-1，源/目标均 tmpfs、1 GiB，随机 8 MiB 块重复128次，压缩 off，
CRC32C、1 MiB chunk、pipeline depth=2、direct 写入、every_n_chunks/16、commit_sync=none。
同一源 inode 21，SHA-256 `b2a198cfe01bc24a882f253c7c3a39db1af779728f0cf1b0d19bd6641417b200`。
显式绑定 192.168.100.2/.1，eno17095np0，实际 data_transport=rdma，双端 QP/路由/
接口计数保留；控制面仍 TCP。对照三次后候选三次，另一次候选 full 控制。
每次新建目标；目标准备和 RDMA 准备都在完整 task 内。源预热/SHA、外部输出 SHA
在 CLI 计时外，内部提交校验仍计时。小组顺序测量不是随机配对或 steady-state 上限。

| verified_chunks，各三次中位数 | 对照 | 帧复用 |
|---|---:|---:|
| 文件阶段 wall，秒 | 0.447252 | 0.433435 |
| 文件阶段 rate，Gbps | 19.206028 | 19.818295 |
| CLI task wall，秒 | 2.767839 | 2.768433 |
| CLI task rate，Gbps | 3.103481 | 3.102815 |
| GNU time CLI elapsed，秒 | 2.740000 | 2.710000 |
| GNU time CLI rate，Gbps | 3.135013 | 3.169718 |
| 冷 data_connect，秒 | 2.102980 | 2.109270 |
| 三个 payload 线程 CPU，秒 | 0.791535 | 0.777223 |
| 客户端 user+sys，秒 | 1.640000 | 1.640000 |
| 双端 CPU 估计，秒 | 3.160000 | 3.140000 |
| 接收 lane CPU，秒 | 0.445411 | 0.431713 |
| DATA 帧分配 | 17408 | 1 |
| 实际 writeAtAll 调用 | 17408 | 17408 |
| writeAtAll 累计 wall，秒 | 0.234729 | 0.234457 |

文件阶段 rate 中位数变化 +3.188%，接收 lane CPU 变化
-3.075%，完整 task rate 变化 -0.021%。
仅解释为本轮小负载观测；三次不足以证明普适收益。allocation 从 17408 降至 1 是资源
行为的确定变化，吞吐和 CPU 结论必须以表中重复样本判断，不能等同扩大到 100G。
外层task墙钟含Python timeout wait轮询，当前Python3.14.4的该循环最长等待50ms；GNU time elapsed另列，输出分辨率0.01秒。含准备和提交的CLI未移出计时，不按小于计时分辨率的变化声称净收益。
客户端 GNU time CPU 分辨率0.01秒；服务器 CPU 用最后可用 /proc tick，可能漏掉尾部，
双端相加是估计。三个 payload 线程不等于全进程总 CPU，嵌套子计时不能相加。
task rate CV 对照/候选为 0.841704%/1.478350%。
候选 full 单次 task=2.867759s、
2.995348Gbps，文件阶段=0.554586s；
这是 full 正确性控制，没有重复 full 对照，不能声称 full 提速。

## 完整性、内存、错误边界和清理

7/7 正式样本全部 CRC、外部 SHA、源和目标页驻留、FILE_RESULT/DirectoryEnd/控制终态、
原生 QP 证据通过，恢复次数为0；无被排除样本。发送进程物理读取字节逐样本为 [0, 0, 0, 0, 0, 0, 0]。
启动 MemAvailable >=12 GiB、tmpfs free >=2 GiB；守护采样 <6 GiB 或 owned RSS>4 GiB
只停止本次实验。两端约2.04 GiB 接收池仍存在；RSS 和 tmpfs 分开记账。

| 七个正式样本 | R770-1 | R770-2 |
|---|---:|---:|
| 最低 MemAvailable，GiB | 25.468071 | 25.053513 |
| 峰值实验 RSS，GiB | 2.067951 | 2.067909 |
| 峰值 tmpfs 用量，GiB | 1.000042 | 1.000000 |

无守护 violation、采样到的 owned VmSwap 或主机 swap-out；主机全局 swap-in 页数见
rollup.json，不能归因给 payload。RNR/out_of_buffer 均为0；CQ response error/flush
唯一增量组合为 `{'R770-1': [(65, 64), (129, 128)], 'R770-2': [(65, 64)]}`。成功传输并未完成 CQ 生命周期归因，稳定性门继续保留，首次peer-mapping控制拒绝同样未归因。
每次输出 SHA 后已删除，最终源按 inode/大小/SHA 再验后删除，两端本轮 tmpfs 根已不存在。
原生产 PID 126766/164834、仅 GSI 的空闲 QP、两份旧未跟踪 October 文档均保留。

## 下一步

帧复用阶段完成。下一项独立拆分冷 RDMA 建连中的池分配/清零、MR 注册、接收预投递、
握手和 QP 关闭/在途完成所有权。先找成本再选择池生命周期或复用方案，不盲目缩减 RQ。
准备时间继续纳入完整 task；保留单 lane/1 GiB 内存预算和 CQ/RNR 门。不能把本轮内存
文件阶段当作磁盘持久化带宽、100G 软件上限或生产 readiness。

证据根：`/home/Su/experiments/CPNetFlux/2026-11-06-v3-frame-reuse-01`。验收命令与日志、matching-builds-*.json、comparison.json、rollup.json、
cleanup-*.json、github-backup.json 和 HANDOFF.md 保留在该目录，payload 不入 Git。
