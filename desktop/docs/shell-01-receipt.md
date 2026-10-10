# DESKTOP-SHELL-01 回执

日期：2026-10-10；实时输入 HEAD：f83da936f6bb502a4c712ed3854069ccba3dc2b0；分支：codex/DIR-V2-TRANSFER-ENGINE-01。
范围仅 `desktop/`，未修改 src/include/tests/tools、根 CMake、保护目录或其他任务 dirty 文件；未启动跨域实验/GSI。无并行写入者/活动构建，desktop index 初始为空。

交付：
- `cpnetflux-agent.py`：同UID Unix socket、0600/0700权限、1MiB单帧、有限客户端/队列/状态预算、持久 state 原子替换、agent epoch/事件 sequence/重连快照、operation 幂等和 expected_revision。
- demo adapter：排队→扫描→连接→协商→传输→提交→完成；模拟失败生成 recoverable checkpoint，取消真实收口，完成后重复取消返回 already_terminal。client断开不取消任务。
- `cpnetflux-desktop`：Qt 6/QML 中文七页面（首页、新建、详情、节点、目录占位、历史、高级），进度、速度/ETA未知、已传输/已提交、模式/回退原因、连接五步占位、演示告示。
- 独立 `desktop/CMakeLists.txt`、Qt RPC client、schema/contract/agent/offscreen tests、构建和启动 README。

验证：
- `python3 -B desktop/tests/test_contract.py`：6/6通过。
- `python3 -B desktop/tests/test_agent.py`：6/6通过，覆盖异常/超长帧、权限、demo终态、取消、恢复、事件重放、epoch重启、凭据字段拒绝。
- `cmake -S desktop -B desktop/build -G Ninja -DCMAKE_BUILD_TYPE=Debug`：通过。
- `cmake --build desktop/build --parallel 2`：通过。
- `ctest --test-dir desktop/build --output-on-failure`：3/3通过（desktop-contract、desktop-agent、desktop-offscreen）。
- offscreen测试实际启动 agent 和 Qt 窗口，七页逐页加载并同步 1 个快照；当前无 X11/Wayland 图形会话，grabWindow像素截图不可保证，未宣称视觉交互实测。

依赖：深圳安装了 Qt 6.2 必要开发/QML模块、OpenGL开发头和 Noto CJK 字体；安装前磁盘约16GiB可用，未进行系统升级。`libsecret` 未安装、未接入；没有安全凭据存储就不能保存密码/token，profile仅保存引用。

未接入：真实 tree client / C++ library、真实 TLS/登录/能力/数据端口/目录授权探测、V2 checksum/resume/data TLS、V3动态队列/range、远端文件读取、systemd单元。界面和 demo 完成不代表 CPNetFlux真实传输完成。

下一步：00确认底层结构化 summary/event、mode能力矩阵、取消/checkpoint边界和凭据注入接口后，更新 draft-2 并串行接入真实 adapter。
