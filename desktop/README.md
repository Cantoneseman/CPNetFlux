# CPNetFlux Linux Desktop Shell

本目录是 Linux 桌面端第一版 `DESKTOP-SHELL-01`。它包含中文 Qt 6/QML UI、独立 Python `cpnetflux-agent` 和明确标注的 demo adapter。

## 构建

依赖：Qt 6.2（Core、Gui、Qml、Quick、QuickControls2、Network）、CMake 3.20+、Ninja、C++20、Python 3、Python `jsonschema`。agent 不保存密码或 token；libsecret/Secret Service 尚未接入，因此 profile 只接受 `credential_ref`。

```bash
cmake -S desktop -B desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build desktop/build --parallel 2
ctest --test-dir desktop/build --output-on-failure
```

## 演示运行

必须使用私有 runtime/state 目录。演示 adapter 只推进虚拟计数，不读取或发送真实文件：

```bash
mkdir -m 700 -p "$XDG_RUNTIME_DIR/cpnetflux" "$XDG_STATE_HOME/cpnetflux-desktop"
python3 desktop/agent/cpnetflux_agent.py --demo   --runtime-dir "$XDG_RUNTIME_DIR/cpnetflux"   --state-dir "$XDG_STATE_HOME/cpnetflux-desktop"
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software   desktop/build/cpnetflux-desktop   --socket "$XDG_RUNTIME_DIR/cpnetflux/agent.sock"
```

可在第二个终端运行页面：`--page 0..6` 分别是任务首页、新建传输、详情、节点、目录、历史、高级设置。`--smoke-ms 900` 运行离屏启动门禁并退出。真实节点探测、目录浏览和 V2/V3 真实能力仍返回 `unsupported`/未知，不会伪报成功。

agent 通过 Unix domain socket 验证同 UID、socket 0600、父目录 0700，单帧上限 1 MiB；UI 退出只断开订阅，agent 任务继续。agent 进程以用户服务方式独立管理；本 shell 不提供 systemd 安装单元。
