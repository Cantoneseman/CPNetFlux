# DIR-ASYNC-CONTROL-03：真正多控制通道与有界 pending

输入提交：d362ea2360bf0cbfe3626a9c93734368a74ec0b6
目标 worktree：/tmp/cpnetflux-runs/DIR-ASYNC-CONTROL-03/src
开发分支：codex/DIR-ASYNC-CONTROL-03

## 目标
仅优化目录传输控制面。修正 runPipelinedTreeScheduler 当前始终使用 slots[0] 的问题，建立 depth 0/1/2/4 可配置的独立控制连接池；在当前数据传输期间准备后续文件控制请求，缩短逐文件控制往返。

## 非目标
不改数据帧、校验算法、manifest 格式、resume 语义、scheduler 策略、压缩、io_uring、单文件 CLI、GridFTP 代码或云端历史目录；不启动 WAN 长实验。

## 实现要求
1. 每个 pipeline slot 对应独立控制连接，slot 生命周期独占，禁止跨 slot 读写。
2. pending 队列有界，深度由 controlPipelineDepth（0/1/2/4）控制；队列满时有明确回退/拒绝，不无限增长。
3. candidate 必须带 file index、generation、slot identity；切换/复用前校验，不匹配直接失败并清理。
4. 为 control_prepare、transfer_complete_wait 增加同一单调时钟下的阶段计时；未发生的阶段用不适用/缺失语义，不能伪造零值。
5. 超时、取消、控制断线时，关闭相关数据 listener、唤醒/回收 pending 和 worker，保证无悬挂线程和 slot 泄漏。
6. depth=0 行为保持兼容；hash、manifest、resume、错误/重试语义不变。
7. 增加单元/loopback 测试：多 slot 分配与独占、generation 拒绝、队列上限、超时/取消/断线回收，以及目录上传/下载 hash、manifest、resume 和错误路径。

## 允许修改
src/core/io/tree_transfer_client.cpp、相关 include/cpnetflux/core/io/*、
必要 metrics/schema 文件、对应 tests/unit/* 和 tools/test/*；可新增短实验脚本
tools/experiments/gridftp_compare/run_async_control_depth_sweep.sh 及其 Python 比较器/门禁。
不得改动用户工作树或 /root/projects/GridFlux-Beta、/root/projects/CPSS(DCC)。

## 验收
- Release CMake build 成功。
- 定向 CTest 覆盖 pipeline/目录/resume/manifest/error，全部通过。
- Python 门禁和 shell -n 通过。
- git diff --check（允许现有 CRLF）通过；只提交审查过的白名单文件。
- 提交并推送本分支，回读远端 refs/heads/codex/DIR-ASYNC-CONTROL-03 SHA。
- 交接记录输入 SHA、输出 SHA、测试命令/结果、未解决限制；不得声称短实验通过，短实验脚本只供用户手动执行。

## R2026-10-09.1 / v2：本任务执行契约

用户已明确解除迁移暂停门并授权目录控制面改造；这不等于 CLOUD-GITHUB-01 验收完成。旧迁移门对本任务的暂停、DIR-ASYNC-CONTROL-02 的单控制通道设计及扩大矩阵计划在本任务中 superseded。仅在本隔离分支实现；Windows 不修改源码，live root 不切分支。

- depth=N 表示全局一条流水线数据 lane 最多 N 个 pending 文件；池为 N+1 条独立控制连接。另有 file_parallelism-1 个普通数据 worker；数据并发上限不增加。
- 每个 slot 持有唯一候选指针、slotIndex 与逐次递增 generation；成功文件读完控制完成回复才释放。pending 只在轮到它时 join，不能在当前文件 data end 处等待整个队列。
- 取消先 shutdown 所有本任务控制连接，准备线程和文件 worker join 后再 reset/销毁。现有 30s 控制回复超时保留；DNS、TCP connect 和 TLS handshake 原有阻塞行为不在本次承诺中。
- control_prepare 是本进程候选控制准备调用的累计耗时；普通路径覆盖 control acquisition 到 passive prelude。transfer_complete_wait 是文件数据函数返回后等待控制终态回复的累计耗时，与 phase_c_wait 同边界，不包含数据 COMPLETE 内部等待。并发累计时间不能视作 run wall 的可相加分解。count=0 新 seconds 字段为 null，phase-timing=off 不输出。
- depth>0 仍拒绝 resume/max-files；depth0 的 resume/manifest 行为必须回归，不宣称新流水线支持 resume。
- 02 既有聊天独占实验 runner/comparator/depth_sweep 和相关测试；04 独占 tools/test 的 pipeline 行为测试。00 负责 C++、unit、CMake、任务/任务板和最终明确文件提交。共享 index 仅00操作。
- 短实验固定 tree_dense_128MiB、fp1/n1、两个方向、3 次、CPNetFlux depth0/1/2/4与真实GridFTP（30 case），不截取前N项代替矩阵。由用户手动启动，运行前检查两端空间与独立目录。文件摊销耗时=明确的case wall/128，与客户端阶段计时分列。
- 验收以真正连接数、跨连接重叠、队列界限、hash/manifest、失败收敛为准；不以编译或假状态机测试证明并发正确。短实验未显示平均文件耗时下降前，不扩大实验。

允许修改补充：src/config/tree_transfer_options.cpp、tests/unit/tree_transfer_options_test.cpp、tests/unit/tree_pipeline_identity_test.cpp、tools/experiments/gridftp_compare/runner.py及短矩阵门禁；允许在此worktree更新BOARD和本任务交付记录，不修改其他角色回执。
