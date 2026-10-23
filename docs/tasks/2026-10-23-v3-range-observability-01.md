# V3-RANGE-OBSERVABILITY-01

输入：49a721a29b10112c8b50b77528723b9af3d653f5；单人串行，深圳根目录。
范围：将大文件 range 接收的 Entry 锁等待/持有、offset writer、块 CRC、writer flush、
session/checkpoint 序列化/临时写入/rename 拆开；补每 range/lane 的 pipeline wait 秒数、
高水位、读/CRC/socket 帧发送、producer 与 lane 线程 CPU。保持 elapsed 起止点不变。
只观测本进程：客户端 file_timings 和服务端 CPNETFLUX_PERSISTENT_TIMING 日志分别保存，
不把服务端时间伪装成本地指标，不扩充数据协议。
诊断环境 CPNETFLUX_PAYLOAD_PIPELINE_DEPTH=2|4|8|16 仅控制所在进程的发送端；
未设置仍为 2。CPNETFLUX_RANGE_TIMING=off 关闭新增诊断，以便同二进制校准探针开销。
非目标：不改锁结构、检查点频率、TLS/CRC/resume/最终校验语义，不宣称跨机最终性能提升。
受影响文件：payload_pipeline.h/cpp、persistent_tree_transfer.h/cpp、
checkpoint manifest_save_stats.h、manifest_store.h/cpp、download_manifest.h/cpp、
transfer_session.h/cpp、download_session.h/cpp、control_server.cpp（观测日志完整 flush）、
tests/unit/persistent_data_session_test.cpp、checkpoint/session 相关测试、
tools/test/run_cpnetflux_range_observability_smoke.py、性能采样脚本、CMakeLists.txt、
本任务证据、性能路线和 ROADMAP（按实际 diff 记录）。
验收：
1. 真实 socket/磁盘回归先在原二进制失败；指标能区分读/写/锁/checkpoint，计数与实际 rename 对照；
   producer/consumer 阻塞和取消计时有行为测试，诊断关闭不改变字节正确性。
2. Release 构建、相关测试、全套 CTest；错误路径不返回虚假成功。
3. 固定输入/二进制校准诊断开销，深度 2 对 4/8/16 每候选至少五组交错配对，
   保存中位数/范围/每 lane 分布和独立 SHA；低于稳定 10% 收益不改默认。
4. 实际检查 CPU 栈工具/权限，保留每线程 CPU；不可用则写明原因，不虚构 profile。
5. 精确文件提交推送回读，固定同一 commit 同步上海并构建/核对二进制；
   任何互传都在双端版本一致后开始。大文件 payload 仅在 /tmp。
状态：已完成实现与本机诊断验收；pipeline 深度候选均 no-go，默认保持 2。最终物理机器性能门仍待阶段 8。

## 实现与回归
- per-range file_timings 新增 range_observation、pipeline 等待/预算/水位、
  producer/lane CLOCK_THREAD_CPUTIME_ID CPU、DATA socket writeAll 与 frame prepare；
  客户端和服务端分别保持 local_sender/local_receiver。wire/elapsed 定义不变。
- Entry 锁指标只覆盖 begin 的 Entry 段、write、complete；全局 context 初始化锁、
  查询 missingRanges/takeStats、后续完整性审计不在此锁指标内。begin 的初始 manifest 保存
  可能在 context 锁内，因此不能将所有 manifest 时间都当作 Entry 持有子集。
- checkpoint_seconds 包含 range 记录更新/排序和自动/显式 manifest flush，不含 writer flush；
  manifest_save 的序列化、temp open/write/close、rename 是其子区间（含初始化/提交时的额外保存）。
  chunk_flush 指每个已完成块的 writer flush 调用，POSIX 直接写时可很短，不代表 fsync。
- pipeline wait 是条件变量等待到重新取锁的时间；多个线程、lane、子区间会重叠，禁止相加冒充 wall。
  queue budget 不包含 producer/consumer 正在使用的缓冲和 frame 副本，不是进程总内存。
- sender lane CPU 范围为 payload prepare/send/join；producer CPU 在自己的线程采样；
  receiver lane CPU 是 begin 至结果写出。它们是 CPU 秒，不是 wall 秒。
- 共享 FileTransaction 不保留 lane 栈指标指针；每次 session 保存用作用域绑定并在返回时清空。
- 先运行原二进制的真实 64 MiB 双向回归，按预期因缺少 pipeline depth 指标失败；
  后将 fixture 改成 128 MiB 且双端 chunk=4 MiB，确保真正触发 striped range。
- CTest 新增观测开启/关闭双向 128 MiB SHA 和 inotify rename 核对；
  关闭探针时仍有真实 checkpoint rename，新增计时和计数为零。
- 回归覆盖真实 producer/consumer 等待、取消唤醒、关闭诊断、拒绝超界深度、
  两种 checkpoint 格式成功/rename 失败/序列化前拒绝。
- Release build 日志 /tmp/cpnetflux-range-build.log；全套
  CPNETFLUX_TEST_TOKEN=<test-only> ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j1
  结果 432 项、0 失败、1 项 io_uring 环境条件跳过，152.56 秒。
  日志 /tmp/cpnetflux-range-ctest.log。git diff --check 通过。
- TLS 性能脚本 128 MiB quick harness 完成 4 次传输和独立 SHA，仅用于脚本验收，
  /tmp/cpnetflux-range-harness-02/result.json，不作为深度收益证据。
后续：固定本轮代码提交，4 GiB 五组诊断校准与各候选五组配对；再同步上海构建。

## 参数实验环境修订
实测深圳为 2 vCPU、总 RAM 3495 MiB、可用约 2417 MiB。4 GiB 输入大于缓存容量；
pilot 第一条完整 process wall=72.4797 秒，core=47.0407 秒，且额外独立 SHA 涉及磁盘。
因此本项保留两条 4 GiB 开/关观测 pilot 后停止扩大该矩阵；它不是原 R730/R750 热缓存复测。
正式 40 次参数/开销实验改用 512 MiB 固定输入，四路、TLS/CRC/resume/外部 SHA、
4 MiB chunk、64 KiB frame、receiver verified_chunks/final_only/commit_sync=none 不变。
这是阶段 4 的软件诊断矩阵；阶段 8 的物理机器 4 GiB 与 GridFTP 对照仍保留。
脚本报告缓存为“预读一次、未清系统缓存、不保证驻留”，并保存 RAM 信息，避免宣称整文件热缓存。

## 正式实验结果与下一步
固定 source commit f180d3a9ca36d11046a5c2e7347c4555fc20aa6c；
运行时代码 commit 2e509b3a432a53dc987b0511cb821e881dcd89fe（均已推送并回读同 SHA）。
命令：
python3 tools/perf/run_cpnetflux_range_observation.py --build-dir /tmp/cpnetflux-build-fd-release-01 --evidence-dir /tmp/cpnetflux-range-perf-512m-01 --bytes 536870912 --rounds 5
40 次传输全部成功、独立 SHA-256 一致；每候选 5 组交错配对。TLS 控制/数据均 required，
CRC Auto、无压缩，receiver final_only/verified_chunks/commit_sync=none；
独立 SHA 在计时之外，process wall 包含客户端 summary，不更改 core elapsed。
结果（秒，process wall 中位数与范围）：

| 组别 | 深度 2 | 候选 | 对深度 2 中位数变化 |
| --- | --- | --- | --- |
| 深度 4 | 1.513294 [1.481294, 1.557847] | 1.522290 [1.518047, 1.531713] | +0.594% |
| 深度 8 | 1.515302 [1.503791, 1.520161] | 1.514130 [1.509995, 1.522411] | -0.077% |
| 深度 16 | 1.503022 [1.411333, 1.518583] | 1.501584 [1.208317, 1.860615] | -0.096% |

均未达到稳定 ≥10% 改善；深度 16 个别样本正负波动明显，不能选择最快单条当作收益。
queue budget 从深度 2 的每 lane 131064 B 增至深度 16 的 1048512 B，仍无稳定提速，故默认保持 2。
诊断开销校准 5 组：关闭中位 1.518635 秒，开启 1.435741 秒；样本存在明显波动
（关闭范围 1.328181–3.276877，开启 1.154352–1.912607）。本轮未观察到持续减速，
不能将开启更快解释成负开销，也不能据此给出精确探针开销上限。

depth=2 观测样本共 20 条，各线程累计秒数的中位数：
- sender 源读取约 0.081 秒、CRC 0.134 秒、DATA socket/TLS writeAll 2.971 秒；
  producer 满队列等待 3.050 秒、consumer 空队列等待 0.393 秒。
- receiver Entry 等锁 2.455 秒、持锁 0.961 秒、offset writer 0.251 秒、
  chunk CRC 0.067 秒、checkpoint 0.625 秒。
- 每个 512 MiB 文件 128 次块 flush，131 次 manifest 保存/rename（块 128 + 初始化/提交 3）；
  manifest 序列化约 0.004 秒、rename 约 0.626 秒（跨样本独立中位数不能当作同一条相加）。
- 4 GiB 两条 pilot 均完成独立 SHA；开启观测样本记录 1024 块 flush、1027 次保存、
  Entry wait 8.569 秒、hold 6.966 秒、offset writer 2.643 秒、checkpoint 3.747 秒、
  manifest rename 3.534 秒。core 47.359 秒、完整 process 72.381 秒，
  含云盘/cache 容量限制，不是原 R730/R750 吞吐复测。pilot 在两条完成后主动停止，
  后续中断条目不作结果，原始 report 的 running 状态不能称为完整矩阵通过。
- perf 可执行文件不存在，perf_event_paranoid=4；未取得 CPU 栈、未修改系统权限。
  每 lane/producer CPU 秒和全部分项保存在 evidence，不能据此声称已定位 TLS 内部 CPU 栈热点。

判断：本机证据支持先执行阶段 5，降低锁内逐块 checkpoint rename 次数；
producer 等待较多且加深队列无收益，优先处理下游 checkpoint/Entry 串行段。
这是下一项实验假设，不是已证明批处理一定能提速，也不替代阶段 6 的并发正确性设计。

## 双端版本与备份
上海同步前逐一核对旧提交归档的 619 个文件，未覆盖未知改动。以固定 source commit
f180d3a9ca36d11046a5c2e7347c4555fc20aa6c 生成归档，双端 SHA-256：
08e307f5a513e07790c11eaa656b8c3d8ee58b65b3e843519cf940c8b22d174b。
上海 /root/projects/CPNetFlux/SOURCE_COMMIT 同 SHA；Release 完整构建成功，18 项专项 CTest 全通过。
深圳/上海关键二进制 SHA 完全一致：
- cpnetflux-gridftp-server: 942b34e04f100b29f7f47fd4f2b31add59a2a3199da1d395d234e8133b2fa31e
- cpnetflux-tree-upload-client: 2a87b92e1164793a73f41d585ec57347a396299574a228a142401d1404360ca6
- cpnetflux-tree-download-client: 1da20943ead0fab3874bb847d89134f978d4d68f15f654013150ef0167ebe004

本项性能传输都在深圳本机完成；上海专项双向 smoke 在上海本机完成，
不冒称已经完成本轮公网互传或最终 R730/R750 对照。现有系统服务未替换。
源码与该阶段证据均按明确文件提交 GitHub；最终证据提交 SHA 由聊天和 Git remote 回读记录，
避免在提交中写入其自身 SHA。
下一步：V3-RANGE-CHECKPOINT-BATCH-01，保持先 flush 数据再记录 verified chunk，
有界合并 per-file manifest 保存；候选每 1/4/16 块、100 ms 触发和提交屏障需恢复回归。
当前路线 4/8 完成，剩余 checkpoint 合并、range 并行写、profile 热点优化和最终双机验收 4 项。
