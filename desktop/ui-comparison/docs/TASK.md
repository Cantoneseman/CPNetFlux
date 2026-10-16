# DESKTOP-UI-SKILL-COMPARE-01

日期：2026-10-17。状态：共同基线、原版和 A 已准备；B 等待用户指定 ui-design-system 来源。比较任务未完成；用户选择前不合入正式主线。

## 范围与非目标

只在 `D:\Project\CPNetFlux-Desktop` 开发 Qt/QML 桌面 UI 候选、样式、显示映射、预览脚本、UI 测试和任务文档。明确授权的本地预览例外不改变深圳正式源码权威。不修改 core、协议、agent API、认证实现、调度器、服务、发布打包或性能实验；不修改 `D:\Project\CPNetFlux`。

## 固定输入与核验

- GitHub remote：`https://github.com/Cantoneseman/CPNetFlux.git`；深圳核实的 SSH remote：`git@github.com:Cantoneseman/CPNetFlux.git`。
- 深圳 main：`6777dd7f5ad9bd96f8d703e932bfdc67c7a1ab28`，与 origin/main 一致。当前 dirty 为协调/任务文档和 dynamic_file_queue/work_execution core 开发；全部保留，不写入该树。
- 桌面 UI 输入：`8253126c9baaa6737b81afdbf2911802eb6671cd`，`codex/agent-live-progress-01`，工作树干净且 GitHub SHA 一致。包含上一阶段 `cec591a3d8c286b18ec43787a73a97b219b04fae` 和后续真实进度、V3 证据显示修复。
- main 与桌面分支已分叉：main 的节点包导入/认证修复不在这个桌面分支；本次不擅自交叉合并。比较只基于上述固定桌面提交，节点入口沿用其手工配置/凭据引用能力。
- 本地目标初查为空，无 Git/dirty/既有文件；由 GitHub sparse clone 导入 `desktop/`、`packaging/` 和 Git 根文件，未复制云端 runtime、payload、证书、token、密钥或构建目录。大部分传输核心未 checkout。
- 本地比较分支：`codex/desktop-ui-skill-compare-01`。原版 `desktop/ui` 保持固定输入字节，A/B 是隔离 QML 子目录，不叠加修改。

## 组织与启动

- 原版：`desktop/ui/Main.qml`。
- A：`desktop/ui-comparison/candidates/a/`，已使用本机核实的 Anthropic frontend-design；B 预留位置：`candidates/b/`，仅在收到用户指定 ui-design-system 后从同一固定输入生成。未取得 skill 前不创建冒名候选。
- 公共预览器：`desktop/ui-comparison/preview.py`；PowerShell 入口 `Preview.ps1`。只接收明确的 variant/scenario，不自动退回 demo。
- 共享数据：合成 fixture，明确水印，`rpc.connected=false`；不访问 socket、文件传输或系统凭据。按钮不制造成功结果，真实动作在设计预览中不可执行。
- Python/Qt 仅安装到本目录 `.venv`，本机 `.git/info/exclude` 明确忽略该目录，不改系统 Python；源码与截图忽略规则在本任务子目录中，原版根 `.gitignore` 也保持不变。

## API/schema 与验收

候选复用 `rpc_client.h` 的 UI 属性/动作和 `desktop-agent API 2.0.0`、adapter-boundary 的真实状态语义；未知指标不能推算，凭据只展示引用。页面覆盖任务列表、任务详情、新建、节点、断线/错误；使用一致数据与窗口参数。

验收：原版/A/B 分别启动；1280×720、1366×768、1920×1080 与 1/1.25/1.5 Qt scale-factor 矩阵；QML 错误/溢出检查和关键截图；已有 contract/source tests；尽可能验证 Qt 6.2 Linux 兼容，明确原生 Windows/PySide 预览不等于 Ubuntu GNOME/安装/真实传输验收。

## skill 来源和待解决项

- A 已发现并应用安装项：`C:\Users\12563\.claude\plugins\marketplaces\claude-plugins-official\plugins\frontend-design\skills\frontend-design\SKILL.md`；仓库 origin `https://github.com/anthropics/claude-plugins-official.git`，checkout `1a2f18b05cf5652fd25403e8d229fc884fb84103`；插件元数据无版本字段，以 commit + SKILL.md SHA256 定位。实际应用规则见 `SKILL-A.md`。
- 另在 factory 插件找到同名 frontend-design，不混用。当前 skill 列表、`.agents`、`.codex`、插件缓存、`.claude`、`.factory`、`.trae-cn`、`.copilot`、`.continue`、`.openclaw`、常见用户文档/下载路径未找到 ui-design-system；已向用户索取准确链接/路径。
- adapter-boundary 和 desktop README 的“仅 demo”介绍在输入提交中存在陈旧表述；本任务不改 API 文档事实，不将该文字当作最新实现证明，也不宣称预览验证了真实传输。

## 回滚与交接

恢复原版直接启动 `-Variant original`；A/B 不覆盖原版。不执行 reset/clean，不删除已有项目。固定输入的 tracked 文件可用 `git show 8253126:<path>` 回读；导入清单记录 Git blob 和 SHA256。比较阶段完成或受 skill 来源阻塞时，精确提交本阶段源码/文档并推送独立 codex 分支，回读 SHA；不提交 `.venv`、截图大包、构建或私密数据。用户选定后另开正式合入任务，本阶段不作方案选择。
