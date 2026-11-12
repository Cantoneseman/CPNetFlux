# 桌面端 API 3.0 后端交接

本轮后端接口已实现绑定读取/清除/手动选择、V3-only 合同、逐 lane 远端映射，
并将用双服务器上传/下载进行验收。
桌面端可以据此开始开发；不应把尚未实现的核心功能做成可用控件。
完整契约：rpc-contract.md；可机读定义：../schemas/desktop-agent.schema.json。

## 接入与页面任务

1. 先 agent.getInfo，精确要求 API 3.0.0；再 agent.getContract。
   按 methods.support/reason 控制可用性，从 policy_defaults/schema 初始化表单。
2. 节点页：profile CRUD、token 文件引用、node.testConnection。
   分别显示 TLS/认证/目录权限/数据口结果；当前 ready=false 不伪装为全绿。
3. 网络页：inventory -> plan（推荐或 selections 手选）-> 用户确认 -> apply。
   进入页面先 get，提供 verify、基于 expected_plan_id 的 clear。
   检测的是本机网卡；每个 peer profile 关联本机源地址和逐 lane 远端 IP 配置；映射必须指向同一 V3 服务。
4. 新任务页：只显示 V3；目录上传/下载，validate 展示实际选项和 warnings，
   再 create/start。流水线深度可能覆盖 pending_window，应展示解析值。
5. 任务页：列表分页、事件订阅、stale/重连同步、cancel、具备资格时 resume。
   原始 revision、attempt_id、epoch/sequence 必须随操作和更新正确使用。
6. 结果页：transfer.evidence、attempt/file/range 分页。
   observation/coverage 表示最终观测明细，非实时完整目录。
   空文件没有 range；core_attempt 与桌面 attempt_number 不能混用。

## 控件边界

可配置：通道、数据/控制窗口、压缩、CRC32C、resume、重连次数、
总 deadline 和 global 调度。默认值从后端获取，不能继续沿用 API 2.x 数值。
queue_depth/range_policy 由 V3 自动决定，fallback 固定 reject。
高级调度预算需要实际验收：过小预算可能无法容纳 V3 的大文件 range，
预检不执行完整数据扫描，不承诺任意组合都可运行。

暂不开放：暂停、远端目录浏览、单文件统一认证、系统 IP/路由/网卡启停和实时逐块速率。
映射的多个地址必须属于同一 V3 服务实例和 root。凭据只有文件引用，
不发送 token 值，不把管理员凭据/系统密钥环做成默认可用入口。
网卡 verify 只证实本机地址可用，不代表物理链路独立。

## 已验收与复现

核心 CTest 413/413；桌面 CTest 5/5，其中契约/agent/V3 行为共 41 项。
深圳和上海分别运行 real agent，4 次真实传输通过；
每组 4 文件、44040225 字节，含 40 MiB 大文件、随机数据、小文件和空文件。
两端源码与核心二进制一致，TLS/token、两个逻辑 lane、压缩、
global adaptive、pipeline 和 local bind 组合通过。

复现脚本 tools/test/run_cpnetflux_desktop_agent_peer.py 需要单独启动的
V3 TLS/token 对端、测试 CA/token 文件、本机可用源地址和独立证据目录。
不要把脚本当部署管理 API。具体证据与 SHA：
docs/tasks/2026-10-21-v3-desktop-agent-align-01-result.md。

下一步由桌面端按上述契约实现页面和 RPC 集成，再按实际页面流程做端到端验收。
其余明确 unsupported 的能力属于独立阶段，不阻塞当前已支持流程。

## 2026-10-22 前端集成

正式 QML 现已使用 API 3.0.0 的 getInfo/getContract、预检/确认/创建/启动、reset subscription、取消/resume、独立分页 watermark 与 network get/clear/manual mapping。浅色设计取代旧深色 demo 壳；没有新增 IPC 版本或另一套协议。
真实 SIGTERM 无 summary 时，agent 也检查现有 V3 tree manifest 头部身份并提供恢复候选；完整源身份与缺失 range 校验仍由核心执行。恢复前缺失 range 预览和实时逐块统计仍未提供。
实际验收和证据见 docs/tasks/2026-10-22-v3-desktop-ui-refactor-01-result.md。
