# 方案 A：frontend-design

- 本地准确路径：`C:\Users\12563\.claude\plugins\marketplaces\claude-plugins-official\plugins\frontend-design\skills\frontend-design\SKILL.md`。
- 元数据名称：`frontend-design`；作者：Anthropic。
- 来源：`https://github.com/anthropics/claude-plugins-official.git`。
- 本地来源 checkout：`1a2f18b05cf5652fd25403e8d229fc884fb84103`。插件无独立版本字段。
- SKILL.md SHA256：`336d388ea4181c22fc5662d2b0a0cadc6c2a613ca0be52030536d2693b458ff7`。
- 同名 factory 插件没有应用。用户的 `fronted-design` 按已安装的 `frontend-design` 解释；若用户后续指定另一来源，A 应从共同基线重新制作，不叠加本方案。

应用规则：先确定 industrial/utilitarian 方向；用鲜明主色/强调色与统一颜色参数保持识别；用标题、等宽数字与正文形成层级；精确调整留白、卡片和控件密度；只加按钮 hover 的短色彩过渡，不用动画伪造传输进度。该 skill 面向 Web 的 CSS/React 建议翻译为 Qt/QML 样式和组件，没有引入 Web/Electron/Tauri。

改动只在 `candidates/a/Main.qml`。它直接从固定 `8253126` 的 `desktop/ui/Main.qml` 复制，保留 RPC、事件连接、目录选择、取消/恢复条件与后端证据映射；没有先应用其他 UI 优化 skill。公共预览器与 fixture 同样用于原版和将来的 B。

当前已知限制：Windows CJK 正文采用可用系统字体保证中文显示，产品字标使用 Bahnschrift，数字/技术标识使用 Consolas；Ubuntu 回落字体和 Qt 6.2 的表现需要另验。此候选是设计比较输入，不是生产 readiness 证明。
