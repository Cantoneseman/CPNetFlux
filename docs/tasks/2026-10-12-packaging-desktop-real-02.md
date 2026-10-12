# PACKAGING-DESKTOP-REAL-02

日期：2026-10-12

## 范围

基于已验收候选提交 `039473f2b8478c572e23e1635c950915959c9a8e` 修复 Linux DEB 交付元数据：补齐 Ubuntu 22.04 Qt 运行依赖，并让包清单准确反映真实目录 adapter 已接入。

## 非目标

不修改传输协议、V2/V3 核心、桌面 UI、认证语义、agent API、服务启动策略或性能实验；不执行跨域实验；不把凭据、构建产物或 payload 纳入 Git。

## 验收

- Release 构建与 CPack 生成 DEB/TGZ。
- DEB `Depends` 包含 `qt6-qpa-plugins` 与 `qml6-module-qtquick-templates`。
- 包清单 `real_desktop_adapter=true`，服务不自动启动。
- 安装 staging 内容包含 core CLI、desktop、agent、user/system service 模板及 build-info。
- 仅本机可运行的 CTest 结果沿用候选提交；缺少 token 的 GridFTP smoke 单独记录为环境阻塞。

## 回滚

删除本分支/worktree，或恢复到输入提交；不操作深圳根目录 dirty 文件。
