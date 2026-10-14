# V3-COMPOSITION-01：安全持久会话组合回归

- 状态：done；结果见 docs/tasks/2026-10-14-v3-composition-01-result.md
- 输入提交：5d676dccc98e9ae608c63e47a2e41181e7439e83
- 权威源码：深圳 /root/projects/CPNetFlux
- 路线决策：docs/DECISIONS/2026-10-14-v3-stable-composition.md

## 目标

为真实稳定的桌面传输路径补足组合回归，验证持久数据连接可与动态元数据队列、大文件 range、CRC32C、控制面 TLS、数据面 TLS 和已提交任务的幂等 resume 正确协作。若测试在输入版本失败，先定位并修复根因，再扩大验证。

## 范围

1. 在 loopback 稳定矩阵增加 TLS 持久目录用例，至少覆盖一个超过自适应阈值的大文件与小文件混合。
2. 双向传输后独立比对文件集合、SHA-256、服务端提交 manifest 和客户端 download manifest。
3. 对已提交上传和下载重复执行 --resume，验证不会重复提交、覆盖已提交数据或伪报新增 payload。
4. 对测试暴露的 TLS、range、manifest、取消/回收问题只做最小根因修复，并增加相应的 RED/GREEN 回归。
5. 完成本阶段定向门禁与全量 CTest；记录 skip/环境限制，不扩大性能实验。

## 非目标

- 不在本阶段合并 global scheduler、control pipeline 与持久数据 worker；这些由 V3-WORKER-STATE-01 单独实现和验收。
- 不改变协议语义或默认策略，不删除已有功能，不启动 GridFTP/GSI 或跨主机性能实验。
- 不把本地 loopback 结果作为跨域稳定性或性能证据。
- 不修改桌面 UI、agent RPC、部署工具或用户凭据。

## 验收

- RED：新增组合用例在修复前能证明对应行为缺失；若原实现已通过，则明确记录为现有能力的回归锁定，不声称生产代码修复。
- cmake --build <build> -j2 通过。
- ctest --test-dir <build> -R cpnetflux_v3_stability_matrix --output-on-failure 通过，双向 TLS+CRC32C+动态 range hash/manifest 正确，幂等 resume 成功。
- 完整 ctest --test-dir <build> --output-on-failure 通过；环境可选项明确 skip。依赖外部 token 的用例通过临时环境变量运行，凭据不得落日志或 Git。
- 代码与测试变更只提交明确文件，push 到已核实的 CPNetFlux remote 并回读远端 SHA。

## 允许修改路径

- tools/test/run_v3_stability_matrix.py
- CMakeLists.txt
- src/core/io/framed_data_socket.cpp
- src/core/io/persistent_data_session.cpp
- src/core/io/persistent_tree_transfer.cpp
- src/core/io/tree_transfer_client.cpp
- include/cpnetflux/core/io/persistent_data_session.h
- include/cpnetflux/core/io/persistent_tree_transfer.h
- tests/unit/（仅相关行为测试）
- docs/tasks/2026-10-14-v3-composition-01.md
- docs/tasks/2026-10-14-v3-composition-01-result.md
- docs/DECISIONS/2026-10-14-v3-stable-composition.md
- docs/coordination/BOARD.md

## 回滚

按本任务提交逐项执行 git revert。保留阶段前已提交的事务引擎、Linux 初始化和桌面节点导入。测试 payload 与证据放在 /tmp/cpnetflux-runs/V3-COMPOSITION-01；不清理任务外目录。
