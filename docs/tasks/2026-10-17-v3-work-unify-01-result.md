# V3-WORK-UNIFY-01：共同工作租约契约验收

- 日期：2026-10-17（UTC+08:00）。状态：阶段 01 完成。
- 权威工作区：深圳 /root/projects/CPNetFlux，00 根目录单人串行、main；未启动其他角色或子代理。
- 输入提交：6777dd7f5ad9bd96f8d703e932bfdc67c7a1ab28。
- 实现提交：cbebce51e08bcdd986179e1f633885792f9bf762。已推送 git@github.com:Cantoneseman/CPNetFlux.git 的 main；push 返回 0，远端 refs/heads/main 回读完全相同。
- 证据根目录：/tmp/cpnetflux-runs/V3-WORK-UNIFY-01-20261017/。文档收尾提交和最终远端 SHA 在此目录 completion-receipt.json，避免自引用提交 SHA。

## 实际交付

work_execution.h 定义共同 WorkItem、WorkLease、WorkerContext、ConnectionEpoch。工作项包裹现有 PersistentFileIdentity，复用 file/range/generation/attempt/checksum 身份；scheduler::WorkItem 保留为规划投影，通过 bindScheduledWork 校验 task、路径、range、resume generation、retry 和 dispatchable 后绑定完整身份。阶段 01 未把 GlobalScheduler 接入 persistent worker。

DynamicFileQueue 仍是唯一 ready/lease/credit owner。活动租约绑定进程内唯一 leaseId、workerId、linkId、connection epoch、完整身份、idle deadline 和本地 IO 计数，遵循 claimed -> in_flight -> commit_pending -> completed。乱序、重复结果、旧 owner/epoch/generation/attempt、跨队列 handle 均拒绝。完成态 checksum 不当作源身份变化，但提交必须持有原始完整租约。

requestDrain 停止新 claim；本地 IO 归零且结果成功提交才释放 credit 并允许 cut/reopen。超时转 reclaimable，保留已有 IO；已过期 IO 结束只能排空，不能续活。disconnect 必须由调用者在关闭旧 transport 并停止 IO 后确认；requeue 必须等待此确认、只接受未提交租约、递增 attempt。已提交工作不能再次回收，reopen 递增 epoch；cancel 为终态，所有旧 handle 失效。缺失工作必须先由上层 manifest 对账，本接口不自行判断丢失结果是否已经在对端提交。

单文件上传/下载 sender 已通过 unified_file_transfer 调用共同 DynamicFileQueue + sendPersistentTree；动态目录上传及服务端下载复用相同 sender。sendPersistentTree 保存完整 lease handle，BEGIN/DATA/END 与结果接入共同迁移；negative FILE_RESULT 不再被队列记作成功。IO 计数仅覆盖本地同步 DATA frame 写入，不代表 TCP ACK、NIC 在途或链路 BDP。静态目录/接收端和 scheduler/pipeline 的外围 loop 尚未合并，属于阶段 02。

真实重派集成发现整文件 attempt 非零被 FILE_BEGIN metadata 校验拒绝。修复使用既有 40 字节扩展承载 attempt，未新增字段或版本；attempt=0 的原有编码不变，range 不完整仍拒绝。旧二进制安全拒绝新增整文件 retry 身份；不宣称旧端点支持自动重连或跨版本重派。

## 验证与证据

构建目录 /tmp/cpnetflux-runs/V3-DEV-PREFLIGHT-01-20261016/build，Release、BUILD_TESTS=ON、TLS=ON、IO_URING=ON、BUILD_DESKTOP=OFF，复用预检已核实依赖，未安装软件。

- TDD 红灯实际执行：共同契约 21 项失败；edge/adapter 5 项失败；审查补测揭示跨队列 handle 和未确认关闭即重派等 7 项失败；整文件 attempt 的协议/真实 sender 集成 2 项失败。原始 red-*.log、review-red.log、final-focused.log 均保留。
- cmake --build /tmp/cpnetflux-runs/V3-DEV-PREFLIGHT-01-20261016/build -j 2：最终返回 0。
- ctest --test-dir 同上 -R 'DynamicFileQueue|WorkLease|WorkExecution|PersistentDataSession|PersistentTree|PersistentDirectoryBatch|Scheduler|Range|SessionControlTest' --output-on-failure：实际匹配并通过 96 项。
- ctest --test-dir 同上 --output-on-failure -j 2：最终 329/329 通过，0 失败，0 内部 GTest skip，约 42.8 秒。相对输入 299 项新增 30 项。pass3-summary.json、pass3-full-ctest.log、final-LastTest.log 和 final-test-inventory.json 为最终依据。
- 全量首轮有两项 smoke 在 Python import 时退出，原因是本轮 shell 漏注入 CPNETFLUX_TEST_TOKEN；其余 327 项通过。补齐随机临时进程环境后全量 329 项通过，失败日志 pass2-full-ctest.log 保留。未修改测试来跳过认证，token 值不落仓库或交付日志。
- cpnetflux_v3_stability_matrix --rounds 8 在最终全量中通过：TLS/CRC32C/dynamic range/resume、128 MiB dense、256 MiB mixed，以及 TLS control/data/handshake RST。真实 TCP RST 测试确认只回收未提交租约；真实 sender/receiver 测试确认重开 epoch=2、attempt=1 完成且旧 handle 拒绝。
- git diff --check 与明确 staged 文件检查通过；实现 SHA、push 和远端 SHA 见 implementation-backup-receipt.json。implementation-verification.json 记录受测文件哈希及 00 自审结论，没有冒称独立 reviewer 验收。

## 状态保护与交接

两份 01B 未跟踪文件保持 input-snapshot.json 中 SHA-256，四个既有 worktree 未编辑/合并/删除。BOARD 只添加本轮当前区；原 dirty 内容保存在 protected-board-before.md，工作文件去掉本轮前缀后逐字节一致，通过部分 index 条目仅提交新增区。不提交旧 dirty、凭据、构建产物、payload 或原始大日志。Windows 仅接收小型证据查看副本。

本阶段没有部署、重启或修改既有服务，没有触碰历史 GridFlux-Beta、CPSS(DCC) 或历史 payload。未做跨主机或性能实验；上海反向深圳 2121 的预检缺口另留实验准入处理。按用户意见，GridFTP proxy 在实际使用前刷新；本轮 loopback 的 GridFTP-style CPNetFlux smoke 不调用外部 GridFTP/GSI。

下一步 V3-WORK-UNIFY-02：在同一 ready/lease 契约上合并完整 worker loop，接入 global scheduler，补齐连接 owner/transport teardown、manifest 对账后的缺失工作重派和 watchdog 自动驱动，增加实际 worker/epoch/调度模式观测；然后再进入 LinkSet。当前 persistent + global scheduler 显式拒绝仍保留，未启动阶段 02。阶段 01 通过不代表十物理链路、100Gbps/1Tbps、跨域或生产 readiness。

回滚仅对实现提交执行 git revert，并先保留当前 dirty；不 reset/clean、不删除实验数据或停止服务。
