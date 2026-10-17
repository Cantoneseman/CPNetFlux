# 方案 B：article-guided（Lenny's Newsletter）

- 来源文章：[How to turn your AI into a world-class designer](https://www.lennysnewsletter.com/p/how-to-turn-your-ai-into-a-world-class-designer)
- 来源日期：2026-09-01（页面显示日期）；本文不是本地技能包，也没有被冒称为 `ui-design-system`。
- 应用范围：只把文章中的设计流程用于候选 B 的视觉方向和审查标准，不改变 CPNetFlux 的 RPC、schema、真实状态语义或安全边界。

## 采用的流程

1. **Define**：把桌面端定义成“传输信号台（Signal Desk）”，用纸张、标记色和清晰的状态层级表达真实后端证据，而不是用装饰模拟传输。
2. **Deliver**：以编辑式工作台为方向：暖纸色画布、珊瑚红动作色、深蓝墨色文字、紧凑的边框和不依赖渐变的分区。导航保持可见，任务列表、详情和配置使用同一套信息层级。
3. **去除无价值元素**：不加入图片、渐变、假动画、虚构速度或额外指标；未知字段继续显示“暂无/未提供”，不可操作项继续禁用并解释原因。

## 设计批评记录

- 目标质量：用户一眼看出真实传输与开发诊断的区别，能从任务状态、阶段和核验证据找到下一步。
- 首轮删减：移除演示性的装饰图形和与任务无关的指标；保留路径、状态、证据、错误和 agent 连接信息。
- 约束：候选 B 从固定输入 `8253126c9baaa6737b81afdbf2911802eb6671cd` 的原版 `desktop/ui/Main.qml` 独立生成；不以候选 A 的源码为基底，不访问 socket、凭据、文件系统传输或真实 agent。

## 已知边界

这是 article-guided 设计比较候选，不是生产合入或真实传输 readiness 证明。预览使用隔离的合成 fixture，启动窗口始终显示“设计预览 / 未连接真实 agent”水印；正式接入仍需 Linux Qt 6.2、agent socket、认证和 Ubuntu GNOME 安装验收。
