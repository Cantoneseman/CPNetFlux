# 阶段65：TCP连接到接收队列的证据补全

输入提交：`6a86d4ea510cf7d7660a968d7df3f6d7d90ff4f8`。实现提交：`4454e5da510e36095001e06d9ec4f96c27b9ffd1`，已推送并回读 GitHub 分支 SHA。R770-1 为唯一写入者，R770-2 仅固定提交构建和验收；无子代理、worktree 或系统网络修改。

本阶段结论为 **OBSERVABILITY CLOSED / NETWORK CAUSE OPEN**。补齐了可验证的流端点与 CPU/NAPI 观测；没有新增性能成绩，没有证明或修复 OOB 根因。阶段64比较仍为 NO-GO（仅1/5完整有效配对，第2对CP OOB/missed最终各+1275），稳定性 NOT_RUN，上机 NOT_READY；不计算新的速度倍数、不宣称稳定40G或M2通过。

## 解决的具体缺口

阶段64只保留了各接收队列聚合包数，失败样本疑似两条流集中到同一队列，但缺少真实TCP端口，无法重建该样本的逐流哈希。直接使用普通四元组Toeplitz还会误判：本机实际启用 `symmetric-or-xor` 输入变换。

本阶段只读取实际 RSS key/256项间接表/tcp4字段/32队列/ring1024配置，按 OR/XOR 变换后计算 Toeplitz。独立10组hash向量及4项离线测试通过；12条串行、每条256KiB的低速连接，预测队列12/12与主要实际RX队列一致，并记录CPU/NAPI。总应用数据3MiB，单连接64KiB以下活动接收块，观察窗口网卡/HCA错误和host pswpout均无增量。它验证的是当前部署的预测方法，不能倒推缺失四元组的旧失败样本，也不能证明流碰撞导致OOB。

## 软件交付

V3客户端JSON的 `lane_endpoints` 增加控制/数据两端实际端口、`data_endpoint_scope=last_connected_socket`、`tcp_last_incoming_cpu`、`tcp_last_incoming_napi_id`。连接/重连时记录端点，在既有最终统计位置、关闭socket前读取可选CPU/NAPI；不在逐帧路径新增系统调用。CPU/NAPI是最后接收包的属性，不是全部包的历史或丢包归属；端点只代表最后一次成功连接，重连会清除旧观测。

不支持/不可得的值明确为null；RDMA数据端口和TCP CPU/NAPI不从bootstrap TCP冒充。原生RDMA未运行（仍暂停），不能把TCP网卡实验说成verbs。此次没有修改协议、DATA大小、pacing、缓冲预算、默认配置或吞吐算法。

受影响源码/测试：`src/core/io/tree_transfer_client.cpp`、`tools/test/cpnetflux_fault_proxy.py`、`tools/test/run_cpnetflux_peer_mapping_smoke.py`。代理记录真实accept四元组作为独立端口oracle，覆盖正常TLS双向传输、data-reset上传、control-reset下载和不可达失败；重连后必须匹配最后真实连接，不接受只填写某个合法端口。

## 验收与版本

先对旧二进制运行加强测试，出现预期 `socket endpoint observation fields missing` 失败；新实现转绿。最终两端各5/5 CTest通过，无跳过：

```sh
cmake --build build --target cpnetflux-gridftp-server cpnetflux-tree-download-client cpnetflux-tree-upload-client cpnetflux-private-tmpfs-launcher -j 2
ctest --test-dir build -R 'cpnetflux_peer_mapping_smoke|cpnetflux_v3_logical_link_matrix|cpnetflux_large_data_smoke|cpnetflux_tcp_kernel_pacing_smoke|cpnetflux_private_sender_policy' --output-on-failure
python3 /home/Su/experiments/CPNetFlux/2026-11-12-rss-flow-evidence-65/verify65.py
```

固定实现提交后，两端同RelWithDebInfo、同可选后端配置、四个二进制SHA-256一致，随后仅运行一次跨机功能核验：R2 `.2`→R1 `.1`，显式绑定直连接口，4条TCP数据连接，一个零字节文件；无DATA有效负载，不是原性能profile、不是性能/恢复/稳定性实验。

| channel | 服务端口→客户端口 | RSS预测队列 | 最后接收CPU | NAPI ID |
|---|---|---|---|---|
| 0 | 61100→58689 | 3 | 3 | 8196 |
| 1 | 61102→46443 | 10 | 10 | 8203 |
| 2 | 61101→53811 | 25 | 25 | 8218 |
| 3 | 61103→51087 | 20 | 20 | 8213 |

这里是4个channel，单link的lane_index均为0，不能把lane_index当作唯一连接ID。四组端点互异，CPU数字与预测队列一致；队列20的CPU/NAPI另有本阶段串行probe直接对照，其他3个队列没有同队列串行probe，不能冒称4条均有逐流独立RX计数证实。CPU编号与队列编号相同也不是通用Linux保证。

客户端V3结果pass、completed_files=1、bytes_transferred=0，目标存在且大小0；两端网络门、资源门及清理均通过。客户端time最大RSS为10,144KiB；短任务结合进程采样与time -v，不当作长期内存泄漏证明。停止服务的首次Windows命令因远端shell转义失败（包装命令exit2，客户端本身exit0），随即用stdin脚本触发停止；服务实际exit0，未超过租约、无残留，不重跑样本。

## 证据和清理

原始证据：`/home/Su/experiments/CPNetFlux/2026-11-12-rss-flow-evidence-65`。冻结清单逐文件记录SHA-256；`verify65.py`离线验证hash与结果语义，不发包、不重跑矩阵。R2公开证据归档 `peer65.zip` SHA-256：`08c61e985efe91307c702156fbd01654ca5eb398fb661794631aad665f7a7cb9`，在R1的 `peer-r2/` 解包核验。未归档私钥或数据payload。

零字节核验的两端临时tmpfs根、临时服务49366和probe49365均已清理；RED功能测试的源/目标数据在记录hash后清除，日志/summary保留。原2121服务身份R1 PID126766/start1191040、R2 PID164834/start1679976保持；最终pswpout R1=4693038、R2=2292432。没有清理旧实验、CPSS(DCC)或不相关目录。

## 下一步与停止边界

下一项是有明确假设的网络诊断：在相同固定发送策略、相同数据量下，结合真实四元组、RSS预测和接收CPU/NAPI，区分“同一RQ聚集”和“不同RQ仍丢包”，再验证是否需要软件连接布局或发送突发边界修复。先设计可控制的分组与验收，不能随机重复高吞吐样本直到碰巧不丢包；不得擅自改系统RSS、ring或IRQ配置。

只有网络失败得到解释且候选通过代码/功能门，才重启固定版本GridFTP正式配对；满足比较门后做至少1800秒且至少100任务的稳定性、恢复/取消/TLS与资源趋势验收。8GiB物理空闲内存门仍未准入，不因本次0字节成功恢复大负载。失败实验与历史冻结保持，不将工程闭环替代完整用户目标。
