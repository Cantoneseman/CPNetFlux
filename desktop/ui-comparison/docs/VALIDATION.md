# 本地比较检查点验收

日期：2026-10-17。输入：`8253126c9baaa6737b81afdbf2911802eb6671cd`。原版与方案 A 已可运行；B 尚无指定 skill 来源，未创建。**本比较阶段未完成，也未要求用户选择 A。**

## 已运行且通过

工作目录均为 `D:\Project\CPNetFlux-Desktop`，Python 为本目录 `.venv\Scripts\python.exe`：

| 命令 | 实际结果与边界 |
|---|---|
| `python -X utf8 desktop/tests/test_contract.py` | 8/8 通过，离线 schema/API 契约 |
| `python -X utf8 desktop/tests/test_ui_source_contract.py` | 通过，固定原版 source contract |
| `python -X utf8 desktop/ui-comparison/tests/test_preview_contract.py` | 5/5 通过；确认 fixture 永不报告真实连接/操作成功、未知扫描指标保留 null、原版/agent/schema Git blob 未改、A 保留 RPC 边界、缺少 B 时明确失败 |
| `python -X utf8 desktop/ui-comparison/tests/render_matrix.py --variants original a` | 原版 63 + A 63 = 126 次 Qt/QML 加载/渲染，无被检测到的 QML 错误；A 的最终版本单独重跑矩阵覆盖旧结果 |
| `wsl -d Ubuntu-22.04 -- bash -lc 'cd /mnt/d/Project/CPNetFlux-Desktop && python3 -B -m unittest discover -s desktop/tests -p test_agent.py -v'` | 9/9 通过；8 个 demo/API 单元测试和 1 个确定性 core 事件生产器测试。测试仅使用临时目录，非真实传输验收 |
| `python -X utf8 desktop/ui-comparison/preview.py --variant a --page 3 --width 1366 --height 768 --smoke-ms 400 --screenshot desktop/ui-comparison/evidence/a-node-native-final.png` | Windows 原生图形窗口启动/截图成功，`real_agent_connected=false`、QML 错误为空 |
| 同一 native 入口 `--page 2 --scenario failed` | 失败状态窗口启动/截图成功，无 QML 错误 |
| `python -X utf8 -m py_compile`（预览器、fixture、两个测试脚本） | 通过 |
| `git diff --check` | 通过；未修改原版 tracked 源码 |

矩阵页面/状态：任务概览、任务详情、新建、节点/认证、扫描详情、失败详情、agent 离线。每项覆盖 1280×720、1366×768、1920×1080 **逻辑窗口** 和显式 Qt scale-factor 1/1.25/1.5。结构化结果见 `render-results.json`。关键截图在忽略的 `evidence/` 中；查看过任务概览、详情和节点页面，截图摘要另存 `screenshot-index.json`，截图不进 GitHub。

## 环境与尚未验证

- Windows：Python 3.12、PySide6/Qt 6.8.3，Fusion Controls、软件渲染；本地 venv 安装锁定版本。offscreen 平台提示 Qt 不附带 fonts 目录，预览器已显式加载本机 CJK/Segoe/Consolas 字体；该提示不影响截图或 QML 加载。native 图形窗口没有该提示。
- 原生 Windows CMake/Ninja/编译器未发现完整链路；WSL 有 CMake/Ninja/g++，未发现 Qt6 开发依赖。因此没有宣称本阶段 C++ 全套 CMake/CTest 或 Qt RPC C++ 测试已运行，也没有安装全套新工具链。现有 agent/contract/source 门禁与实际 QML 渲染已按上表执行。
- 矩阵是加载/渲染门禁，不能等同于人工逐控件的键盘、鼠标、屏幕阅读器和溢出验收。部分长页面通过滚动访问下部内容；原版固定高度问题保留，A 按内容高度布局。
- 没有切换 Windows 系统 DPI 设置；native 窗口会叠加 OS 当前 DPI，offscreen 使用可重复的 Qt 显式缩放。没有把逻辑窗口和物理屏幕混为一谈。
- 未运行 Ubuntu Qt 6.2/GNOME 会话/安装验收、真实 agent/socket/传输、跨域/性能实验。按任务不打正式 DEB。
- main 上节点包导入/认证修复与本比较输入分叉；此处保留手工节点/凭据引用入口，正式合入前另行核对。
- B 需要用户提供 `ui-design-system` 的准确来源；收到后从同一固定基线独立制作，不以 A 为起点。

## 下一步

取得 B 的指定 SKILL.md；记录来源/版本/摘要；串行制作 B；运行同一数据、页面、尺寸和缩放矩阵；交付原版/A/B 对照并停在用户选择步骤。用户选择前不合并、发布或替用户决定。
