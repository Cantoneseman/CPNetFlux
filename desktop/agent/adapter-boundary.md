# Agent 与底层串行接入边界

本轮无 agent 实现，仅契约与设计。推荐独立用户服务持有任务生命周期，UI 不持有传输进程。首版可包装 tree upload/download 客户端，但只消费经过版本核验的 JSONL/summary/checkpoint；人类 stdout 不是唯一事实源。

需 00 提供：结构化 schema/version 与稳定文件/range/attempt identity；实际 mode/fallback 和协商安全策略；取消入口及完成竞态/manifest 收口；resume eligibility 与 checkpoint 身份；节点 capabilities、目录浏览和 root 授权探测；token 安全注入入口（不能 argv/env/日志）。缺字段置 unknown/null，不能以进程退出或发送完毕补成 completed。

适配配置：requested v2→底层 persistent_tree；V3 动态队列及 range 由 scheduler/range policy 传入并记录实际值。UI/agent 不自行切文件、计算危险 range 或写核心 manifest。channel/queue/pending 必须经过双方能力和资源上限约束。profile 更新不改变运行 attempt 已冻结配置。token 仅通过 libsecret 获取，安全注入尚未收口前真实任务启动 blocked。

底层 fallback 在 payload 前决定；agent 记录原因与实际策略，不以自动降安全换 V2。取消 accepted→cancelling→backend quiescent+checkpoint evidence→终态；resume 新 attempt，以 verified checkpoint 事实补缺范围。agent 重启先检查进程归属和 checkpoint；UI 重连走原子订阅快照，禁止直接启动重复 attempt。
