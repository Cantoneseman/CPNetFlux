# DEPLOY-SERVER-SETUP-01：服务器初始化与节点接入包

- 输入提交：`5e5e7b6b506bfb32e582042affb51d7634fb3714`
- 目标：让 Linux 服务器安装后可用一个显式、可审计的初始化命令生成 CPNetFlux TLS/token 配置、服务配置和桌面端节点导入包。
- 本阶段不把凭据写进 Git、日志、命令行或服务状态；导入包必须由管理员通过安全通道交给对端。

## 范围

1. 新增 `cpnetflux-server-setup` 初始化工具，支持 dry-run、无覆盖校验、TLS CA/服务器证书、token、数据根目录、服务 unit、可选 enable/start 和可选防火墙配置。
2. 生成版本化 `.cpnetflux` provisioning bundle，包含节点地址、TLS server name、CA 公钥证书和 token，文件权限 0600；命令只输出路径和 SHA-256。
3. 服务配置增加显式 `tls_client_auth` 字段。旧配置缺少该字段时继续按 `required` 处理；初始化生成的配置使用 `none`（TLS 服务端身份校验 + token），匹配现有桌面认证闭环。
4. 反映当前服务真实被动数据端口窗口：`data_port_base` 至 `data_port_base+511`。初始化工具、防火墙规则和文档不能按 connections 虚构更小窗口。
5. 增加单元/脚本门禁，覆盖输入校验、无覆盖、bundle 私密性、token 不泄漏、端口窗口和 dry-run。

## 非目标

- 不自动向网络传送 token/私钥，不默认修改防火墙，不默认 enable/start 服务。
- 不改变传输协议、range 调度、桌面 agent RPC 或服务器数据语义。
- 不实现客户端证书/双向 TLS；后续如需 mTLS 必须单独扩展桌面 secret-store 与证书生命周期。
- 不启动跨主机或性能实验，不修改 Windows 副本、GridFlux-Beta、CPSS(DCC)。

## 验收

- `python3 -m unittest packaging/tests/test_server_setup.py packaging/tests/test_service.py` 全部通过。
- `cmake --build build --target cpnetflux.../cpack`（按环境可用目标）以及 `ctest --test-dir build --output-on-failure` 相关门禁通过。
- dry-run 不创建文件；已有配置/证书/token 不加 `--force` 时拒绝；bundle 为 0600 且 token 不出现在 stdout/stderr。
- 生成配置可由 `cpnetflux-service --check-config` 接受；旧配置仍要求 mTLS。
- 明确记录 commit、push、远端 SHA 回读及未完成项。

## 允许修改路径

- `tools/deploy/cpnetflux-server-setup`
- `packaging/cpnetflux_server_setup.py`
- `packaging/cpnetflux_service.py`
- `packaging/server.example.json`（仅需时）
- `packaging/cpnetflux-server.service`
- `cmake/LinuxPackaging.cmake`
- `packaging/tests/test_server_setup.py`
- `packaging/tests/test_service.py`
- `docs/release/linux-install.md`
- 本任务记录。

## 回滚

恢复输入提交并删除本阶段新增的安装脚本、Python 模块、测试和文档；服务端已有配置、私钥、token、数据根目录不由回滚工具删除。若已安装服务，只能由管理员手动停用/删除 unit；代码回滚不会撤销主机侧配置。
