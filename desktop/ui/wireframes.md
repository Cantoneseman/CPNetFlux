# Qt 6/QML 页面线框与构建设计

本轮交付线框，无 QML shell 可执行程序。深圳未安装 qt6-base-dev/qt6-declarative-dev/libsecret-1-dev；不自动安装。

```text
┌ CPNetFlux ─────────────── agent已连接 / 离线·上次更新 ┐
│ 任务首页  │ [上传目录] [下载目录]                       │
│ 节点      │ 运行中 / 可恢复 / 历史                     │
│ 历史/恢复 │ 名称  文件进度  字节进度  实际模式  状态   │
│ 高级      │ 详情：已传输 / 已提交；速度 / ETA           │
└───────────┴───────────────────────────────────────────┘
```

新建步骤：方向 → 节点（连接测试）→ 源目录与目标逻辑 root → 冲突/安全预览 → 创建 / 创建并开始。路径浏览弹层：root 面包屑、分页目录、授权状态、选择按钮；权限 unknown 提示尚未确认，不显示绿色。
详情上栏：状态、requested→actual 模式和 fallback 文案；中部两条独立文件/字节进度；提交进度与传输速度分行；tabs=文件/ranges/attempts/连接/错误。ranges 展示 offset/length/generation 只读，不给普通用户任意编辑。底栏取消；暂停按能力禁用并说明 unsupported；恢复只在资格 eligible 时可点击。
节点页右侧五步连接测试，各步骤图标+具体原因；第四步附数据端口检查，数据口未通过不显示连接就绪。凭据输入走系统密钥环，不回显 token。
历史页选择旧任务，查看 commit/checkpoint/attempt；恢复前再次校验身份和能力。高级页仅展示请求预算，运行页显示实际预算；V3 小文件动态领取与大文件 ranges 共用页面，不绑定固定 channel/fileId。

实现分层：QML pages + C++ RPC client/model；agent 为独立进程/用户服务，QLocalServer 校验 Unix peer UID；QProcess 仅由 agent 持有。UI 关闭、崩溃、重开不终止 agent 或子进程。agent 重启后的孤儿任务识别需底层 PID/attempt/checkpoint 证据，不在 shell 伪造。

独立 desktop/CMakeLists.txt 规划（本轮未新增目标）：CMake>=3.20/C++20，Qt6 Core/Gui/Qml/Quick/QuickControls2/Network/Test，libsecret-1 via pkg-config；目标 cpnetflux-desktop、cpnetflux-agent、契约/Unix socket 测试。根 CMake 暂不改。
后续命令：cmake -S desktop -B desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Debug；cmake --build desktop/build --parallel 2；ctest --test-dir desktop/build --output-on-failure。当前不存在 CMakeLists，以上是计划命令，未执行。
Qt offscreen 仅验启动/模型，真实 Linux 桌面另验图形、键盘/无障碍、文件选择器、密钥环提示。不能把离线 fixture shell 标为真实传输功能。
