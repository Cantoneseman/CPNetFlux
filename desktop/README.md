# CPNetFlux Linux Desktop Shell

本目录是 CPNetFlux Linux 桌面端原型，包含中文 Qt 6/QML UI、独立 Python `cpnetflux-agent` 和明确标注的 demo adapter。00 已冻结 `desktop-agent API 2.0.0`；当前壳已接入 API2 握手、任务订阅重同步、配置和实体分页；真实传输、凭据存储与网络探测仍未实现。见 `desktop/docs/rpc-contract.md` 与 `desktop/docs/acceptance.md`。

## 构建

依赖：Qt 6.2（Core、Gui、Qml、Quick、QuickControls2、Network）、CMake 3.20+、Ninja、C++20、Python 3、Python `jsonschema`。

```bash
cmake -S desktop -B desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build desktop/build --parallel 2
ctest --test-dir desktop/build --output-on-failure
```

## 安装交付

整合构建使用 `-DCPNETFLUX_BUILD_DESKTOP=ON`，安装后二进制为 `cpnetflux-desktop`、`cpnetflux-agent`。
Linux TGZ/DEB、依赖、私有用户服务和回滚步骤见 [Linux 安装说明](../docs/release/linux-install.md)。
安装包不会自动启用服务；普通 agent 不带 `--demo`，真实方法明确返回 unsupported。

## 演示运行

必须使用私有 runtime/state 目录。演示 adapter 只推进虚拟计数，不读取或发送真实文件：

```bash
mkdir -m 700 -p "$XDG_RUNTIME_DIR/cpnetflux" "$XDG_STATE_HOME/cpnetflux-desktop"
python3 desktop/agent/cpnetflux_agent.py --demo --runtime-dir "$XDG_RUNTIME_DIR/cpnetflux" --state-dir "$XDG_STATE_HOME/cpnetflux-desktop"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software desktop/build/cpnetflux-desktop   --socket "$XDG_RUNTIME_DIR/cpnetflux/agent.sock"
```

可在第二个终端运行页面：`--page 0..6` 分别是任务首页、新建传输、详情、节点、目录、历史、高级设置。`--smoke-ms 900` 运行离屏启动门禁并退出。当前 demo 保持明确标记；真实节点探测、目录浏览、V2/V3 传输、安全凭据注入仍须按 API 2.0.0 后续实现。
