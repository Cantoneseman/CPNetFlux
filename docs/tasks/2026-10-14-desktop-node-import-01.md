# DESKTOP-NODE-IMPORT-01：桌面端节点包导入

- 输入提交：`5e5e7b6b506bfb32e582042affb51d7634fb3714`
- 依赖：DEPLOY-SERVER-SETUP-01 生成的 `.cpnetflux` 包；desktop 当前 API 2.0.0、TLS+token 真实目录传输闭环。

## 范围

- 在“节点连接”页面增加节点包选择器。
- 校验包格式、版本、节点 ID、地址、TLS server name、CA PEM、token 和源文件权限。
- 将 CA 与 token 写入当前用户 `XDG_CONFIG_HOME/cpnetflux/nodes/<sha256(node_id)>/`，目录 0700、文件 0600；profile 只保存 CA 路径和 `file:` token 引用。
- 导入后通过现有 `profile.create/update` 和认证检查实现真实可用连接；token 不进入 QML、RPC、agent state、日志或命令行。
- 增加 Qt 编译、offscreen、多尺寸和真实导入回归；选择器惰性加载，不因发行版缺少 QtQuick.Dialogs 阻断主界面。

## 非目标

- 不传送节点包、不自动连接公网、不绕过 TLS 校验、不实现 mTLS 客户端证书或 OS secret-store。
- 不修改传输协议、agent 状态语义、目录/range 调度或单文件能力。
- 不把 demo 合成状态作为认证或传输成功。

## 验收

- `cmake --build desktop/build` 通过。
- `ctest --test-dir desktop/build --output-on-failure` 4/4 通过。
- `desktop/tests/test_import.py` 验证 CA/token 私有文件权限、profile 引用和 token 无泄漏。
- 无效/过大/组可写包被拒绝；界面显示下一步修复信息。
- 真实安装包包含节点包选择器资源及 server setup 工具，不包含测试凭据。

## 允许修改路径

- `desktop/ui/rpc_client.{h,cpp}`
- `desktop/ui/Main.qml`, `desktop/ui/ProvisioningPicker.qml`, `desktop/ui/main.cpp`
- `desktop/CMakeLists.txt`, `desktop/tests/test_import.py`
- `desktop/agent/adapter-boundary.md`
- 本任务记录及相关产品/安装文档。

## 回滚

删除本任务新增导入器、选择器和测试，恢复现有 profile 手工填写流程；不删除用户已经导入的 `~/.config/cpnetflux/nodes` 凭据，需由用户按节点删除/轮换 token。