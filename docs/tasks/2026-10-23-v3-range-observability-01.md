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
状态：开发中。实际命令、输出证据、后续决策在验收后填写。

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
