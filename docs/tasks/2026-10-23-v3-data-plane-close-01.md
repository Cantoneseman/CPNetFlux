# V3 range 独立写入
输入：0d4686cc21f1d8083c189119df576906ce419ae4；当前 codex/V3-DESKTOP-NETWORK-BINDING-01。
范围：同一 V3 引擎内将 range writer/CRC/统计按 attempt 独立持有；非重叠显式 offset 写可并发，checkpoint 仍单写；Entry 只保护租约/claim/attempt 与提交屏障。旧 attempt 接管前排空其写入并丢弃未验证 buffer，迟到 DATA 不可触碰新 attempt。完成前排空所有写入再校验/发布/关闭 FD。
非目标：不改 TLS/CRC/协议格式、网卡、pipeline 深度和 checkpoint 默认 batch=1；不提前实现 CPU 热点优化；不碰历史项目和系统服务。
锁契约：slot mutex 保序；Entry state mutex 不跨真实文件 I/O/checkpoint；独立 checkpoint mutex 保护 session/保存与定时写者。state 锁不能等待 slot/checkpoint 锁；接管先等旧 slot 再核对 state/generation。完成设置 closing、等待 active write 清零后提交。统计持有者不能是其他 lane 的栈。
受影响：persistent_tree_transfer.cpp/.h、必要的 FileIo 契约注释；独立 range writer 测试目标、观测 smoke/perf 脚本、CMake、路线与本任务证据。
验收：先用旧代码观察真实 pwrite 重叠测试失败；真实写探针只在独立测试二进制中注入，不向产品加测试入口。覆盖并发写、冲突 claim、attempt 接管/迟到写、写失败、提交屏障、buffered writer、断开和恢复；原 checkpoint SIGKILL 测试及完整 CTest。
性能：先 512 MiB 热缓存诊断，再固定 4 GiB 至少5组旧/候选交错，校验不计入 wall。其他集合检查退化≤5%。≥10%稳定改善才采用；若不达标，保留事实与最简单可接受实现，不能标成性能目标完成。用基线二进制与候选源码/二进制哈希明确区分结果；计时不改。
交付：深圳根目录串行开发/提交/push/readback；同提交同步上海、两端构建且关键二进制 SHA 相等才互传；Windows 只同步性能优化路线.md。保留两个无关 worker-state 未跟踪文档。

## 候选实现与正确性验证
每个 Claim 保留自己的共享 slot，接管不复用旧 slot 的可变 writer/统计；旧 attempt 的未验证缓冲尾部直接丢弃。checkpoint 独立串行，Entry state 不跨文件写入。提交先关闭写/claim 入口、等待 active write，然后在 checkpoint 锁内重新判断完整覆盖并提交/关闭 FD，不能用等待前的过期覆盖快照。
候选开关 CPNETFLUX_RANGE_WRITE_MODE=serial|parallel，性能门通过前默认 serial；两种均为同一 V3 协调器，不涉及历史协议/执行器。
真实 pwrite 入口探针只链接独立测试目标。旧程序在非重叠范围测试中最大并发入口为1，期望2，RED成立；候选达到2。该探针证明系统调用入口重叠，不代表物理磁盘并行度。覆盖冲突 claim、接管等待/迟到 DATA、范围几何变化拒绝、发布屏障、真实 pwrite 失败、各自 buffered writer、已缓冲未验证尾部丢弃以及非法模式提前拒绝，共9项通过。
完整 Release 构建后，CPNETFLUX_RANGE_WRITE_MODE=parallel CPNETFLUX_RANGE_CHECKPOINT_BATCH=16 CPNETFLUX_TEST_TOKEN=cpnetflux-test-token-only ctest --test-dir /tmp/cpnetflux-build-fd-release-01 --output-on-failure -j1：449项无失败，1项io_uring环境条件skip，148.21s。既有断开/恢复、checkpoint定时、SIGKILL门禁包含在完整套件内。
9项独立测试另在GCC ThreadSanitizer下batch=1通过。初次锁告警被CPNetFlux之外的最小程序复现：本机GCC11的steady wait_for使用pthread_cond_clockwait，libtsan不拦截该入口；测试探针改为受拦截的system_clock wait_until后无告警，未关闭sanitizer检查，未改产品单调时钟。该检查不覆盖io_uring或batch16后台线程。
原始日志：/tmp/cpnetflux-parallel-red.log、parallel-green.log、parallel-targeted.log、parallel-boundaries.log、parallel-ctest.log、parallel-tsan-final.log（后五项均在/tmp/cpnetflux-前缀下）；根因最小复现/tmp/cpnetflux-cond-tsan-repro.cpp与steady/system日志、/tmp/cpnetflux-parallel-tsan-diagnosis.txt。
测量脚本新增旧二进制对照、large/dense/mixed集合和逐次树SHA；计时边界未变，baseline缺失的新并发水位记null，水位取max不能求和。512MiB quick仅验证工具，不能作为采用结论。正式5组配对在本候选提交后执行；改动前基线二进制固定在/tmp/cpnetflux-pre-parallel-0d4686c。
