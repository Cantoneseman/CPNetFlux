# V3-UNIFIED-01 第一阶段证据
状态：range 安全修复通过；V1 能力迁移和删除尚未完成，实验尚未启动。
输入 5f361c9be504fd64a0609442bf6c32e5574ddcf2；证据根 /tmp/cpnetflux-runs/V3-UNIFIED-01。

修复：同文件不同 range 可由不同通道独占；range 元数据同一性、完整分区、重叠及回放拒绝；提交前覆盖和 checksum 验证；resume 重校验 partial；registry 生命周期随 batch 回收，消除栈 timing 指针；range 结果前 checkpoint flush；错误准备回复；TLS sender 同一 SSL 读写串行。
TDD：red-direct.log 两项行为测试在输入实现失败，targeted-2.log 修复后 45 项通过（含断线+损坏 checkpoint 恢复、双通道同文件）。
全量：ctest-full-2.log，277 项中 276 pass、1 io_uring skip、0 fail，71.89秒。测试 token 临时随机生成，不入日志/Git。
依序 loopback-dense/mixed/large.log：128×1MiB、57文件 mixed、129MiB/4通道 range 上传下载独立 SHA-256/committed manifest 通过。只验证本机正确性，不宣称 WAN 性能收益。

alpha demo 的 --max-files 故障注入仍明确走旧路径，这是下一阶段迁移项，不能以全绿 CTest 声称 V1 已退役。
GSI：深圳代理刷新后有效期43200秒；上海既有匿名 GridFTP服务保留，真实 GSI 对照认证及端点配置仍待核验。
下一阶段：迁移单文件上传/下载、IO/同步/预分配、压缩和 max-files 故障注入到共用 V3；完全覆盖后独立删除 V1。失败保留现阶段已验证提交。
