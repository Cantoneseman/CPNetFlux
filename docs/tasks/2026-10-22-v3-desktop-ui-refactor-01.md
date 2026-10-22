# V3-DESKTOP-UI-REFACTOR-01

日期：2026-10-22
输入提交：深圳权威 /root/projects/CPNetFlux HEAD c74eeacd05b327519023badcac657d662b07647b（代码基线为 3dd7bff，c74eeac 仅新增实验计划）（当前分支 codex/V3-DESKTOP-NETWORK-BINDING-01）。
负责人：当前总指挥，深圳根目录单人串行开发；保护根目录既有未提交文件。

## 阶段范围

把现有 Qt 6/QML 桌面端从“已有 RPC 展示壳”重构为基于最新 V3 desktop-agent API 3.0.0 的真实客户端流程，并沿用已确认的简洁苹果风视觉方向：

- agent 启动握手：agent.getInfo + agent.getContract，展示 API/runtime/adapter/能力协商和明确错误。
- 新建任务：从后端 policy_defaults/policy_schema/methods 初始化表单，严格执行 transfer.validate → transfer.create → transfer.start；只提交 V3 tree_transfer 语义。
- 真实任务：Unix socket 订阅、重连/epoch/sequence reset、任务列表、取消请求/终态、真实 checkpoint 恢复。
- 任务详情：请求/实际策略分离，阶段/指标/manifest/checksum/finalize/error 来自 agent；attempt/file/range 分页保持各自 snapshot watermark。
- 节点与网络：复用现有 profile、TLS/token 文件引用、连接测试和本机绑定/逐 lane peer mapping 页面；未支持能力按 contract 禁用。
- 视觉重构：统一导航、状态徽章、卡片层级、空状态、错误状态和响应式表单，不把 demo 数据混入真实任务列表。

## 非目标

- 不修改 V3 核心协议/执行器、调度器、压缩或故障恢复实现。允许补齐 agent 对取消后既有 V3 manifest 的观测，不在 UI 实现恢复算法。
- 不开放暂停、远端 directory.list、单文件统一认证、管理员 credential/Secret Service、实时逐块速度。
- 不启动长时间跨域/性能实验，不修改正式服务配置，不触碰上海或其他用户项目。
- 不把未提供的后端字段推算为真实值；不把 demo 作为默认路径。
- 不删除根目录既有 dirty 文件；不提交构建产物、凭据、payload 或本地状态。

## 接口/schema 依赖

- desktop/docs/rpc-contract.md 与 desktop/schemas/desktop-agent.schema.json，API 版本精确为 3.0.0。
- agent.getInfo、agent.getContract、profile.list、node.testConnection、node.network.*。
- transfer.validate/create/start/cancel/resume/get/list/subscribe/evidence，attempt.list/file.list/range.list。
- policy 的实际默认值、support/reason、resolved_options、effective_policy、errorDetail 和 snapshot watermark 由 agent 返回。

## 当前能力事实与缺口

已存在：真实 V3 tree 上传/下载；TLS/token 文件引用；连接测试；任务持久化、订阅、取消、重连；核心 summary/manifest/checksum/finalize 证据；resume checkpoint；本机源地址和逐 lane peer mapping；最终 file/range 观测分页。
需要本阶段补齐：UI getContract 驱动、validate/create/start 串联、策略字段回读、API 错误分类、详情分页 watermark 隔离、真实任务创建后的状态导航和新视觉层。
明确未实现：pause、directory.list、实时逐块速率、单文件统一认证、管理员凭据/Secret Service。

## 受影响文件

- desktop/ui/rpc_client.h
- desktop/ui/rpc_client.cpp
- desktop/ui/Main.qml、desktop/ui/PathPicker.qml
- desktop/tests/rpc_driver.cpp、test_rpc_client.py、ui_flow_driver.cpp、test_ui_real.py、test_v3_api.py、test_package.py
- desktop/agent/cpnetflux_agent.py（取消后无 summary 时的 manifest 候选观测）
- desktop/CMakeLists.txt、cmake/LinuxPackaging.cmake、packaging/cpnetflux-agent.service、packaging/tests/test_service.py、desktop/agent/cpnetflux-agent.in
- desktop/README.md、desktop/agent/adapter-boundary.md、docs/release/linux-install.md
- desktop/docs/backend-handoff-api-3.md、docs/coordination/BOARD.md（本阶段交接）
- 本任务记录与必要的 UI handoff 文档

## 验收标准

1. Python contract/agent/V3 定向测试通过。
2. CMake build、CTest、Qt offscreen 页面启动通过。
3. 真实 agent 启动时 UI 必须显示 API 3.0.0、runtime=v3、adapter=real；getContract 未完成时禁止新建。
4. 新建任务必须可观察 validate、create、start 的请求结果；agent 错误显示稳定 code/message/retryable。
5. 真实任务列表经 transfer.subscribe 恢复；断线后显示 stale/重连，恢复后 reset 快照不丢任务。
6. 任务详情的 attempt/file/range 页面不会互相错误复用 watermark；未知字段保持未知。
7. demo 仅显式 --demo，并与真实任务/状态有清晰视觉隔离。
8. CPack DEB 配置保持 Qt 运行依赖，未新增凭据或构建产物。

## 回滚

只回滚本阶段明确提交；保留并继续保管根目录已有未提交文件。恢复前保留当前修改，不使用 reset/clean 扩大范围。

## 验收命令

python3 -B desktop/tests/test_contract.py
python3 -B desktop/tests/test_agent.py
python3 -B desktop/tests/test_v3_api.py
cmake -S desktop -B desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build desktop/build --parallel 2
ctest --test-dir desktop/build --output-on-failure

不启动长时间跨域/性能实验；真实传输场景只在已有 loopback/定向 agent 门禁允许时做最小验证。


## 已确认的最小后端补齐

真实取消会 SIGTERM 核心；核心未必写最终 summary，但可能已有 tree manifest。agent 现有 read_real_evidence 仅在 summary 分支观测 manifest，导致真实 checkpoint 被误报未知。
保持 API/schema 3.0.0 不变：无 summary 时也读取 manifest 前四个头字段，要求 version=1、mode、source logical path、checksum policy 匹配，且不是 symlink；只声明候选存在，不推算已完成字节。resume 的源身份/缺失 range 仍交由现有 V3 核心核验。缺失 range 预览目前无 API，UI 不伪造清单。
安装 wrapper 默认发现同目录核心，保留环境变量与 CLI 显式覆盖；user service 不启用 PrivateTmp，保证用户选择的文件路径与 agent 视图一致。
