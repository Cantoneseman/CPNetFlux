# V3范围记录ID水位53
日期2026-11-12；输入81a34b7c7bae2430c61c8c56e9019b8df539bab6。R1单人串行。
范围：FileTransaction::recordRange每完成chunk遍历manifest计算next ID的二次工作，
改用会话现有append watermark；新增只读查询和可选工作量观察，保持分配语义。
8192个chunk原扫描8192*8191/2=33550336条；这是确定工作量，不是已证实墙钟收益。
非目标：不改CRC/lease/attempt/checkpoint格式或节奏、数据写入/页管理/默认值，
不启动跨机性能试验、pacing/exact-read重复扫描、原生RDMA或DEFERRED研究。
不承诺此项足以达到40Gbps；必须报告局部CPU/墙钟与完整任务之间的证据缺口。

受影响文件：
- include/cpnetflux/core/session/verified_chunk_append_watermark.h；
- include/cpnetflux/core/session/verified_chunk_record_stats.h；
- include/cpnetflux/core/session/{download_session,transfer_session}.h；
- src/core/session/{download_session,transfer_session}.cpp；
- src/core/io/persistent_tree_transfer.cpp；
- tests/unit/session_record_lookup_test.cpp；
- 本任务、对应-result.md、docs/ROADMAP.md与docs/README.md。
证据：/home/Su/experiments/CPNetFlux/2026-11-12-v3-record-id-watermark-53。

验收：先以现有扫描语义实现查询并观察，复杂度回归应因实际扫描条数失败；
再改为已维护水位，覆盖空/零/稀疏乱序、save/move/resume、None校验、失败插入、
最大ID及损坏剔除重建；与独立扫描oracle一致。
量化8192个记录的旧/新查询，不写payload、不传网络，成本不得当整任务收益。
完成相关session/persistent/CRC/lease/取消/预算/恢复/发布门，git diff --check；
明确文件提交推送/远端SHA回读，再由R2同步同提交构建相关验收。
不以局部基准冒充M2；若开销很小如实定性。保留两份旧未跟踪文档。
