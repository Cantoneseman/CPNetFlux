# V1-RETIRE-01 结果
输入 52d02dfab2d8a8da522e3b7b47f85df4fcee3ded；源码 /tmp/cpnetflux-runs/V1-RETIRE-01/src。
已删除：旧单文件状态机（前序迁移提交）、SessionInit/ChunkComplete wire types、ChunkComplete 32/48-byte codec、旧 metadata resume 模式与 fallback理由。
保留：单文件 CLI/STOR/RETR薄适配、per-file/pipeline/global调度策略、V2 manifest读写和CRC/TLS/压缩/IO/恢复/提交策略。所有策略调用同一V3事务引擎；旧wire明确拒绝，成对升级。
验收：新增源审计 gate；旧type5/7在输入实现返回成功，red检查退出1；修复后拒绝。targeted.log 59项通过。全量ctest-full.log在新增源审计门前280项中279通过1环境skip；源审计门另运行通过。dense/mixed/large回归同目录loopback日志，独立SHA与manifest均通过。实验尚未启动。
未把V3版本称生产ready；真实io_uring运行仍为环境skip；公网对照效果待实验。
回滚使用前序GitHub明确提交，禁止恢复旧wire而只更新一端。
