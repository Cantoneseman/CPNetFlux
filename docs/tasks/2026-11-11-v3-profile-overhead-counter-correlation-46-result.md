# V3 profile扰动与接收计数窗口诊断46结果

日期：2026-11-11
合同：[Task46](2026-11-11-v3-profile-overhead-counter-correlation-46.md)
代码提交：`6f1bbf94bc4704d35ff8c3e10997b02bbe34b111`
状态：**ABORTED / NETWORK COUNTER GATE；无profile对照结论、无性能结论。**

## 结论

只运行了10个计划样本中的第1个 profile-off 样本。1 GiB文件传输、SHA-256和1024个verified chunks均通过；但接收网络观察窗口记录到 mlx5 `rx_out_of_buffer` 增量237，驱动按合同立即停止，profile-on和其余样本均未运行。不得将单个样本的34.887 Gbps或profile扰动解释为性能结果，也不重试本矩阵。

窄窗口把 +237 的mlx5队列缺buffer指标与客户端进程执行前后观察区间关联起来，但计数器是累计值，驱动可能异步刷新；它不能证明每个事件都发生在客户端进程仍运行的瞬间，也不能证明来自该传输流。接口级 `rx_out_of_buffer` 没有给出对应队列，`flow_cause_proven=false`。因此这为后续归因提供了比45更窄的时间边界，但没有完成流级或根因定位。

## 本次样本

| 项目 | 结果 |
|---|---|
| 样本 | `1g-profile-overhead-off-r1` |
| 固定数据路径 | R2→R1、4路TCP、256 KiB DATA、TCP receive-buffer关闭、1 MiB chunk、CRC32C/verified_chunks、实验mmap接收、tmpfs |
| 观测变量 | receiver profile关闭；两臂共用的phase/range timing开启 |
| 文件完整性 | 1 GiB，SHA-256 `a7c73184111fd6448739634dfd9776726b097d57f879a9aea9c4b324a7a97471`；1024/1024 verified chunks通过 |
| 任务计时 | `t_ready=0.2462 s`，驱动打印34.887 Gbps；单样本且触发网络门，仅保留诊断上下文 |
| R1资源 | 最低MemAvailable 29,656,420,352 B；测试进程owned swap峰值0；`pswpout`增量0 |
| R2源资源 | 1 GiB源全页驻留；采样MemAvailable 29,063,798,784 B；源准备至传输结束的`pswpout`增量0 |

## 接收计数与边界

| 计数 | 增量 | 解释 |
|---|---:|---|
| R1 `rx_out_of_buffer`（mlx5 ethtool） | **+237** | 诊断器标记`hardware_receive_queue_no_buffer`；接口聚合值 |
| 子进程前后窗口 `rx_missed_errors`（netdev） | 0 | 该快照边界内尚未反映增加 |
| 子进程结束至后续本地校验后 `rx_missed_errors` | **+237** | 本地哈希/manifest后的快照读到增加；与mlx5值相同，但计数刷新时序未定 |
| 完整旧式before-to-after `rx_missed_errors` | **+237** | 包含进程与后续校验，不能独立做传输归因 |

观察器记录到`rx5/rx13/rx16/rx29_packets`分别增加185,929、185,773、185,895、185,775；该信息只说明多队列上有流量，不能说明`rx_out_of_buffer`来自哪一队列或哪条lane。后续只读网卡盘点为32个combined队列、RSS indirection 0–31、RX/TX ring 1024（上限8192）、adaptive coalescing及RX 8 us/128 frames。没有更改NIC、路由、IRQ、RPS或ring配置。

## 构建、测试与清理

R1/R2均使用代码提交`6f1bbf94bc4704d35ff8c3e10997b02bbe34b111`，CMake配置一致（RelWithDebInfo、tests on、desktop off、RDMA/TLS/io_uring/isal on）。两端CMake build无待编译项；Python语法检查通过，相关单测各26/26通过，`ctest --test-dir build -R cpnetflux_large_data --output-on-failure`两端各5/5通过。三份运行时二进制SHA-256两端一致：server `125db9bf5e21cf98de5ef674d097b894cdcaabe9ed5dbadebdf3c586570e45a5`，download client `20f600140276a636740b43deb172d5ecb8bab81f593733452b7e22a91c76c30c`，upload client `b6c9e9c53360ca0dd0ef8a071518834dd80208ac66a3c1fe38af365fb3fb76b0`。

驱动在gate失败后已停止测试server并移除R1目标及R2源、两端payload根；R1目标slot已删除。R1/R2的`/dev/shm/cpnetflux-s0-profile-overhead-46`当前均不存在，49335–49338测试端口已释放。原始数据只保存在R1实验目录：`/home/Su/experiments/CPNetFlux/2026-11-11-v3-profile-overhead-counter-correlation-46`。

| 证据文件 | SHA-256 |
|---|---|
| `results.json` | `095b36d358058a5e54a61f79b3a5d4994610351c902460555a800eb917b51024` |
| `failure.json` | `8be0f4f6afa072cf5d209f92d88fcaa05f8fb88011694e04af13da1cb07facf7` |
| `cleanup.json` | `3e3bf47b24d2a48a8bbc3d8c6f4e8ddf3fc0f9be5baf450412e345d3f57005cc` |
| `server.json` | `78eaa5956eb7bb08658c89fe9ddd13bbbce4c7cd36492d698b1c08f448dce9dd` |
| `matching-builds.json` | `d8a6b75c71eb6ea02957745347e15495fd97a0e4bec3b133fc326ff5f252bbbd` |
| `samples/1g-profile-overhead-off-r1.receipt.json` | `12764b480475a6490bcb691520806aa5abed583a8e72a2243a54c3c07e098709` |
| `samples/1g-profile-overhead-off-r1.summary.json.gz` | `defde9ab3e9aa8b93e4d9fc5b8b2d1f4684154a95be813d97e43e45188e32a13` |

## 下一步

暂停profile扰动矩阵，不重跑M1-45或Task46。当前首要工作改为独立归因R1接收侧`rx_out_of_buffer`：检查mlx5计数语义与可用的队列级/RQ refill/IRQ证据，尝试把队列活动与具体TCP endpoints作时间关联，并明确现有接口聚合计数的因果边界。不得仅凭四个活跃RX队列、+237计数或单次34.887 Gbps调整系统参数或宣布软件瓶颈已定位。形成可证伪假设和新的停止门后，再决定是否恢复profile诊断或转入应用接收路径优化。
