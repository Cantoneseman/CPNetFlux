# 桌面端 API 3.0 后端交接

本轮后端接口已实现并通过双服务器上传/下载验收。
桌面端可以据此开始开发；不应把尚未实现的核心功能做成可用控件。
完整契约：rpc-contract.md；可机读定义：../schemas/desktop-agent.schema.json。

## 接入与页面任务

1. 先 agent.getInfo，精确要求 API 3.0.0；再 agent.getContract。
   按 methods.support/reason 控制可用性，从 policy_defaults/schema 初始化表单。
2. 节点页：profile CRUD、token 文件引用、node.testConnection。
   分别显示 TLS/认证/目录权限/数据口结果；当前 ready=false 不伪装为全绿。
3. 网络页：inventory -> plan（推荐或 selections 手选）-> 用户确认 -> apply。
   进入页面先 get，提供 verify、基于 expected_plan_id 的 clear。
   检测的是本机网卡；每个 peer profile 关联一份本机源地址配置。
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

暂不开放：暂停、远端目录浏览、单文件统一认证、逐链路远端映射、
系统 IP/路由/网卡启停和实时逐块速率。凭据只有文件引用，
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

下一步应先完成上述桌面页面和 RPC 集成，随后按实际页面流程进行
端到端验收。其余暂缺能力属于独立后端阶段，不阻塞当前已支持流程。
