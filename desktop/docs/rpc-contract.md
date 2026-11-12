# desktop-agent JSON-RPC API 3.0.0

状态：本轮 V3 桌面开发契约。唯一结构定义是 `desktop/schemas/desktop-agent.schema.json`；
行为验收位于 `desktop/tests/test_contract.py`、`test_agent.py`、
`test_v3_api.py`。旧 API 2.x 的 V1/V2 模式和自动回退规则已经失效。
API 版本与 JSON-RPC 的 `jsonrpc="2.0"` 是两回事；运行引擎只有 V3。

## 接入顺序

1. 连接本机 Unix socket：`$XDG_RUNTIME_DIR/cpnetflux/agent.sock`。
2. 调用 `agent.getInfo({})`，精确要求 `api_version="3.0.0"`。
3. 调用 `agent.getContract({})`，按每个方法的 support/reason 启用控件。
4. 读取 profile、保存的网卡绑定、任务列表，并建立 transfer.subscribe。
5. 新任务从 policy_defaults 初始化，用 policy_schema 校验表单，先 validate，再 create/start。

`agent.getContract` 返回 runtime_protocol、adapter、methods、policy_defaults、
policy_schema 和 limitations。support 为 supported/unsupported/simulated。
real adapter 缺少核心可执行文件时，相关启动/预检方法显示 unsupported。
支持一种方法不代表任意参数组合可用，最终以 validate 或明确错误为准。

仅支持 UTF-8 JSON + LF 单消息帧，最大 1 MiB；不支持 batch 和无 id 请求。
请求 id 必须为非空字符串。父目录 0700、socket 0600，并检查同 UID。
时间为 UTC RFC3339，整数不超过 2^53-1；未观测字段用 null/unknown。
UI 退出只断开连接，正常运行的 agent 继续负责任务。

## 可调用接口

| 方法 | 主要参数 | 结果 kind / 可用边界 |
|---|---|---|
| agent.getInfo | {} | agent_info；握手、feature、进程 epoch |
| agent.getContract | {} | agent_contract；表单默认值、schema、方法状态 |
| profile.list | {} | profile_list |
| profile.create/update | operation_id, profile | profile |
| profile.delete | operation_id, node_id | deleted；活跃任务引用时拒绝 |
| node.testConnection | node_id, root_id, path, access | connection_test；真实 TCP/TLS/token/V3 协商 |
| node.capabilities | node_id | capabilities；本机核心支持上限，不冒充已连接对端 |
| node.network.inventory | node_id | network_inventory；本机网卡快照 |
| node.network.plan | node_id, inventory_revision, requested_links, peer_addresses, selections? | network_plan；推荐或手选 |
| node.network.apply | operation_id, node_id, plan_id, inventory_revision, confirm=true | network_apply |
| node.network.get | node_id | network_configuration；binding 可为 null |
| node.network.verify | node_id, plan_id | network_verify |
| node.network.clear | operation_id, node_id, expected_plan_id, confirm=true | network_configuration；绑定变化返回 revision_conflict |
| transfer.validate | direction, source, destination, policy | transfer_validation；不创建任务、不连接对端 |
| transfer.create | validate 参数 + operation_id | transfer；只创建 queued |
| transfer.start/cancel | operation_id, transfer_id, expected_revision | operation |
| transfer.resume | 上述参数 + checkpoint_id | operation；新建 attempt |
| transfer.get | transfer_id | transfer |
| transfer.list | cursor, limit | transfer_list；固定快照的分页游标 |
| transfer.evidence | transfer_id, attempt_id | transfer_evidence；最终核心事实白名单 |
| attempt.list | transfer_id, cursor, limit | attempt_list |
| file.list | transfer_id, attempt_id, cursor, limit | file_list；最终摘要中观测到的文件 |
| range.list | 上述参数 + file_id, generation | range_list；最终摘要中观测到的 range |
| transfer.subscribe | transfer_ids, agent_epoch, after_sequence | subscription；之后推送 transfer.event |
| transfer.pause | operation_id, transfer_id, expected_revision | 明确 unsupported，不改变任务 |
| directory.list | node_id, root_id, path, cursor, limit | 明确 unsupported |

完整字段、必填项、枚举和错误结构以 schema 为准，不从示例猜测。

## 网络配置流程

inventory 检测的是运行 agent 的本机网卡，不是远程服务器网卡。
node_id 表示把本机源地址配置应用于哪个 peer profile。
plan 不带 selections 时推荐地址；手选形式为：
`selections=[{"interface_id":"linux:eth0","local_address":"192.0.2.10"}]`。
手选数量必须等于 requested_links，每块网卡只能选一次，地址须已配置且接口可用。
不足链路的推荐可以展示，不能 apply。旧 inventory_revision 被拒绝。

peer_addresses 为空时每条 lane 使用 profile.host；填写后必须覆盖每条 requested
link，且是与本地地址同族的数字 IP。apply 保存 CPNetFlux 的本地绑定和逐链路
远端映射，不改系统 IP、路由、网卡启停或防火墙。get 读取已保存值；clear 必须提交 get 返回的
expected_plan_id（未保存时为 null）。活跃任务期间拒绝修改/清除所用配置。

verify 检查地址仍可用，physical_path_verified 始终为 false；
真实传输 summary 回报 peer_addresses 和每 lane 的 control/data socket 实际本地/远端 IP；
agent 校验配置与观测一致后才确认任务完成。这仍不证明物理路径独立。
summary 还回报 peer_addresses 和每条 lane 的 control/data socket 地址；agent
会校验这些实际地址，失败时任务不会被标成完成。远端 IP 必须属于同一 V3 服务和根目录。
单个绑定地址可用于多个逻辑通道；多个绑定地址的数量必须与 channel_count 一致。
创建任务时冻结 profile 与 binding，之后更改配置不会偷偷改变已有任务。

## 任务策略和执行

固定项：requested_mode=v3、fallback_policy=reject、conflict_policy=fail、
range_policy=auto、queue_depth=auto、control_tls=verify_ca。旧模式和不支持的
覆盖/跳过策略在参数阶段拒绝；不再切换旧执行器。

| 策略 | 范围 / 核心映射 |
|---|---|
| scheduler | auto/dynamic_ring/static；auto 解析为 dynamic |
| channel_count | auto 或 1..10；同时配置 file-parallelism 和 link-set-links |
| pending_window | 1..16；data-pending-window |
| control_pipeline_depth | 0/1/2/4；非零要求 dynamic |
| reconnect_attempts | 0..10；与 lane/整批恢复共用核心预算，要求 CRC32C 和允许 resume |
| transfer_timeout_ms | 0..86400000；0 在不重连时使用核心默认，重连时解析为 300000 |
| compression | off/auto；本分支的 V3 核心压缩，不代表其他未合入功能分支 |
| chunk_size / buffer_size | 正数字节；schema 给出保守上限 |
| scheduler_mode / scheduler_policy | off/global 与 fixed/adaptive；global 要求 dynamic，adaptive 要求 global |
| scheduler_work_item_min_bytes/max_bytes | 正整数，min 不得大于 max |
| scheduler_default_rtt_ms / scheduler_capacity_gbps | 正整数毫秒 / 正数 Gbps |
| checksum_policy | required_crc32c/none |
| resume_policy | never/allow/require；require 须已有 tree manifest |
| data_tls | required/off；off 或 checksum none 要显式 experimental_raw_ack=true |

preview 的 resolved_options 是计划值。队列容量由核心决定：
dynamic 为 channels × (pipeline_depth 非零时取它，否则取 pending_window) × 2；
static 为 0。不提供一个填写后却被忽略的手动 queue_depth 数值。
V3 使用共享流水线窗口：depth 非零时 resolved_options.pending_window 等于 depth；
若与请求 pending_window 不同，preview.warnings 明确说明覆盖关系。
scheduler_mode 是 RPC 字段，映射到核心命令行 --scheduler，不是 --scheduler-mode。

real adapter 目前支持 TLS/token 目录上传、下载；单文件、管理员凭据和
Secret Service 注入尚未接入。token 只通过权限受限的
`credential_ref="file:/absolute/token"` 文件引用传递，不经 RPC 传值。
远端 root_id 必须等于 profile.default_root_id；它是预配置 root 的标识，
不会在服务端授予权限或切换宿主目录。路径必须 root-relative，不能有 ..。

首次 allow 不传 --resume；只有显式 require 或后续恢复 attempt 才传。
进程结束后由核心 manifest/校验继续执行一致性检查；UI 不自行判断可恢复。
旧 API 状态目录保留，API 3 必须使用独立私有状态目录；不自动迁移旧任务。

## 结果、进度和证据

policy 是请求，preview 是预估；effective_policy 只在读到成功的 V3 核心
摘要后解析，运行期间保持 pending/unknown。实际通道数取
link_set_accepted_links，不取总连接次数；TLS 验证事实取核心成功结果。

transfer.evidence 返回当前 attempt 的 final_summary 或 not_available，
只公开 schema 中列明的核心字段，包括恢复计数、压缩、队列、lane、TLS、
local bind、逻辑字节、wire bytes、阶段时间和 tree_hash。
不会把整个内部日志或 token 内容经 RPC 发出。

真实 adapter 暂无实时逐字节速度、文件/range 事件。
file.list/range.list 增加 observation 与 coverage=observed_only：
最终摘要里没有列到的跳过项或失败项不能推定为已展示；空列表也不能
推定空目录。mtime_ns、每文件字节和校验值缺少证据时返回 null。
只在最终成功摘要下把观测文件/range 标为 committed/verified。
整文件观测的 length=0 不创建虚构 range；range_count 只统计显式 range。
range.attempt_number 是桌面 attempt 序号，core_attempt 单独保留核心从 0 开始的重试序号。
UI 应将这些页面称为最终观测明细，不能伪装实时监控。

bytes_total 是整个任务逻辑总量；bytes_transferred 为本次实际发送的逻辑字节，
可能因恢复跳过而小于 total；成功时 bytes_committed=bytes_total。
完成需要核心 exit 0、result=pass、V3/TLS/调度/通道/队列策略匹配、
文件计数闭合、无失败文件、最终 commit 和完整证据。进程 exit 0 本身不足以完成。

## 生命周期、分页和错误

operation_id 幂等且持久化；同 ID 同参数回放原结果，不同参数拒绝。
expected_revision 防止旧页面覆盖最新状态。cancel accepted 是取消请求，
底层进程退出才确认 cancelled/quiescence=confirmed；排队任务可直接取消。
resume 新建 attempt_id，递增 attempt_number，保留原历史和各自摘要路径。

分页游标绑定方法、任务、attempt、file generation 和 limit，快照有效期 300 秒。
实体页返回 snapshot_id/agent_epoch/revision/snapshot_sequence，续页保持水位。
epoch 变化或事件无法连续重放时 subscription 返回 reset=true 和完整快照。
慢消费者断开后重同步，不能阻塞数据传输。

连接探测只验证控制面，未探测目录授权和数据端口时 ready=false。
node.capabilities 的 protocol_version 明确标为本机支持项，control_tls_verified=false；
真正的已验证 TLS 取 connection_test 或成功任务 evidence。

错误格式为 JSON-RPC error + 稳定 errorDetail。应用错误范围 -32001..-32012，
标准错误 -32700/-32600/-32601/-32602/-32603，映射由 schema/契约测试固定。
RPC 拒绝不改任务；运行失败写入 snapshot.error。UI 不应吞掉 unsupported 或
revision_conflict 后继续执行。
