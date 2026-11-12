# 下一开发项：V3目录项与完整树语义

状态：ENGINEERING_CORRECTNESS_CLOSED。阶段78两端同提交/同配置/四SHA，双端完整回归各825通过、3项原生RDMA环境跳过、0失败；新A/B/C尚未运行。阶段77混合目录文件全对而empty/丢失，必须先补齐这个能力缺口，再恢复上机验收；本文件区分代码、正确性与跨机性能验收，不产生新性能成绩。用户总体目标OPEN，C和上机NOT_READY。

## 范围和非目标

输入运行时42718ac，最新原始实测输入b6aee7216bda89c374acaea956a54bcdcc2995cc。当前V3 scanPersistentTree调用会跳过directory的scanLocalTree，TreeManifest只有files。普通非空目录由文件父路径隐式创建，无法表示空目录/只有目录的树；客户端result=pass当前只证明文件合同。研究基线已公开该边界，不能把这次发现说成本轮回归。

本项支持普通目录项、零文件目录树及其取消/恢复，明确文件/目录两类完成事实；保留V3-only、CRC/verified_chunks、原子文件发布、预算和租约。不加入权限/owner/group/xattr/ACL、symlink/hardlink复制、压缩/微批/RDMA、动态策略或新吞吐调参。

## 实现次序及受影响面

1. 先固定V3目录任务语义：目录的相对路径、身份/完成事实、协商能力、旧manifest输入读取行为与边界。新任务需要目录语义而对端不支持时显式拒绝/报告，不能静默遗漏，也不能降级到旧执行器。目录不计为payload字节，不伪装成零长普通文件。明确摘要分别统计文件与目录，避免继续把文件PASS当完整树PASS。
2. 扩展树扫描/数据模型/manifest和V3已有元数据通路。目录项包含空目录并保留父子关系；只使用有界、分批、可校验的元数据，不全局放大控制帧限额或无限缓存路径；单大文件无目录项时应避免额外逐chunk工作。预计涉及include/cpnetflux/core/tree、src/core/tree、V3 persistent metadata/frame/控制能力及src/core/io/tree_transfer_client.cpp；实际wire编码在代码设计时确定，不能从本计划当成已定协议。
3. 在接收端按安全父路径创建和确认目录；拒绝绝对路径、..、symlink和普通文件冲突，处理TOCTOU、重复目录、缺父目录、取消后重试及服务重启。只操作可证明属于任务的路径，不用远端输入直接拼接递归mkdir/删除。保持文件发布与目录完成的区别，不承诺整个目录树原子替换。
4. 将目录事实纳入checkpoint和恢复：已存在的安全目录可幂等验证，缺失目录可补建，冲突必须失败；旧file-only清单作为历史读取/迁移输入，不能把缺少目录字段直接解释为目录均已完成。全程预算、取消、租约释放与owned清理可核验。
5. 验证后固定提交并双端同步，再重新执行完整验收；运行时改变后阶段75数字只保留历史，不能无验证搬到新二进制。目录正确性优先，小文件完成时间差距另建成本假设，当前不启动微批或参数扫描。

## 验收

先单元/协议/manifest/恢复测试：纯空目录、空根、仅嵌套目录、零长文件与同名类型冲突、混合树、重复项、越界/超额元数据、symlink/路径逃逸、旧清单、能力不支持、取消/重启后恢复。必须证明原失败目录确实保留，不能只镜像实现写测试。

之后两端同提交/构建/四SHA，在明确有效的受控实验窗口验证两个方向实际树语义、内容/资源/网络/清理；不复用已结束77的lease、不延长本轮90分钟、当前不再发载。重新固定最强合法配置的五对大文件与dense/tiny/mixed各3次、反向/安全场景；全部通过后同常驻服务PID>=1800秒且>=100完整跨机任务，以及真实双机data/control断连、取消恢复、重启、TLS、坏数据/checkpoint拒绝。任何缺项NOT_RUN；8/64GiB、真实磁盘持久化、native verbs、生产身份管理仍单独说明。

## 本轮交接

阶段75 A：31.075/12.352Gbps；阶段77 dense：0.399/0.945秒，tiny：3.504/2.561秒（CP/Grid各3次）；mixed514文件256MiB全对但空目录丢失。83项只读审计通过的是证据一致性，B/C仍未通过。双端RX/TX1024/1024及原服务保持，无owned payload/挂载/代理/临时监听。源代码/默认未改，产品回归复用阶段68双端156项，不称新运行。

## 阶段78实施范围（修改前固定）

输入提交1974c05c1466155098254751ce425e746300c45d，唯一写入者R770-1。先以真实loopback上传/下载证明空目录缺失，再实现：普通目录扫描、独立directories清单及完成事实、安全fd-relative创建、协商DIRECTORIES=1的V3目录元数据。目录使用独立类型和确认，保留V3工作租约；不伪造零长文件。每条路径最多4096字节，每任务最多65536目录、16MiB目录路径，超额明确失败；DATA/控制帧原限额保持。目录重放逐项验证，不以旧Completed或目录数量代替实际安全检查。旧file-only清单仍可读取，恢复需补齐源端扫描事实或拒绝不明确输入。

受影响文件：tree_scan/tree_manifest头与实现及测试；新增persistent_directory元数据/安全创建模块与测试；frame头/验证、work_execution/DynamicFileQueue目录类型、persistent_tree_transfer和tree_transfer_client、control_server能力/目录任务接线、CMake测试登记、历史edge smoke预期及新目录语义smoke。若范围变化先补记录。

验收：空根、纯嵌套空目录、混合树在V3上传与下载完整保留；双向恢复及缺失目录重建，类型冲突/路径逃逸/symlink/损坏元数据/重复或超额条目/旧能力拒绝；相关CTest并完整回归。跨机之前固定提交、推送、两端同配置构建和四个二进制SHA一致。本阶段不启用ring修改/跨机负载，不宣称性能与长期稳定性门通过；A/B/C需随后重新验收。

证据目录：/home/Su/experiments/CPNetFlux/2026-11-13-v3-directory-semantics-78。完成时记录真实测试、提交/远端SHA、剩余项；不修改阶段75/76/77冻结证据。

阶段78设计补充：目录元数据固定在lane 0，其他lane只发送零目录结束帧，避免目录路径预算随lane数放大及跨lane重复路径；普通文件的lane与range分配保持。每条元数据携带覆盖header+transferId+path的CRC32C、独立确认，目录父项必须先出现。目录WorkKind独立于File，仅复用V3 lease状态机，不进入文件事务/字节统计。初次正确性版本每次确认一个目录，WAN多目录延迟不作性能承诺。新schema2清单保存directories，schema1仅历史读取；上传恢复通过明确源端重扫升级，下载旧清单拒绝并要求新任务。

恢复安全测试另发现旧配置解析会提前create_directories并跟随目标祖先symlink；该行为发生在V3安全检查之前，已用失败用例复现。受影响面增加src/config/tree_transfer_options.cpp及其单元测试，目录准备移入协商成功后的安全V3路径。不是针对默认网络或数据路径的调参。

阶段78整套回归结果待闭合：首轮821项有4项失败（2项测试令牌未提供、旧schema oracle、旧安全阶段断言）；相关修正后63项定向通过。最终候选826项有1项失败：source_port_plan_smoke的early-control-reset2（512B边界）报告file begin identity mismatch；3项原生RDMA测试跳过。该失败作为恢复边界继续调查，不以重试通过覆盖、不放松身份校验、不启动跨机负载；先补身份字段诊断和保留原始证据，再作定向复现。

恢复定向复现：trace开启12/12通过；关闭trace第18次出现重复file ID，第26次捕获transfer_id变化（路径/大小/mtime/chunk/generation相同）。服务首个终止原因是duplicate active range。源码顺序证实：sendPersistentTree在返回外层batch.failChannel前已经recover工作队列，健康lane可在旧batch占用未释放时领到attempt1；后续重建同batch生成新transferId只是下游症状。修复范围增加persistent_directory_batch头/实现与实际socket+共享queue/batch的确定性回归。先验证旧顺序必败，再将旧占用清理放在重新派发之前；身份检查、CRC、重试预算/deadline保持。

恢复修复结果：确定性回归先RED（旧顺序duplicate active range），修复后14/14相关单测通过；同一512B边界、原重试预算2和10秒deadline，无trace独立真实客户端64/64通过（identity-fixed-1/result.json）。新增releaseFailedWork只释放匹配owner/完整identity的旧占用，不detach lane；先通知外部清理，再recover入ready。未放宽身份/CRC或增加重试预算。完整828项回归进行中，双端验收与新A/B/C仍OPEN。

恢复候选完整828项出现2项拒绝时延回归：reconnect_smoke及committed_output_integrity_audit_smoke的corrupt场景等待deadline而非3秒内拒绝，原始日志ctest-final-recovery.log保留。定位：server新TransportError callback对接收路径也返回OK，绕开旧fileEvent返回TransportError触发的finishTransport完整性审计。修复限制为仅server sender(!upload)使用releaseFailedWork；接收保持原有传播与审计合同，不改3秒断言、deadline/重试预算。之后重跑定向与完整回归。

R770-1最终完整回归：ctest-final-sender-only.log/status.json记录828项，825通过、3项原生RDMA环境跳过、0失败，328.60秒。与目录语义/恢复相关的最终定向4/4通过。此前两项拒绝时延回归完整保留；未用后续通过覆盖失败记录。最终二进制64/64次早期控制断连重验通过（identity-fixed-final/result.json），随后固定提交并同步R770-2；跨机负载须随后在新受控实验窗口重新准入。

最终双端验收通过：运行时dbb2d6bac86b29914dbea655b29660937267e38f已推送/回读一致。R2完整828项825通过、3跳过、0失败（328.34秒），四二进制/构建配置与R1逐项相等。最终结果与边界见[78结果](2026-11-13-v3-directory-semantics-78-result.md)及[冻结](2026-11-13-v3-directory-semantics-78-freeze.json)。代码正确性闭合，正式比较/稳定性/故障组和上机状态仍OPEN/NOT_READY。
