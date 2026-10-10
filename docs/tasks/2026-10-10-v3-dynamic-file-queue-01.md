# V3-DYNAMIC-FILE-QUEUE-01：动态文件队列与 V2 阶段计时

日期：2026-10-10；状态：实现中。权威输入：深圳 de11e15bcc8f92e8d730be1b2483420bd528ef39。
分支：codex/V3-DYNAMIC-FILE-QUEUE-01；复用干净 worktree /tmp/cpnetflux-runs/V3-DYNAMIC-FILE-QUEUE-01/src。
本轮用户明确要求独立 worktree，覆盖旧 AGENTS 的根目录直接开发偏好；不操作根 index 或混入 dirty。

## 接手事实与最小切片

底层输入仍为 ea0dda9 的 V2 多通道：fileId 静态取模、每通道独占 TCP、pending 1..16、已有 file generation/manifest 事务。
输入后继提交仅规划/桌面设计；没有已有 V3 源码。根目录 dirty 保留。深圳 / 与 /tmp 同盘，约 16GiB 可用；本批构建和临时 loopback 峰值预算 <=2GiB，保留 >=10GiB。
后续切片依次为 V2-TRANSACTION-STATE-01（统一任务/文件/range/attempt 状态）；V3-RANGE-01（阈值、范围事务及缺失恢复）；V2-CHECKSUM-RESUME-01；V2-DATA-TLS-01；V2-DEFAULT-COMPAT-01。V1 删除另立任务，不在本批执行。

## 范围与契约

- 在现有 V2 file transaction 上增加可协商的 dynamic scheduling，不另造 V3 文件状态语义。
- --file-scheduling dynamic|static；tree reuse 下默认动态，静态保留原 XDIR/XDIRP 行为；V2 仍 opt-in。
- 上传客户端/下载服务端共享有界环状元数据 ready queue，大小降序、同大小稳定 file_id；队列只保留 PersistentFileIdentity 元数据，沿用扫描清单，无文件内容缓存。
- 通道在 pending credit 可用时原子领取；每个文件唯一领取，结果必须绑定 owner、file_id、generation、path、transfer_id、size、mtime、chunk_size。失败取消整批，不自动重放。
- 动态目录批次显式 ID，所有通道须匹配参数/路径/方向；全局去重、文件总数和成功完成门。断线、超时、取消释放本批连接和队列，目标碰撞不覆盖。批次有一个有界生命周期监视线程，只检查本批控制连接；退出前 join，解绑后不引用旧 fd。
- 单机 monotonic 计时，增加逐文件 queue_wait/read/first_payload/payload_io/file_result/manifest/finalize/wall；目录 summary 增加 control_prepare/data_connect。明确客户端/服务端观测和并发累加边界，未观测字段为 null，不虚写 checksum 数据。
- structured summary 增加 requested/actual scheduling 与 fallback_reason，旧服务能力拒绝在 payload 前回退；payload 后失败不换协议。

## 非目标

不实现 range striping、动态阈值、checksum/resume/TLS 新能力，不启用 V2 默认，不删除 V1，不改桌面端，不做 WAN/GridFTP/性能矩阵，不停止用户进程，不碰其他项目。第一阶段不承诺性能门已达成。

## 允许路径

include/cpnetflux/core/io/{persistent_data_session,persistent_tree_transfer,dynamic_file_queue,persistent_directory_batch}.h；
src/core/io/{persistent_data_session,persistent_tree_transfer,tree_transfer_client,dynamic_file_queue,persistent_directory_batch}.cpp；
include/cpnetflux/config/tree_transfer_options.h；src/config/tree_transfer_options.cpp；
src/protocol/control/control_server.cpp；CMakeLists.txt；
tests/unit/{persistent_data_session,tree_transfer_options,dynamic_file_queue,persistent_directory_batch}_test.cpp；
tools/test/run_cpnetflux_tree_dynamic_queue_smoke.py；
本任务与结果文档。发现新增必要路径先补任务范围，不扩桌面文件所有权。

## 验收与证据

基线：固定输入 worktree Release/Ninja TLS ON、io_uring OFF；定向现有 PersistentSessionTest/TreeTransferOptionsTest。
行为测试先验证旧版拒绝动态请求，再实现唯一领取、ring wrap/backpressure、身份拒绝、超时/断线/取消及阶段数据。
顺序：定向单测 -> 全量 CTest -> 128x1MiB 动态双向 -> mixed 静态/动态 hash/manifest -> 大文件单文件完整传输回归（range 尚未实现，显式 deferred）。
命令：cmake -S <worktree> -B <run>/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCPNETFLUX_BUILD_TESTS=ON -DCPNETFLUX_ENABLE_TLS=ON；
cmake --build <run>/build --parallel 2；
ctest --test-dir <run>/build --output-on-failure；
python3 tools/test/run_cpnetflux_tree_dynamic_queue_smoke.py --build-dir <run>/build --case dense|mixed|large --evidence-dir <run>/verify/<case>。
全量 CTest 的 token smoke 需要 CPNETFLUX_TEST_TOKEN；只在子进程环境生成临时测试值，不打印或写入仓库。
证据：/tmp/cpnetflux-runs/V3-DYNAMIC-FILE-QUEUE-01/verify。每步保存 stdout、退出码、summary/独立 SHA-256 和 manifest 结果。仅明确文件 stage/commit，push origin 并 ls-remote 回读。

## 回滚

运行时 --file-scheduling static 恢复原固定 shard；--data-session-reuse off 使用原 V1。代码撤销本批明确实现提交，先保存任何后续修改；不 reset/clean 根目录，不删除未知 payload、其他 worktree 或用户作业。
