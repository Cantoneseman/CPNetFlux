# desktop-agent JSON-RPC API 2.1.0

状态：已冻结的桌面端 IPC 合同。消息形状使用 JSON Schema Draft-07，schema 和离线语义门禁位于 `desktop/schemas/desktop-agent.schema.json`、`desktop/tests/test_contract.py` 与 `desktop/tests/fixtures.json`。合同版本 `api_version=2.1.0`，独立于 JSON-RPC 的 `jsonrpc="2.0"`。

## 传输、版本与安全

Unix stream socket 路径 `$XDG_RUNTIME_DIR/cpnetflux/agent.sock`。父目录 0700、socket 0600；agent 检查 `SO_PEERCRED` 并只接受相同 UID。runtime dir 缺失或 UID 不符时拒绝连接。agent 由用户服务管理，UI 退出只断开订阅，不取消传输。

帧为 UTF-8 JSON + LF，单帧最多 1 MiB；不支持 batch 或请求 notification。agent 限制并发请求和事件队列；慢消费者断开后重同步，不得阻塞 payload。时间为带时区 UTC RFC3339，速度为 bytes/second；JSON 安全整数最大 2^53-1，禁止截断。未知值使用 `null` 或对应 `unknown`。profile/RPC/event/log 只允许 `credential_ref`，禁止传 token、密码和私钥；节点包导入器在本地生成受限文件后仍只传引用。

UI 在任何 profile 读取或有副作用请求前调用 `agent.getInfo`，要求 `api_version` 精确等于 `2.1.0`；不匹配时只显示兼容错误，不尝试猜测兼容。响应给出 agent epoch、adapter 类型、帧/并发上限和实际支持 feature 列表。feature 列表是 agent 已实现能力，不可由 UI 推导。

## 网络绑定方法与结果

桌面端安装在服务器本机时，agent 提供四个只作用于 CPNetFlux 的网络配置方法：

- node.network.inventory(node_id)：读取本机网卡、地址、carrier、MTU、速度和默认管理接口。只读，不修改系统网络。
- node.network.plan(node_id, inventory_revision, requested_links, peer_addresses)：从可用的物理/云虚拟接口生成确定性源地址绑定建议，排除回环、链路本地、组播和未指定地址。数字对端地址约束源地址族；主机名优先推荐 IPv4。清单变化时拒绝使用旧方案。逐链路对端地址映射尚未实现，peer_addresses 只允许空数组或 null 条目；非空地址明确返回 unsupported。
- node.network.apply(operation_id, node_id, plan_id, inventory_revision, confirm=true)：明确确认且全部请求链路均有可用绑定时保存 CPNetFlux 配置；网卡不足时必须减少请求链路数并重新生成。不会修改 IP、路由、网卡状态或防火墙，也不执行用户提供的命令。
- node.network.verify(node_id, plan_id)：检查地址仍属于可用接口，不尝试向对端建连；physical_path_verified=false。TCP 只绑定本机源地址，出站接口由系统路由决定。不能把本地检查当作连通性或物理路径验收。

network_plan 的每个 binding 包含 lane_id、interface_id、local_address 和可选 peer_address。network_apply 只返回实际保存的 CPNetFlux binding。传输客户端随后把这些地址作为 V3 control/data socket 的本地 bind 地址，并在 JSON summary 中记录 local_bind_addresses。系统 IP、路由和网卡启停属于后续独立的特权配置阶段。

## 方法与结果

| 方法 | 参数 | `result.kind` |
|---|---|---|
| `agent.getInfo` | 空对象 | `agent_info` |
| `profile.list/create/update/delete` | profile 地址/CA/`credential_ref`；变更带 `operation_id` | `profile_list/profile/deleted` |
| `node.testConnection` | `node_id/root_id/path/access` | `connection_test` |
| `node.capabilities` | `node_id` | `capabilities` |
| `directory.list` | `node_id/root_id/path/cursor/limit` | `directory` |
| `transfer.create` | `operation_id/direction/source/destination/policy` | `transfer` |
| `transfer.start/pause/cancel` | `transfer_id/operation_id/expected_revision` | `operation` |
| `transfer.resume` | 上述参数 + `checkpoint_id` | `operation` |
| `transfer.get` | `transfer_id` | `transfer` |
| `transfer.list` | `cursor/limit` | `transfer_list` |
| `attempt.list` | `transfer_id/cursor/limit` | `attempt_list` |
| `file.list` | `transfer_id/attempt_id/cursor/limit` | `file_list` |
| `range.list` | `transfer_id/attempt_id/file_id/generation/cursor/limit` | `range_list` |
| `transfer.subscribe` | `transfer_ids/agent_epoch/after_sequence` | `subscription`，之后发送 `transfer.event` |

不提供 `task.*` 或 `event.subscribe` 别名。`persistent_tree` 是 V2 数据面；V3 指动态文件调度和大文件 range 策略，不作为另一种 RPC 版本。

## 能力、请求策略和实际策略

`node.capabilities.mode_capabilities` 对每个 `v1`/`v2` 分别报告 scheduler、range、checksum、resume、pause、TLS 和资源上限。旧的聚合能力字段仅作显示，不能据此推导某模式或组合一定支持。

transfer 的 `policy` 是用户请求值；`effective_policy` 是 agent 从后端协商/运行事实读回的实际值。建立任务或协商前须标 `resolution=pending`，所有有效值保持 `unknown`/`null`；不得复制请求值伪装成实际值。开始数据面前标 `resolution=resolved`，并回读 mode、scheduler、range、checksum、resume、控制/数据 TLS、实际通道数和时间。effective 值必须满足所请求安全策略；否则在 payload 前拒绝任务，不能自动降级 checksum 或 TLS。V1 fallback 仅在 `fallback_policy=allow_compatible`、V1 满足 checksum/resume/TLS 请求且尚未发送 payload 时发生，并记录明确 `fallback_reason`。payload 后错误不得改跑 V1。

## 稳定身份、分页快照与事件

文件 identity 为 `(transfer_id, attempt_id, file_id, generation)`，并保留 `relative_path,size,mtime_ns`。目录调度队列只暴露元数据。range identity 为 `(transfer_id, attempt_id, file_id, generation, range_id)`，含 `attempt_number,offset,length,checksum,state`；区间不得越界或重叠。checksum 必须分别表达 algorithm、expected、actual、status；缺证据保持 unknown。

`attempt.list/file.list/range.list` 的第一页创建固定 `snapshot_id`，返回同一 `agent_epoch`、transfer `revision` 和 `snapshot_sequence`。续页 cursor 是不透明 token，并绑定方法、transfer、attempt、file generation、limit 与该快照；不得跨任务/attempt/file 复用。所有页均保留原快照 watermark；快照过期明确返回 `cursor_expired`，客户端重新从第一页读取。range 查询必须指定 `file_id` 与 generation。所有条目 identity 必须与页范围相符。

`agent_epoch` 每次 agent 生命周期变化；事件 `sequence` 全局单调递增，transfer `revision` 单调递增。subscribe 原子返回快照及 watermark：新订阅、epoch 改变、游标过期/缓冲被淘汰返回 `reset=true` 和完整 transfer 快照；连续重放返回 `reset=false`、空快照和请求游标。客户端分页的实体读取以其 watermark 为界，再从 `snapshot_sequence` 订阅后续事件。事件必须携带完整实体，不用 byte delta 还原事实。payload 身份、event envelope、attempt/revision/generation 不一致就拒绝更新；旧 attempt 事件不得覆盖当前 attempt。

UI 断开显示 stale，重连执行订阅重同步。不得仅靠 `transfer.get` 的汇总字段声称目录/range 详情完整。

## 幂等、生命周期、取消与恢复

`operation_id` 在同用户域唯一且持久化。相同 ID 和相同参数返回原结果，不同参数拒绝；先查幂等记录再校验 `expected_revision`。记录操作和期望状态持久化后才能响应 accepted。连接断开导致结果未知时，用原 ID 重试或查询，不另发 ID 盲目重做。`transfer.create` 只创建 queued；start accepted 不代表 payload 已启动。pause 若后端不能真实暂停，返回 `unsupported` 且不改变状态。重复 start 不创建第二个子进程。

cancel accepted 仅表示取消请求。活跃任务必须先变为 `cancelling`；只有底层 worker/channel 全部停止、没有继续写入、并持久化最终 checkpoint/commit 状态后，attempt 才能带 `quiescence=confirmed` 进入 `cancelled`。排队任务可直接安全终止。完成与 cancel 竞态以已持久化 terminal 状态为准；已完成重复取消返回 `already_terminal`。断线或 agent 重启不自动重复启动任务。

resume 仅在 checkpoint 可验证时接受。agent 核对 checkpoint/schema 版本、transfer/source/destination/policy identity、文件 `(file_id,generation,path,size,mtime_ns)`、manifest/checksum 与已提交 range；source identity 改变返回 `source_changed`，缺失/损坏返回 `checkpoint_missing`。新恢复必须创建新 `attempt_id`/递增 `attempt_number` 并保留旧历史；只重传未验证缺失 range，已提交 range 不重复提交。`resume_policy=require` 而无可验证 checkpoint 时拒绝；eligible 不能仅由文件存在推定。

## 目录、配置和授权

`directory.list` 的 cursor 绑定节点、授权 root、目录和 snapshot；路径相对于授权 root，拒绝绝对路径、`..` 逃逸和符号链接绕行。过期 cursor 明确报错。profile 在节点活跃任务引用时不可删除；配置更新不改变已有 attempt 冻结的 endpoint、credential reference 和 policy。目录/节点测试各检查状态、原因和错误；控制端成功但数据端失败时 `ready=false`。

## 错误与完成条件

应用错误映射：-32001 unsupported；-32002 invalid_state/revision_conflict；-32003 not_found；-32004 auth_failed/keyring_locked；-32005 tls_identity_failed；-32006 permission_denied；-32007 data_port_unreachable；-32008 capability_mismatch；-32009 checkpoint_missing；-32010 source_changed；-32011 cursor_expired；-32012 protocol_error。标准 JSON-RPC 错误 -32700/-32600/-32601/-32602/-32603 使用稳定 `errorDetail`；数值码与 `data.code` 必须一致。传输失败写入持久 transfer snapshot.error，RPC 拒绝本身不改变任务。

`completed` 必须具备完整扫描、接收端 commit 确认、文件/字节计数闭合、`transfer_status=pass`、校验策略满足、range 全部验证并提交、`evidence_status=complete`，且无错误。`bytes_total` 是需发送/已提交逻辑字节，不含按 `conflict_policy=skip` 跳过的文件字节；`files_total` 包含跳过文件。UI 应明确显示“完成（含跳过）”。`checksum_policy=none` 只能在显式 `experimental_raw_ack=true` 下启用，完成态须标记 `not_requested`。
