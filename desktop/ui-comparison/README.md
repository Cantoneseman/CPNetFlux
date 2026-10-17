# CPNetFlux 桌面 UI skill 对比

这是一份明确获准的本地 UI 候选工作区，**不连接真实 agent，不传输文件**。窗口标题和底部水印始终标明设计预览，数据均为固定合成 fixture。深圳仍是正式源码权威。

当前：原版、A 和 article-guided B 均可运行；**比较任务尚未完成，不作方案选择。** B 的来源是用户确认的 Lenny's Newsletter 设计流程文章，不冒称为 `ui-design-system` 技能；具体记录见 `docs/SKILL-B.md`。

从 `D:\Project\CPNetFlux-Desktop` 执行：

```powershell
# 原版 / A，两者使用同一份任务数据
.\desktop\ui-comparison\Preview.ps1 -Variant original
.\desktop\ui-comparison\Preview.ps1 -Variant a

# 页码：0任务、1新建、2详情、3节点/认证、4目录、5历史、6诊断
.\desktop\ui-comparison\Preview.ps1 -Variant a -Page 3
.\desktop\ui-comparison\Preview.ps1 -Variant a -Page 2 -Scenario scanning
.\desktop\ui-comparison\Preview.ps1 -Variant a -Page 2 -Scenario failed
.\desktop\ui-comparison\Preview.ps1 -Variant a -Scenario offline
.\desktop\ui-comparison\Preview.ps1 -Variant a -Width 1280 -Height 720 -Scale 1.5

# 候选 B：article-guided Signal Desk 方向
.\desktop\ui-comparison\Preview.ps1 -Variant b
.\desktop\ui-comparison\Preview.ps1 -Variant b -Page 2 -Scenario failed
```

也可以双击根目录 `Preview-Original.cmd`、`Preview-A.cmd` 或 `Preview-B.cmd`。导航、输入框、节点配置编辑和技术详情可以本地查看；创建、取消、恢复和连接检查不会执行真实操作。三种 variant 必须显式指定，缺少候选时启动会报明确错误，绝不自动回退到 A 或 demo。

全新机器安装预览环境：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r desktop\ui-comparison\requirements-preview.txt
```

预览使用 Windows 原生 Qt 6.8.3/PySide6 加载真实 QML 文件。实际 `rpc_client.*`、agent、schema 和原版 QML 保持输入提交内容。前端候选正式接入 Linux 客户端时仍需针对 Qt 6.2、真实 agent、授权和 Ubuntu GNOME 另行验收。

参数宽高是 **Qt 逻辑窗口尺寸**；scale 1/1.25/1.5 是显式 Qt 缩放，截图像素会随缩放增加。没有声称验证 Windows 系统 DPI 设置切换，也没有把 1280×720 逻辑窗口冒充 150% 下同像素大小的物理屏幕。原版固定高度表单和最小窗口约束保留，便于公平观察问题。

## 方案 A

实际 skill：本机已安装的 Anthropic 官方 `frontend-design`（不是 factory 同名副本）。准确路径、来源 commit、SHA256 见 `docs/SKILL-A.md`。

方向：工业工作台。墨绿色导航配明亮黄绿强调，浅灰绿背景，克制的卡片，较大的标题和等宽任务计数。按钮、表单和进度条使用一致样式，主动作和键盘焦点有清晰区分。概览增加任务总数/未结束/完成数、路径搜索和状态筛选；新建传输拆成路径、节点和安全提交区，节点入口分为基础连接、TLS/身份与分步检查。表单高度由内容决定，“添加新节点”只清空本地表单。不新增 API 或替代状态机。

适配成本：候选集中于一份 QML，可在后续正式任务拆出样式组件和颜色参数；正式合入前要核对主线节点包导入/认证修复，不能直接覆盖主线 Main.qml。字体在 Windows 使用本地字体，Linux 回落 Noto Sans CJK SC；字宽和 Qt 6.2 仍需实际复核。

## 方案 B

实际来源：用户确认的 Lenny's Newsletter 文章，按 `article-guided` 标注；文章是设计流程参考，不是安装的 `ui-design-system` skill。来源、采用的 Discover/Define/Deliver 思路和删减记录见 `docs/SKILL-B.md`。

方向：传输信号台（Signal Desk）。B 从同一固定输入的原版 QML 独立生成，保留原生 Qt 控件和原版信息结构，但使用暖纸色画布、珊瑚动作色、深蓝墨色和紧凑边框；任务路径、agent 状态、阶段、错误和核验证据保持为唯一信息来源。B 刻意不加入图像、渐变、假动画或额外指标，未知字段仍显示“暂无/未提供”。

适配成本：B 仍集中在一份独立 QML；与 A 一样，正式合入前要核对主线节点包导入/认证修复、Qt 6.2 字体和 Ubuntu GNOME 尺寸。B 的 native screenshot 与矩阵结果写入忽略的 `evidence/`，不上传截图大包。

## 验证和来源

任务边界：`docs/TASK.md`；输入文件清单/摘要：`docs/import-manifest.json`；测试记录：`docs/VALIDATION.md`；矩阵结构化结果：`docs/render-results.json`。截图仅保存在忽略的 `evidence/`，不上传 GitHub 大包。

```powershell
.\.venv\Scripts\python.exe -X utf8 desktop\tests\test_contract.py
.\.venv\Scripts\python.exe -X utf8 desktop\tests\test_ui_source_contract.py
.\.venv\Scripts\python.exe -X utf8 desktop\ui-comparison\tests\test_preview_contract.py
.\.venv\Scripts\python.exe -X utf8 desktop\ui-comparison\tests\render_matrix.py --variants original a
```

恢复原版只需启动 `-Variant original`，不用回滚或重置任何项目。A/B 都从固定基线独立产生；用户选定前不合入深圳、不打正式安装包。
