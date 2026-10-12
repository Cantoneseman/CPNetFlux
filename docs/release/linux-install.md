# Linux 交付候选安装指南

本包是内部安装候选，尚未选择项目 LICENSE，不公开发布或宣称生产就绪。包含已整合的 V3 单文件/目录引擎、mTLS/token 服务、Qt 桌面和本地 agent。桌面 API2 的真实传输 adapter 尚未接入；只有显式 --demo 才模拟任务，不能用于双机传输证明。真实传输现在通过 CLI 完成。

## 构建和打包

支持现场验收平台 Ubuntu 22.04 x86_64；其他发行版应重新构建验证。需要 GCC/CMake、zlib/OpenSSL/spdlog 开发包；桌面需 Qt6.2 开发包、Python3/jsonschema 和 QtQuick/Controls/Layouts/Window/WorkerScript 的 QML 模块。DEB 自动计算 ELF 依赖并声明 Python/QML 运行依赖；TGZ 使用系统运行库，不是静态或跨发行版通用包。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DCPNETFLUX_ENABLE_TLS=ON -DCPNETFLUX_BUILD_DESKTOP=ON
cmake --build build -j2
CPNETFLUX_TEST_TOKEN="$(openssl rand -hex 32)" ctest --test-dir build --output-on-failure
python3 tools/release/create_linux_packages.py --build-dir build --output-dir /absolute/output
```

上述 token 是仅供 loopback CTest 的一次性测试值，不是部署凭据，不要复用。

打包器要求干净的已提交输入，并核对 build-info.json 的 source_revision。产出 TGZ、DEB、linux-package-manifest.json、SHA256SUMS。客户端/服务命令安装在 bin；不安装 benchmark、测试、凭据、payload。未配置 maintainer scripts，不自动创建用户、启动服务或开放端口。

## 安装

检查 SHA256SUMS，核对 manifest 的 revision 和依赖。在 Debian/Ubuntu 使用 `sudo apt install ./cpnetflux_..._amd64.deb`；桌面可从应用菜单或 `cpnetflux-desktop` 启动。TGZ 解到独立目录（包含 usr/bin、usr/libexec、usr/share）；把 usr/bin 加入 PATH。wrapper 相对自身定位 agent/schema，无构建路径依赖。TGZ 需自行安装 manifest/DEB 列出的系统依赖。

源码开发验收使用隔离 `DESTDIR=/tmp/staging cmake --install build`，不等于宿主机已安装。不要把安装 prefix 放进可由不可信用户修改的目录。

## 服务准备（管理员显式操作）

示例均在 /usr/share/cpnetflux/deployment；不会自动覆盖 /etc。

1. 使用 `systemd-sysusers /usr/share/cpnetflux/deployment/cpnetflux.sysusers` 创建专用无登录用户；使用 `systemd-tmpfiles --create /usr/share/cpnetflux/deployment/cpnetflux.tmpfiles` 创建受控目录。
2. 首次安装才复制 server.example.json 到 /etc/cpnetflux/server.json，root:cpnetflux、0640。保持 enabled=false，先配置实际 CA、服务器证书/私钥、token。token 和私钥必须 cpnetflux:cpnetflux、0600；配置和证书不可被组/其他用户写入。token 只通过路径交给核心，不写在 argv/env/RPC。
3. 数据仅放 /var/lib/cpnetflux/data（或显式调整 unit 的 ReadWritePaths）；服务不以 root 运行。选择 read-only/write-only/read-write 权限，核对已有数据备份。将 enabled 改为 true；按 cpnetflux 用户运行 `cpnetflux-service --check-config`。该检查不代表 TLS 证书链/有效期或 token 登录已通过。
4. 复制 server unit 到 /etc/systemd/system，`systemd-analyze verify /etc/systemd/system/cpnetflux-server.service`，daemon-reload 后显式 start。先在 127.0.0.1 验证，再选择监听地址和防火墙。控制端口默认 2121；被动端口窗口为 data_port_base 起 512 个端口，须只向授权对端开放。
5. 客户端用 --help 核对 --tls-mode required、--tls-ca-file、--tls-cert-file、--tls-key-file、--auth-token-file、--data-tls-mode required；先做小文件双向 hash/manifest 验证。不能用匿名/off TLS 作为交付默认。服务级访问策略并非多用户 ACL 或账号管理。

TGZ 自定义 prefix 使用 unit 前必须把 ExecStart 改为实际绝对路径。包内样例路径按 DEB 的 /usr 布局生成。没有自动执行这些管理员操作。

## 桌面与 agent

同一普通用户运行 `cpnetflux-agent` 和 `cpnetflux-desktop`。需要私有 XDG_RUNTIME_DIR；agent socket 0600、状态目录0700。可复制 user unit 到 ~/.config/systemd/user，再显式启动；不会启用 demo。UI 退出不停止 agent，不取消任务。

演示可另用私有 runtime/state 目录运行 `cpnetflux-agent --demo`，再用 `cpnetflux-desktop --socket /absolute/runtime/agent.sock` 连接。演示数据是合成的；真实 adapter 缺口见 desktop/agent/adapter-boundary.md。无图形会话的 offscreen 检查不能代替真实桌面交互验收。

## 升级、卸载与回滚

升级前停止本服务和用户 agent，备份 /etc/cpnetflux、/var/lib/cpnetflux、用户 ~/.local/state/cpnetflux-desktop 及证据；保留上一版包和 manifest。安装新包后先配置检查，再做小规模回归，最后恢复服务。示例始终在 share，不覆盖现有配置。

DEB 使用 apt remove cpnetflux 移除包文件；TGZ 使用独立版本目录和 PATH 切换。手工安装的 unit/sysusers 与配置不归包删除，管理员按需要停用 unit。卸载不删除数据、checkpoint、凭据或状态。回滚停止服务后回装旧包/切换旧目录，并使用备份状态；不承诺未来状态 schema 降级兼容。不要直接删除数据根目录。
