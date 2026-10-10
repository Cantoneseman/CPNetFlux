# DESKTOP-SHELL-01 v1
日期：2026-10-10。输入 HEAD：f83da936f6bb502a4c712ed3854069ccba3dc2b0。
范围：desktop/ 独立 CMake、Qt6/QML中文界面、Python Unix socket agent、demo adapter、测试与运行说明。
非目标：底层修改、真实传输接入、账号系统、真实凭据、跨域/GSI实验、并行写index。
文件：CMakeLists.txt、.gitignore、agent/cpnetflux_agent.py、ui/{main.cpp,rpc_client.h,rpc_client.cpp,Main.qml}、tests/test_agent.py、tests/test_ui.py、schemas/desktop-agent.schema.json、docs/{rpc-contract.md,desktop-shell-01.md,shell-01-receipt.md}、README.md。
验收：schema旧门禁与真实agent测试；畸形/超长帧、socket权限/同UID、unknown/unsupported、demo终态/取消/恢复、断开继续、事件重放、epoch重启、节点profile安全保存；Qt offscreen真实窗口加载及截图，独立CMake/CTest；真实图形会话缺失时不宣称交互桌面实测。
预算：构建-j2，现有磁盘16GiB/可用内存2.7GiB。只安装Qt6必要开发/QML运行模块及中文字体，不升级系统。agent依赖Python3/jsonschema>=3.2（现有）。无libsecret接入，不安装它、不允许保存凭据值。
执行顺序：失败行为门禁→agent实现/schema补充→Qt UI→独立构建/CTest→精确提交/推送/远端回读。
\n截图：offscreen backend 可启动并加载窗口；无 X11/Wayland 图形会话时 grabWindow 可能不产生像素文件，截图检查按可取得证据处理。\n