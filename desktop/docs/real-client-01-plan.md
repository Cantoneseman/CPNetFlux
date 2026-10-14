# REAL-DESKTOP-TRANSFER-01 任务记录

日期：2026-10-12
输入提交：深圳权威 `/root/projects/CPNetFlux` HEAD `c8bae2d0d35682d391b0be8d87fe16f40348b7a9`，独立 worktree `codex/desktop-real-transfer-01` HEAD `995dc57ee61a2592924e53e0d1dde40893db34d7`。根目录既有 `desktop/ui/Main.qml`、`desktop/ui/rpc_client.cpp` 未提交改动不触碰。

## 范围

- 将 desktop-agent API 2.0.0 的默认路径改为真实 adapter；`--demo` 保留为显式开发诊断入口。
- 以 CPNetFlux 现有 `cpnetflux-file-*`、`cpnetflux-tree-*` 客户端/服务端、event-log、manifest/checksum/resume 和 TLS/auth 参数为后端来源。
- agent 负责本地路径校验、真实进程生命周期、结构化状态快照/事件、取消、重连持久化和后端返回的错误/校验结果；Qt 只负责展示和发送 API 请求。
- UI 提供本地文件/目录选择、远端地址/端口/root-relative 目标、CA/凭据引用和真实任务详情；敏感值不进入 QML/RPC/state。
- 增补 Linux CPack/DEB 运行依赖和 CTest/loopback 回归门禁。

## 非目标

- 不启动长时间跨域或性能对比实验；不改 GridFTP 对照路线。
- 不改核心 V2/V3 算法或另造 UI 状态机；只调用现有 CLI/事件日志和 API 2 schema。
- 不在本阶段提供未有安全注入通道的明文 token/密码，也不以 demo/本地 copy 冒充真实传输。
- 不修改深圳根目录 dirty 文件、其他用户项目或把构建产物/凭据/payload 放入源码和安装包。

## 当前闭环与缺口

已有：API 2.0.0 schema 与分页/事件语义、Unix socket peer-uid 校验、持久状态/重连、Qt 任务列表与详情、核心 file/tree client/server、event-log JSON、manifest/checksum/resume、TLS/auth CLI 参数、AUTH-FOUNDATION/POLICY。真实 adapter 已接入目录任务：agent 做路径/profile/TLS/token 文件权限预检，启动 tree client，回读 summary/manifest/event-log，持久化结构化证据并支持真实取消；UI 默认真实模式，demo 只由 --demo 显式启用。

当前明确缺口：现有 core file client 没有与 tree client 相同的统一 TLS/认证控制面，真实单文件 create 会返回 unsupported，UI 不会伪造成功；真实 manifest 的 file/range 分页解析和远端 directory.list 尚未提供，因此详情只展示任务级摘要/阶段证据并禁用实体分页；admin credential 的 OS secret-store 注入仍返回 unsupported。DEB 安装 core CLI 是独立输入，service 通过 CPNETFLUX_CORE_BIN_DIR 指向用户安装的 core 目录，缺失时 agent 明确报错。

## 最小接口契约

- `agent.getInfo` 返回 `adapter=real`、`api_version=2.0.0`，features 只列已实现能力；demo 仅在显式 `--demo` 下返回 `adapter=demo`。
- `transfer.create` 接收现有 API 2 `direction/source/destination/policy`，本地端点必须是绝对路径；agent 识别 file/dir 并调用相应现有 CPNetFlux CLI，不复制协议状态机。
- profile 只保存 `credential_ref`/CA 路径等引用；本阶段 token/password/private key 仍禁止通过 RPC、argv、环境、日志或 state 注入；服务器节点包导入器只把 token 写入当前用户 0600 文件，profile 继续保存 `file:` 引用。
- 真实任务快照的 `state/metrics/attempt/error/effective_policy/recovery` 只能由进程退出码、event-log、manifest/checksum 和可核验文件统计填充，缺失字段为 null/unknown。
- 取消使用真实进程终止与最终状态屏障；resume 只在核心 CLI 明确支持且 checkpoint/source identity 可验证时启用，否则禁用并返回原因。

## 受影响文件

- `desktop/agent/cpnetflux_agent.py`, `desktop/agent/adapter-boundary.md`, `desktop/schemas/desktop-agent.schema.json`
- `desktop/ui/main.cpp`, `desktop/ui/rpc_client.{h,cpp}`, `desktop/ui/Main.qml`
- `desktop/CMakeLists.txt`, `desktop/packaging/*`, `desktop/tests/*`
- `desktop/docs/real-client-01-plan.md`（本文件）

## 验收标准

1. API/定向 Python 与 Qt offscreen 测试通过；默认 agent 连接显示真实 adapter，断线/API mismatch/权限错误可见。
2. 核心能力缺口明确阻止真实单文件，不显示假任务；真实 loopback 目录：多个小文件统一 transfer 语义，事件、summary、manifest/checksum/finalize 可见。
3. 取消、agent/UI 重连、失败错误和后端实际支持的 resume 有证据；不支持项禁用并说明。
4. 取消、agent/UI 重连、失败错误和后端实际支持的 resume 有证据；不支持项禁用并说明。
5. TLS/auth 成功和错误路径均返回结构化错误；token 只以受限 file: 引用交给 agent，不落 QML 明文。
6. CMake/CTest、CPack DEB 门禁通过，DEB 依赖包含 `qt6-qpa-plugins` 与 `qml6-module-qtquick-templates`。
7. 若当前环境没有 Ubuntu 22.04 图形 VM，则记录未执行原因，不把离线/本机验证冒充 VM 验证。

## 回滚

只回滚本任务分支提交或删除该 worktree；不 reset/clean 深圳根目录。需要撤销时恢复到 `995dc57ee61a2592924e53e0d1dde40893db34d7`，保留根目录未提交文件。

## 验收命令

- `python3 -B desktop/tests/test_contract.py`
- `python3 -B desktop/tests/test_agent.py`
- `cmake --build desktop/build && ctest --test-dir desktop/build --output-on-failure`
- `cpack --config desktop/build/CPackConfig.cmake -G DEB`
- `desktop/tests/test_real_loopback.py`（单文件/目录/取消/重连，使用已构建 CPNetFlux core）

## 交接

已验证：Python contract/agent 15 项、Qt offscreen 7 页面与断线握手、CMake build、CTest 3/3、CPack DEB；真实 TLS/token 目录 loopback 完成 2 个小文件（bytes 8，checksum/manifest/finalize pass）。实现提交 feafd04affed6a98ce3d2d34a473bc6111a6131b 已推送到 origin/codex/desktop-real-transfer-01，远端 SHA 回读一致；随后本文件的交接更新作为后续提交推送。未执行 Ubuntu 22.04 图形 VM 安装，也未启动长时性能/跨域实验。
