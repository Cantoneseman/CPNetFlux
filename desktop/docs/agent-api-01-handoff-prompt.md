# 桌面端续接 prompt：AGENT-BACKEND-02

请在本桌面端任务中继续开发 CPNetFlux Linux 桌面端。00 已完成 `AGENT-API-01`，冻结 IPC 合同 `desktop-agent API 2.0.0`，并推送到 CPNetFlux GitHub `codex/AGENT-API-01`。开始前阅读 `desktop/docs/rpc-contract.md`、`desktop/schemas/desktop-agent.schema.json`、`desktop/tests/test_contract.py`、`desktop/docs/acceptance.md` 和本任务记录。不要在未同步 00 的情况下修改 schema/合同。

本阶段任务 ID：`AGENT-BACKEND-02`。Python demo agent 已有 API 2.0.0 版本握手、受 schema 校验的请求面、有限 attempt/file/range 模拟快照分页、操作/attempt 持久状态和显式 effective_policy。请优先让 Qt UI 按合同消费这些接口，并提高本地客户端状态可靠性：

- UI 首先调用 `agent.getInfo`，严格校验 `api_version == "2.0.0"`；不兼容时停止后续 RPC，并给出清晰错误。
- 详情页按需分页调用 `attempt.list/file.list/range.list`。使用统一 snapshot_id、revision、agent_epoch 与 snapshot_sequence；cursor 只用于原查询身份，过期提示刷新。UI 从该 watermark 开始订阅事件，不能混合不同 attempt/generation 的数据。
- 清楚区分摘要、分页完整度、stale 状态、reset/resync、错误和“完成（含跳过）”；未知值显示未知，不以 0 或请求值代替。
- `adapter=demo` 与模拟 effective_policy 必须持续显著显示“演示”；不得把模拟 TLS/checksum/mode 状态呈现成真实链路验证。
- 为版本不兼容、分页加载/过期、重连补事件、旧 attempt 事件、空列表和取消竞态添加 UI/agent 回归；运行 Python contract/agent 测试、桌面 CMake build 与完整 CTest。

非目标：改动 C++ 核心传输协议、集成真实网络 adapter、TLS/token 注入、安装器/systemd、跨主机实验或吞吐调优。真实 transfer、节点探测和目录浏览当前保持显式 unsupported；绝不把 token 放进 argv/env/RPC/log，也不使用临时明文文件规避 secret store 缺口。若继续开发需要核心侧结构化输出或安全入口，记录具体请求和证据，不在桌面侧猜测。

请沿用本线程既有工作分支流程，提交只包含本阶段桌面文件并推送已核实的 CPNetFlux GitHub remote，回读远端 SHA。不要清理或覆盖其他 worktree/dirty 文件。
