# V3-COMPOSITION-02：持久传输压缩决策可观测

- 状态：completed
- 输入提交：31ed679bde7a128ec2d15d791a9c26750d86f782
- 代码提交：66277c736d52448903b710b3218934c48620909a
- 权威源码：深圳 /root/projects/CPNetFlux
- 前置阶段：V3-COMPOSITION-01

## 目标

让持久 V3 上传可以按帧收益选择压缩或原样发送，并将策略和实际选择写入文件级及任务级结构化状态；压缩不得改变 TLS、CRC32C、range、resume 和提交语义。

## 范围

1. 移除 persistent tree 对 --compression auto 的静默 per-file 降级，给 V3 sender 接入现有收益判断。
2. 对压缩帧、收益不足的原始帧、压缩失败分别计数，并记录每个文件的实际决策。
3. 增加混合可压缩/不可压缩 loopback 回归，校验传输 hash、manifest、TLS、checksum、range 和幂等 resume。
4. 完成构建、稳定矩阵、全量 CTest、DEB 元数据检查，并提交推送。

## 非目标

- 不实现 global scheduler、控制预取 pipeline 与持久 worker 的运行时伸缩；后续 V3-WORKER-STATE-01 单独处理。
- 不改变服务端下载压缩协商、协议帧语义、默认压缩策略或桌面端。
- 不开展跨主机/吞吐实验，不将 loopback 结果作为性能证据。

## 验收

- 新回归在当前代码上因 persistent mode 拒绝 --compression auto 或缺少决策计数而失败。
- GREEN 中 persistent V3、TLS required、CRC32C、动态 range、resume 均保持；混合文件至少观察到压缩帧和 raw fallback，hash 和 manifest 正确。
- JSON 包含策略、总计数和逐文件压缩决策；resume 的实际 payload 仍为 0。
- cmake --build、cpnetflux_v3_stability_matrix、全量 CTest 和 CPack DEB 检查通过。

## 允许修改路径

- include/cpnetflux/core/io/persistent_tree_transfer.h
- src/core/io/persistent_tree_transfer.cpp
- src/core/io/tree_transfer_client.cpp
- tools/test/run_v3_stability_matrix.py
- docs/tasks/2026-10-14-v3-composition-02.md
- docs/coordination/BOARD.md
- docs/tasks/2026-10-14-v3-composition-02-result.md

## 结果

验收通过；测试命令、DEB SHA256、远端回读及限制见 [V3-COMPOSITION-02 结果](2026-10-14-v3-composition-02-result.md)。下一阶段仍需完成 V3-WORKER-STATE-01；本阶段不代表整个 V3 已达到公开稳定版。

