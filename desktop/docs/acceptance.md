# 接口验收与最小测试计划

本轮门禁只验设计 schema 与场景向量，不证明 agent、UI、取消或真实恢复已经实现。
命令：python3 -B desktop/tests/test_contract.py（依赖 jsonschema，深圳现有）；git diff --cached --check -- desktop。

| 门禁 | 观察行为 | 本轮/后续 |
|---|---|---|
| schema | 所有指定方法正例通过；未知方法/多余字段/明文 token 拒绝 | 本轮 |
| 未知值 | null totals/ETA、unknown mode 保留，不伪造 0 | 本轮向量；后续 UI |
| completed | 无 commit/scan/计数证据拒绝；部分 range 不变文件完成 | 本轮形状+语义检查；后续真实 |
| fallback | requested v2 actual v1 必须理由，不改 checksum/TLS | 本轮；后续 backend |
| 订阅 | 初次快照；游标重放；epoch 重置；重复/缺口；旧 attempt 不覆盖当前 | 本轮消息向量；后续真实 socket 重连 |
| 取消 | 活跃任务 accepted+cancelling 不是 cancelled；completed→already_terminal | 本轮向量；后续断线/commit 竞态 |
| 恢复 | eligible 必须 checkpoint；unknown/ineligible 拒绝；新 attempt | 本轮向量；后续损坏/源变化/旧 manifest |
| 暂停 | unsupported 错误不改变原任务 | 本轮错误向量；后续真实 API |
| 认证 | auth_failed/token expired/keyring locked；不得出现凭据 | 本轮向量；后续 libsecret/TLS |
| 目录权限 | permission_denied；root 逃逸拒绝；未知权限不能 passed | 本轮；后续受控服务端 |
| 数据端口 | 控制成功但 data_port 失败 ready=false | 本轮；后续节点测试 |
| 生命周期 | UI退出后任务存活，重开同步；agent重启不重复启动 | 后续实现测试，未执行 |

下一阶段顺序：00 收口底层字段/取消/安全注入→冻结 draft-2→准备 Qt/libsecret 运行环境→实现离线 shell 与 Unix socket agent 原型→受控 loopback 真实任务接入。真实回归须覆盖上传/下载、动态小文件、大文件 range、cancel/resume、V1 fallback 和两级 TLS；不启动跨云性能实验。
