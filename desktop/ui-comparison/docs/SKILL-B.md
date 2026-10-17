# 方案 B：article-guided（Lenny's Newsletter）

- 来源文章：[How to turn your AI into a world-class designer](https://www.lennysnewsletter.com/p/how-to-turn-your-ai-into-a-world-class-designer)
- 页面日期：2026-09-01。
- 这篇文章是用户选定的设计流程来源，按 `article-guided` 标注；它不是本机安装的 `ui-design-system` skill。
- 设计候选从固定输入 `8253126c9baaa6737b81afdbf2911802eb6671cd` 的原版 `desktop/ui/Main.qml` 独立重做，不以候选 A 或旧版 B 为底稿。

## 设计 brief 与迭代

用户反馈首轮 B 只换了颜色和卡片，和已有 UI 区别不够。按此反馈，B 的第二轮不再保留原有页面组织，而改为高密度的 Mission Control：左侧运维导航、常驻 socket/API 健康状态、任务队列与快捷操作，以及独立的新建向导、任务指挥、节点身份、文件分段、历史和能力诊断页。配色改为深海军蓝、薄荷绿和清晰的告警色，优先让状态与动作一眼可分。

## 采用的设计流程

1. **Discover**：从真实产品边界开始。agent 掌握任务生命周期、持久化和状态事实；UI 只能从 RPC/schema 显示结果，不能维护第二套状态机。真实操作必须通过现有 `RpcClient`。
2. **Define**：把产品定义成面向操作人员的传输控制台，核心任务是确认 agent 可用、发起任务、找到失败原因并执行后端已支持的恢复动作。为以后能力预留成组入口，但明确区分可调用和未实现接口。
3. **Deliver**：重排七个页面，提供目录/文件选择、传输创建步骤、任务阶段与证据、节点与 TLS 配置、文件与 range 分页、恢复资格、扩展点和诊断区。已有 RPC 操作调用 `rpc.create`、`rpc.action`、`rpc.loadDetails`、`rpc.loadNextFiles`、`rpc.loadNextRanges`、`rpc.saveProfile`、`rpc.testNode`、`rpc.requestCapabilities` 和 `rpc.refresh`；没有对应 agent API/schema 的入口置灰并写明缺口。
4. **Critique and iterate**：检查首页能否立即区分在线/离线、用户是否能找到主动作、是否把示例配置误当成真实状态。第二轮去掉了示例主机、CA 文件和凭据引用；没有 agent 授权根目录 ID 时禁用真实任务创建；CA 路径选择复用现有文件选择器。客户端证书、私钥、token 文件导入、暂停、日志导出、批量操作等当前没有安全接口的动作保持禁用。

## 真实性与安全约束

- 任务状态、计数、进度、速度、文件清单、阶段和恢复资格只从 agent 结构化结果读取；未知值显示“未提供”或“未知”。
- B 的比较预览继续使用隔离合成 fixture，窗口标题和水印明确写“设计预览 / 未连接真实 agent / synthetic fixture”。预览按钮不会创建任务，也不会声称真实操作成功。
- 生产连接状态不得由 fixture 填充。界面继续使用已有 RPC 和 adapter-boundary；没有改写 agent 协议、状态机、认证或存储。
- UI 不接收 token、密码或私钥值。profile 使用 agent/系统 secret store 的 `credential_ref`。匿名/TLS 绕过不作为入口。

## 适配范围

候选 B 只改独立 QML 和设计预览测试/记录。其 Windows Qt 6.8.3/PySide6 渲染检查不能代替 Linux Qt 6.2、Ubuntu GNOME、真实 socket/传输和安装包验收。用户选方案前不合入生产 UI。
