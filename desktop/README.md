# CPNetFlux Linux Desktop Shell

本目录包含中文 Qt 6/QML UI、独立 Python `cpnetflux-agent`，以及明确区分的 demo/real adapter。真实适配器通过 CPNetFlux 核心客户端执行有 TLS/认证保护的目录上传、下载和恢复；目录浏览、暂停等未实现的方法仍返回 unsupported。当前本机 IPC 合同为 `desktop-agent API 2.1.0`，该版本号独立于 V3 传输引擎。见 `desktop/docs/rpc-contract.md` 与 `desktop/docs/acceptance.md`。

## 构建

依赖：Qt 6.2（Core、Gui、Qml、Quick、QuickControls2、Network）、CMake 3.20+、Ninja、C++20、Python 3、Python `jsonschema`。

```bash
cmake -S desktop -B desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build desktop/build --parallel 2
ctest --test-dir desktop/build --output-on-failure
```

## 演示运行

必须使用私有 runtime/state 目录。演示 adapter 只推进虚拟计数，不读取或发送真实文件：

```bash
mkdir -m 700 -p "$XDG_RUNTIME_DIR/cpnetflux" "$XDG_STATE_HOME/cpnetflux-desktop"
python3 desktop/agent/cpnetflux_agent.py --demo --runtime-dir "$XDG_RUNTIME_DIR/cpnetflux" --state-dir "$XDG_STATE_HOME/cpnetflux-desktop"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software desktop/build/cpnetflux-desktop   --socket "$XDG_RUNTIME_DIR/cpnetflux/agent.sock"
```

可在第二个终端运行页面：`--page 0..7` 分别是任务首页、新建传输、详情、节点、目录、历史、本机网络绑定、高级设置。`--smoke-ms 900` 运行离屏启动门禁并退出。demo 使用合成数据，不执行真实传输。

## 本机源地址绑定

网络页调用 `node.network.inventory/plan/apply/verify`，读取本机接口并生成推荐方案。应用要求明确确认和未变化的清单版本；请求链路数大于可用接口数时不能应用。绑定按远端节点持久保存，V3 控制/数据连接使用相应源地址，启动前重新验证地址仍可用。

本阶段不会修改系统 IP、路由、防火墙或网卡状态；出站接口仍由现有路由决定。逐链路远端地址映射尚未接入，非空 `peer_addresses` 返回 unsupported。地址检查不等于真实物理路径或吞吐验收。
