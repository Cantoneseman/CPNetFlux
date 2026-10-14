# V3 稳定版连接与事务组合路线

- 日期：2026-10-14
- 状态：批准并分阶段实施
- 输入：5d676dccc98e9ae608c63e47a2e41181e7439e83
- 权威仓库：深圳 /root/projects/CPNetFlux

## 用户目标

安装后由服务器管理员用简短初始化命令生成认证配置和安全节点包；桌面端导入节点包、真实检查连接，然后发起文件或目录传输。底层以统一任务、文件、范围、重试、校验、恢复和提交语义承载不同连接策略。

## 决定

1. 任务事务身份贯穿 task、file、range、generation、attempt、checksum、resume 和 commit；失效身份或重复提交必须在写入/发布前拒绝。
2. 调度器只决定哪个工作项进入哪个空闲 worker；worker 独立拥有控制连接、数据连接、TLS 会话、超时和取消生命周期。动态队列保持有界且只保存元数据。
3. 数据连接在承载 framed work 前完成协商与 TLS 握手。文件或范围使用明确的 begin/data/end/result 边界；断线只回收未完成工作并保留可验证 checkpoint。
4. 能力协商发生在首个 payload 前。支持的能力组合可安全组合；不支持的组合要明确报错，不能静默切换模式或把待检查状态报告为成功。
5. 不要求所有调度、压缩和连接策略在每个任务中同时启用。产品默认选可靠、安全、已验收的组合；高级策略按能力显式启用，并记录 requested/actual mode 和降级原因。
6. 已完成的 V3 事务引擎、V1 wire 退役、服务初始化和桌面节点包导入作为基线保留。旧任务中的单独调度路径不再被视为最终架构；其余组合工作由本路线分阶段验收。

## 实施阶段

- V3-COMPOSITION-01：建立真实组合回归，覆盖持久 data session、动态队列与大文件 range、CRC32C、控制面和数据面 TLS，以及已提交任务的幂等 resume；只修复回归暴露的事务/连接缺陷。
- 后续 V3-WORKER-STATE-01：让 file/range scheduler、control pipeline 和持久数据 worker 使用共同的 dispatch/lease/cancel/recycle 契约，逐项扩大经测试的组合。
- 后续 V3-RELEASE-STABILITY-01：对支持的组合执行取消、超时、RST、服务重启、损坏 checkpoint、目标冲突及连续重复传输；仅在代码门禁通过后生成候选稳定包。

## 非目标与证据边界

- 不启动深圳—上海实验，不用 loopback 吞吐作跨域结论。
- 不宣称生产 readiness、GSI 委派/撤销已完成，或未测组合已经支持。
- 不在本路线内重做桌面 UI；桌面只消费现有 agent/core 能力和真实状态。
- 2026-10-11-v3-unified-01 的 V1 删除与 V3 事务迁移交付仍然有效；该任务不是所有连接策略组合完成的证据。

## 回滚

逐提交执行 git revert <commit>，保留已推送的服务器初始化、桌面节点导入和 V3 事务提交。测试生成物只放 /tmp/cpnetflux-runs/<task-id> 并按任务证据规则处理；不回滚用户数据、凭据或其他项目。
