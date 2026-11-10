# V3 接收端 profile 诊断45结果

日期：2026-11-11
任务合同：[接收端 profile 证据保留与定向诊断45](2026-11-11-v3-receiver-profile-diagnostic-45.md)
代码提交：`82784e540c11b8ac444be0414467e1c488b22342`
状态：**ABORTED / NETWORK COUNTER GATE；无性能结论。**

## 结论

帧尺寸矩阵计划执行10个1 GiB任务，在第6个样本发现接收接口 `rx_missed_errors` 增加405后按合同立即停止。前5个样本和触发门限的第6个样本均完整传输，外部SHA与接收端SHA一致，1024个verified chunks全部通过。资源与payload清理门通过。接收buffer矩阵没有启动；本次数据不能用于判定帧尺寸性能收益，也不能用于选择或晋级运行时配置。

## 已完成样本

| 样本 | DATA上限 | DATA帧数 / 物理写调用 | `rx_missed_errors`增量 | 完整性 |
|---|---:|---:|---:|---|
| `1g-frame-65532-r1` | 65,532 B | 17,408 | 0 | pass，1024 chunks |
| `1g-frame-262144-r1` | 262,144 B | 4,096 | 0 | pass，1024 chunks |
| `1g-frame-262144-r2` | 262,144 B | 4,096 | 0 | pass，1024 chunks |
| `1g-frame-65532-r2` | 65,532 B | 17,408 | 0 | pass，1024 chunks |
| `1g-frame-65532-r3` | 65,532 B | 17,408 | 0 | pass，1024 chunks |
| `1g-frame-262144-r3` | 262,144 B | 4,096 | **405** | pass，1024 chunks；随后触发停止门 |

六个样本的接收文件均与预期SHA-256 `05457bb45c8a1f56a95d388b04c876be019268c6390415cbfe02b17e9f65096b` 一致。只完成了前两组AB/BA配对，第三组的control样本完成、candidate样本触发门限；计划中的其余配对均未运行。驱动保存了部分 `results.json` 和 `failure.json`，未生成完整矩阵 `analysis.json`。

## Profile观察边界

已保留逐range的 `cpnetflux_receiver_payload_observation_v2` 与 `cpnetflux_receiver_cpu_observation_v1`。在这6个样本中，profile记录的DATA CRC覆盖1 GiB、`logical_copy_bytes=0`；DATA帧数与接收端 `physical_write_calls` 一致。65,532 B配置为17,408帧/次物理写，262,144 B配置为4,096帧/次物理写。每个样本出现4次payload buffer分配，其余DATA帧复用连接持有的buffer。

这些是启用计时和CPU观测后的局部计数。观测scope包含计时器成本，CPU字段是指定wrapper的CPU时间且不是任务墙钟分解；此次矩阵不完整，且触发了网络接口门。因此不引用其吞吐字段作为性能结果，不将局部时间差解释成整体提速，也不推断 `rx_missed_errors` 由profile或帧尺寸造成。

## 双端构建与测试

R770-1与R770-2均在固定代码提交 `82784e540c11b8ac444be0414467e1c488b22342`、相同RelWithDebInfo配置下构建；desktop off、tests on、RDMA on。两端 `cmake --build build -j4`、Python脚本编译检查通过；baseline与large-data Python测试各11/11通过，`ctest --test-dir build -R cpnetflux_large_data --output-on-failure` 为5/5通过。

| 二进制 | 两端共同SHA-256 |
|---|---|
| `cpnetflux-tree-server` | `125db9bf5e21cf98de5ef674d097b894cdcaabe9ed5dbadebdf3c586570e45a5` |
| `cpnetflux-tree-download-client` | `20f600140276a636740b43deb172d5ecb8bab81f593733452b7e22a91c76c30c` |
| `cpnetflux-tree-upload-client` | `b6c9e9c53360ca0dd0ef8a071518834dd80208ac66a3c1fe38af365fb3fb76b0` |

## 资源、清理与中止

- 六个样本中最低 `MemAvailable` 为29,477,138,432 B（约27.5 GiB）；owned swap峰值为0，R1 host `pswpout` 增量为0。
- 每个样本的传输和内容校验完成；第6个样本在接口计数增量检查后触发 `AssertionError('receiver profile diagnostic rx_missed_errors gate')`，`failure.json`记录 `completed_samples=6`。
- `cleanup.json`记录本地payload根、R2源文件和R2 payload根均已移除；R1/R2的两个精确 `/dev/shm/cpnetflux-s0-receiver-profile-45-*` 根当前均不存在。
- 不运行同一矩阵的重试；不启动receive-buffer矩阵；不更改默认值、NIC、路由、swap或系统配置。

## 下一步

先对这次已有receipt、接口计数采样位置和profile计时代码做只读审查，确认事件来自哪一端/哪一接口、profile是否增加了足以影响诊断的观测开销，以及现有证据能否把单次增量与传输阶段关联。单个计数样本不足以归因。只有形成清楚的新假设、资源门和停止条件后，才另立有界合同；在此之前不启动新传输实验，不将帧尺寸候选标记为GO或NO-GO。

## 证据

原始证据目录：`/home/Su/experiments/CPNetFlux/2026-11-11-v3-receiver-profile-diagnostic-45/frame`。

| 文件 | SHA-256 |
|---|---|
| `results.json` | `f210f61be4579e031d70f66c2a14019aac3c9767c71587a93ae61a30f344abba` |
| `failure.json` | `eedbd901618b15cfa52dfc071a355080439cec45d2de47c555349d9916f389b9` |
| `cleanup.json` | `3e3bf47b24d2a48a8bbc3d8c6f4e8ddf3fc0f9be5baf450412e345d3f57005cc` |
| `server.json` | `acad17a9af4e0ecb409c5dae9b584d0eef9d0b0f8c2307cd0b80662072788cd7` |
| `matching-builds.json` | `444622debdc25bc16ba9760a892fed09380253866750c16e99fc24fca3cef4be` |
