# desktop-agent JSON-RPC draft-1

本草案使用 JSON Schema Draft-07，无外部引用；尚未冻结为后端 API。

## 消息与权限
Unix stream socket 为 $XDG_RUNTIME_DIR/cpnetflux/agent.sock。父目录 0700、socket 0600；验证 SO_PEERCRED 同 UID。runtime dir 缺失或 UID 不符拒绝。agent 由用户服务管理，UI 退出只断开订阅。
帧为 UTF-8 JSON + LF，上限 1 MiB；不支持 batch 或请求 notification。限制 outstanding 请求及事件队列；慢消费者断开后重同步，不阻塞 payload。时间 UTC RFC3339；速度 bytes/second。整数最多 2^53-1，超过须升版，禁止截断。未知为 null。profile 只含 credential_ref，不传 token/password/private_key。错误与日志脱敏。schema 禁止未声明字段。

## 方法
| 方法 | 参数 | result.kind |
|---|---|---|
| profile.list/create/update/delete | profile 地址/CA/credential_ref；变更 operation_id | profile_list/profile/deleted |
| node.testConnection | node_id/root_id/path/access | connection_test |
| node.capabilities | node_id | capabilities |
| directory.list | node_id/root_id/path/cursor/limit | directory |
| transfer.create | operation_id/direction/source/destination/policy | transfer |
| transfer.start/pause/cancel | transfer_id/operation_id/expected_revision | operation |
| transfer.resume | 上述参数 + checkpoint_id | operation |
| transfer.get | transfer_id | transfer |
| transfer.list | cursor/limit | transfer_list |
| transfer.subscribe | transfer_ids/agent_epoch/after_sequence | subscription |

create 只创建 queued。start accepted 不表示 payload 已启动。pause 首版 unsupported 且状态不变。cancel accepted 仅表示取消请求；completed 重复取消返回 already_terminal。resume 检查资格后开启新 attempt，保留旧历史。节点活跃使用时 profile.delete 拒绝；删除配置不删任务。directory cursor 绑定 snapshot_id，过期明确报错。能力是双方认证后的交集、有 observed_at，不能从版本字符串推断。
旧 task.* / event.subscribe 不作为本版别名；采用本轮 transfer.*。persistent_tree 映射 v2；V3 是 dynamic_ring 调度及 range_policy，不另造协议模式。响应 id 匹配请求 id，result.kind 必须匹配表中方法；schema 形状校验之外由实现关联检查。

## 幂等与订阅
operation_id 在同用户内唯一并持久化。相同 ID/参数返回原结果，不同参数拒绝；先查幂等记录再验 expected_revision。重复 start 不新启子进程，状态/操作结果持久化后才返回 accepted。网络断开导致结果未知时用原 ID 重试或 get，不生成新 ID 盲目执行。
agent_epoch 每次 agent 生命周期变化；sequence 全局递增，revision 按 transfer 增长。订阅原子获取快照和 watermark，响应先送出，后续只发 sequence>watermark。首订阅/epoch 改变/缓冲淘汰 reset=true，返回完整匹配快照与 watermark；游标可重放则 reset=false、snapshots=[]，返回的 sequence 为请求游标，随后重放连续事件。空 transfer_ids 表示全部任务。
UI 断开显示 stale，重连 subscribe；reset 替换缓存，重复 sequence 忽略，缺口重新同步。旧 attempt 事件仅更新历史，旧 generation 不确认新文件。事件 envelope/payload 身份必须一致，transfer snapshot revision 与 envelope 相等。更新事件必须携带完整实体状态，不能只靠字节 delta 重建事实。

## 语义门禁与错误
JSON Schema 不证明跨字段事实。实现必须检查：completed 的 files_committed+files_skipped=files_total，bytes_committed=bytes_total，扫描/commit 确认；提交计数不超过总数；range 无重叠/越界；source/destination 方向；事件身份/revision；响应方法关联。
files_skipped 与 bytes_total 的口径：bytes_total 是需要传输/已提交的逻辑字节，不含按 conflict_policy=skip 跳过的文件字节，files_total 包含被跳过文件；UI 明示跳过并显示“完成（含跳过）”。required_crc32c 的 completed 不允许 integrity_status=not_requested。not_requested 仅显式 none。
-32001 unsupported；-32002 invalid_state/revision_conflict；-32003 not_found；-32004 auth_failed/keyring_locked；-32005 tls_identity_failed；-32006 permission_denied；-32007 data_port_unreachable；-32008 capability_mismatch；-32009 checkpoint_missing；-32010 source_changed；-32011 cursor_expired；-32012 protocol_error。
标准错误 -32700/-32600/-32601/-32602/-32603 仍使用 errorDetail（protocol_error/internal_error）。实现核对数值与 data.code。error.id 仅解析失败时 null。传输失败必须持久化 snapshot.error，RPC 拒绝不自动改变任务。
fallback_reason 必须在 actual_mode=v1 且请求 auto/v2 时非空；回退只在 payload 前、fallback_policy 允许且安全策略保持时发生。payload 后断线走失败/恢复，不能自动重播 V1。

## 待底层收口后升 draft-2
- 当前 capabilities 的全局 checksum/resume/TLS 集合不能推出 V2 同时支持这些组合；UI 必须等待 task 协商实际策略。正式版需按 mode 的能力矩阵或 server-authoritative policy evaluation，不把全局交集误用成模式支持。
- files/ranges/attempts 的完整分页查询与重连恢复尚未定义；本草案事件表达实体，但订阅任务快照不含全目录实体。详情 tabs 先限当前事件/汇总，正式实现前补带 cursor 的实体快照查询，并与 watermark 一致。
- 实際 checksum/data TLS policy 的回读字段与底层取消、checkpoint schema/version 仍需冻结。没有读回事实时 UI 只展示“请求策略”，不得声称已经启用。
