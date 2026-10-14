# Linux 交付候选安装指南

本包是内部安装候选，尚未选择项目 LICENSE，不公开发布或宣称生产就绪。包含已整合的 V3 单文件/目录引擎、TLS/token 服务、Qt 桌面和本地 agent。桌面 API2 的真实 adapter 当前支持通过 core tree client 执行 TLS/token 保护的真实目录上传/下载；单文件任务因 core file client 尚未提供统一 TLS/认证控制面，会明确返回 unsupported。只有显式 --demo 才模拟任务，demo 数据是合成状态，不能用于双机传输证明。

## 构建和打包

支持现场验收平台 Ubuntu 22.04 x86_64；其他发行版应重新构建验证。需要 GCC/CMake、zlib/OpenSSL/spdlog 开发包；桌面需 Qt6.2 开发包、Python3/jsonschema、qt6-qpa-plugins、qml6-module-qtquick-templates 和 QtQuick/Controls/Layouts/Window/WorkerScript 的 QML 模块。DEB 自动计算 ELF 依赖并声明 Python/QML 运行依赖；TGZ 使用系统运行库，不是静态或跨发行版通用包。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_BUILD_DESKTOP=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
python3 tools/release/create_linux_packages.py --build-dir build --output-dir /absolute/output
```

打包器要求干净的已提交输入，并核对 build-info.json 的 source_revision。产出 TGZ、DEB、linux-package-manifest.json、SHA256SUMS。客户端/服务命令安装在 bin；不安装 benchmark、测试、凭据、payload。未配置 maintainer scripts，不自动创建用户、启动服务或开放端口。

## 安装

检查 SHA256SUMS，核对 manifest 的 revision 和依赖。在 Debian/Ubuntu 使用 `sudo apt install ./cpnetflux_..._amd64.deb`；桌面可从应用菜单或 `cpnetflux-desktop` 启动。TGZ 解到独立目录（包含 usr/bin、usr/libexec、usr/share）；把 usr/bin 加入 PATH。wrapper 相对自身定位 agent/schema，无构建路径依赖。TGZ 需自行安装 manifest/DEB 列出的系统依赖。

源码开发验收使用隔离 `DESTDIR=/tmp/staging cmake --install build`，不等于宿主机已安装。不要把安装 prefix 放进可由不可信用户修改的目录。

## 服务准备（管理员显式操作）

安装包不会自动创建服务、生成凭据、启动服务或开放端口。安装完成后，管理员在服务器上显式运行初始化向导；先用 dry-run 查看计划：

```sh
sudo cpnetflux-server-setup --dry-run \
  --listen 0.0.0.0:2121 --advertise-host transfer.example \
  --tls-server-name transfer.example --data-port-base 20200 \
  --data-root /var/lib/cpnetflux/data --node-id lab-node
```

确认地址、数据根目录和端口后，执行同一组参数去掉 `--dry-run`。向导会生成专用服务用户、TLS CA/服务器证书、token、`/etc/cpnetflux/server.json`、受限 systemd unit（仅在加 `--install-service` 时）以及 `0600` 的 `.cpnetflux` 节点导入包。默认不会 enable/start 服务；需要时分别显式添加 `--install-service --enable --start`。防火墙也必须显式添加 `--configure-firewall`。导入包包含 token 和 CA 公钥，只能通过管理员认可的安全通道交给对端，导入后删除。

服务端的新初始化配置使用 TLS 服务端身份校验 + token，不要求客户端证书，以匹配当前桌面认证闭环。已有配置若没有 `tls_client_auth` 字段，启动器继续按 `required` 处理；已有 mTLS 节点不会被静默降级。服务配置中的 token、私钥由专用服务用户读取，配置由 root 管理。

当前控制服务的数据端口分配窗口固定为 `data_port_base` 到 `data_port_base+511`，例如 `20200-20711`；`connections` 不会把防火墙窗口缩小。初始化向导显示并按这个实际窗口生成 UFW/firewalld 规则。先在受控网段验证，再按网络策略放行控制端口与整段数据端口。

也可以手工维护 `server.json`，但必须保持 `enabled=true`、绝对路径、受信文件所有者与权限，并由专用非 root 用户运行 `cpnetflux-service --check-config`。该检查不替代 TLS 证书链、token 登录和双机传输验证。

## 桌面与 agent

同一普通用户运行 cpnetflux-agent 和 cpnetflux-desktop。需要私有 XDG_RUNTIME_DIR；agent socket 0600、状态目录0700。可复制 user unit 到 ~/.config/systemd/user，再显式启动；不会启用 demo。UI 退出不停止 agent，不取消任务。

首次连接其他节点时，在 UI 的“节点连接配置”中填写节点地址、控制端口、TLS 主机名和 CA 文件；认证方式选择“短期 token 文件（支持）”，凭据引用填写 file:/绝对路径/token。这个文件必须已存在、当前用户可读且权限为 0600，内容只留在本机文件中，绝不填入 token 原文。保存后点击“认证检查（不传输文件）”。检查会真实执行 DNS/TCP、TLS 握手、token 登录和 V2 能力协商；成功时显示“已认证”，失败时按步骤给出修复原因。认证检查不会创建远端目录、文件或数据连接，因此可在正式传输前重复执行。管理员凭据、secret-service 引用和匿名/off-TLS 目前会明确显示为不可用或失败。

认证通过后再到“新建任务”选择该节点的目录上传/下载。真实单文件任务仍会明确显示 unsupported；只有显式 --demo 才模拟任务，演示数据不能证明双机传输。无图形会话的 offscreen 检查不能代替真实桌面交互验收。

## 升级、卸载与回滚

升级前停止本服务和用户 agent，备份 /etc/cpnetflux、/var/lib/cpnetflux、用户 ~/.local/state/cpnetflux-desktop 及证据；保留上一版包和 manifest。安装新包后先配置检查，再做小规模回归，最后恢复服务。示例始终在 share，不覆盖现有配置。

DEB 使用 apt remove cpnetflux 移除包文件；TGZ 使用独立版本目录和 PATH 切换。手工安装的 unit/sysusers 与配置不归包删除，管理员按需要停用 unit。卸载不删除数据、checkpoint、凭据或状态。回滚停止服务后回装旧包/切换旧目录，并使用备份状态；不承诺未来状态 schema 降级兼容。不要直接删除数据根目录。
