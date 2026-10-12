# DESKTOP-UI-DEMO-03 范围与验收

输入提交：eaadeb24490e7e7246766efce94f01a6d41c5b68。真实演示事实来自 Ubuntu 22.04 GNOME/VMware NAT：Qt UI 需从图形会话启动；当前 agent 为 demo 合成状态，不代表真实传输。

范围：
- 调整 Qt/QML 信息层级，突出“传输客户端”、agent/API/demo 连接状态和下一步操作。
- 适配约 900px 宽的 GNOME 窗口与常见中文字体比例。
- 保持真实传输、节点探测、目录浏览、TLS/checksum/速度等事实边界。

非目标：
- 不改 desktop-agent、IPC schema、核心协议、真实 adapter、凭据注入、安装器或隔离安装文档。
- 不启动跨主机、性能、RDMA 或 VMware 网络实验。

受影响文件：
- desktop/ui/Main.qml
- desktop/ui/main.cpp（仅增加 smoke 窗口尺寸参数）
- desktop/tests/test_ui.py
- desktop/docs/ui-demo-03-scope.md

验收：
- Qt CMake build 与完整 CTest 通过。
- 离屏 smoke 覆盖 1180px、900px 与最小 760px 窗口，页面无 QML 错误。
- UI 退出后 demo agent 仍存活；不兼容 API 只进行 getInfo 握手。
