# 阶段60：保持性能、闭环设计、GridFTP复测与稳定性

日期2026-11-12；输入a1be2760d90bdecd3163d25a3c937f5c13840553。
用户最新目标：保持现有性能，完成已有开发阶段的未闭环项，再与既有“配置和实验”聊天
01a1a253-6ddf-7f71-bfd7-0eec5550a10f重做GridFTP对比，随后稳定性，准备上机实测。
这取代继续追逐40Gbps的本轮优先级，M2旧结果不改、不冒称通过。

## 修改前范围

范围：审查实际运行主线/已做候选/延期设计，修复已发现回归fixture/启动失败清理缺口，
修正旧的三阶段预算指引，冻结可测版本并运行相关回归；串行交接配置和实验，先比较再稳定性。
非目标：不新增性能机制/线程池/缓存后端，不晋级未经验证默认，不启动RDMA/微批/策略记忆，
不改系统/网卡/全局cache/swap，不删除历史证据，不将当前阶段包装为生产readiness。
受影响：Task59合同/结果；tools/test/run_v3_corrupt_partial_checkpoint_smoke.py、
新增tools/test/run_corrupt_checkpoint_cleanup_test.py、CMakeLists.txt；
docs/V3_TRANSFER_STAGES.md、docs/ROADMAP.md、docs/README.md、README.md；
本计划/阶段结果、docs/DECISIONS/2026-11-12-field-trial-freeze.md；
docs/release/ALPHA_READINESS.md仅加旧版范围提示，不重写历史。
本阶段运行源代码只由R770-1修改；R2接收固定提交。交接前完成提交推送并停止写根目录。

## 阶段闭环审查与判定

- 三阶段/预算、大帧和接收buffer：已有实现/预算/取消/恢复门，工程完成，性能按原结果解释。
  指南65536固定预算项过时，需与当前实际framed capacity计算一致；补文档及代码对应验证。
- CRC、V3 attempt/lease/checkpoint/发布恢复：已完成明确范围实现和回归；不是任意掉电保证。
- kernel pacing/private tmpfs/native监督器：已做机制、默认off与相关回归；Task58资源停止保留，
  不能将缺失正式吞吐当作机制半实现，也不以本次用户保持性能重启M2。
- S1 cached和S2 serializer/zerocopy等：已有显式路径或诊断、默认选择已定；默认晋级/新集成延期。
- 微批/RDMA/统一工作单元/策略记忆：DEFERRED/PAUSED，未作为本轮上线功能，不补建新研究阶段。
- 唯一新发现未闭环实现：损坏checkpoint回归只清理输入源和partial，download复制源遗留；
  prepare/start_server/start_proxy在try外，启动失败不保证stop。先RED再补最小清理修复。

## 验收与后续序列

1. cleanup行为测试覆盖复制后启动失败、已有文件拒绝/保留、源生成失败与替换对象保护；
   真实64MiB损坏checkpoint上传/下载仍拒绝坏checkpoint，全部自有数据清理，日志/manifest保留。
   相关CMake/CTest含V3稳定性组合、预算、取消/deadline、恢复/发布；固定代码再同步R2验证。
2. 冻结代码/配置/三端点程序SHA与helper、默认差异、当前性能边界和回退；GitHub推送回读。
3. 用专用聊天入口续接既有配置和实验，附固定提交和验收合同，记录真实turn/session。
   已检查专用入口/shell/codex CLI；未找到本仓库或本地的DISPATCH.md/ROSTER，
   依据用户明确thread ID与当前AGENTS单写者规则串行交接，不猜旧角色prompt。
4. 新比较合同保留同版本/数据/安全语义/完整任务计时、SHA、零host swap与网络硬门。
   使用最新强配置，保留其完整必要准备；GridFTP先刷新proxy、验证≥1h有效期/信任和双向smoke。
   大文件及小/混合文件、双向、配对重复；规模按真实内存与空间定，最多8GiB，不使用64GiB。
   如果某规模不准入则标NOT_RUN，不改门或缩规模伪装原规模通过；不复用旧失败矩阵挑样本。
5. 比较完成/评估通过后在选定部署配置上做单独稳定性：双向重复、目录/空文件、校验、
   取消/断连/恢复、服务重启与TLS受影响模式；至少30分钟/100个完整任务，峰值每端≤8GiB，
   不积累payload；记录内容/终态/FD/线程/RSS/清理/host swap/NIC、失败和吞吐漂移。
   任何完整性/超时/遗留/资源/网络硬门失败停止并保留证据，不把loopback代替双机负载。
6. 总指挥只读跟踪并审核原始证据；真实问题再串行收回开发，不能两聊天同时改根目录/Git。

本阶段完成要求：上述开发闭环、可比较成绩与统计、稳定性证据和上机启动/回退说明全部成立。
任一关键实验阻塞或失败时如实列出，保持目标未完成，不用文档绿色替代实际实验。
证据根/home/Su/experiments/CPNetFlux/2026-11-12-field-trial-closeout-60。

## 开发执行进度

R1修复与5/5清理回归通过，相关278/278通过（109.07s），无skip；
真实64MiB损坏checkpoint双向拒绝保持，复制源/输入/partial均清理，诊断保留。
三阶段指南按frame_capacity_bytes修正，旧Alpha readiness标为历史快照。
核心src/include与四二进制未变，没有新性能候选或默认晋级。
下一步固定实现提交/推送，再R2同版本构建/278项验收，最后串行交接实验。
开发验收不替代新GridFTP比较或30分钟双机稳定性。

## 实验交接的补充约束

阶段最终冻结和开发结果新增同前缀-freeze.json及-result.md，受影响文件在此固定。
已停止的旧矩阵不重跑；本轮是用户明确要求的新比较/稳定性合同，沿用原资源/完整性门。
内存不足时，不得全局drop_caches或修改swap/系统策略；仅可对明确属于本项目、已停用、
无打开或mmap引用的历史常规单链接payload做一次有记录的POSIX_FADV_DONTNEED提示，
用固定少数绝对路径/O_NOFOLLOW、uid/dev/inode/size/mtime证明范围及文件不变，
不删除/截断数据，不扩展到其他项目/活跃源/新目标，不为过门反复循环释放。
这是实验环境准备，不是重启Task59缓存后端开发；释放后仍完整执行原资源门。
R1可核对Task59 cache-audit-r1.json中列出的旧2026-11-05输出与2026-11-02源；
R2没有完整归属表，须定向核对同类确切文件，不能据R1结果推断R2。
主要比较应覆盖1GiB与资源允许的8GiB最新强配置；不新增不受支持的4GiB私有profile。
小文件/混合数据按现有CLI支持使用有界普通tmpfs，不强塞私有大文件专用驱动。
最新强配置和保守部署备选分开报告，不以换成更慢易过门路径冒充原配置稳定。
稳定性除了选定配置的30分钟/100任务，还须有常驻服务多任务、FD/线程/RSS趋势，
不能只反复重启服务掩盖泄漏；故障恢复/损坏数据测试单列，不把预期失败混进吞吐。
用户消息授权与指定聊天协作；接手者无需向本聊天发消息，总指挥用状态工具/证据根读取结果。

## 开发阶段闭环

实现b183cdf5530a2e6e21d13f909d070d68d5a73b5e已备份；双端同配置构建、各278/278
回归通过（R1 109.07s、R2 139.74s），没有skip。各自真实损坏checkpoint回归的新增fixture
无输入/复制源/partial遗留，诊断保留；旧目录R1 73/R2 23个未删除。
原服务身份/host pswpout保持，四二进制与Task58相同，src/include无新改动。
开发阶段已闭环；[结果](2026-11-12-field-trial-closeout-60-result.md)和[冻结](2026-11-12-field-trial-closeout-60-freeze.json)
保存精确范围。GridFTP比较及双机长时稳定性仍待实际执行，不是本次开发回归已通过。
