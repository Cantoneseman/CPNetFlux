# 有界客户端恢复：按批次重建而非复活 cancelled queue

状态：accepted，依据用户继续开发与两台阿里云互传验收的授权。输入 28bc94e。

现有 sender requestAbort、目录 cancelAll 会永久取消 queue；服务端同批次持有共享接收事务。直接 reopen 或在旧 writer 退出前新建批次不安全。采用完整 attempt 边界：旧线程 join/连接销毁，认证后的 batch-id 退出屏障确认远端旧事务消失，再从稳定 tree/文件 manifest 建立新 queue/epoch、重放恢复握手。03 计划中直接将 recover 接入原 cancelled queue 的步骤被此方案取代；recover 原语自身仍有效。

接收端缺失范围必须来自实际校验的 checkpoint；完整且校验成功的已提交目标允许空缺失回复，身份或数据不匹配终止。transport 错误可重试，协议/认证/校验/存储错误不重试。显式 N 次和跨批次总 deadline 约束整个过程。上传身份稳定；下载保留既有 checkpoint 的 source binding，wire 发送身份可重新发现。逐文件清单重放有控制开销，换取结果丢失可安全对账；暂不保留跨 attempt 调度器学习状态。

必须以 loopback 故障注入和深圳/上海真实互传验收。测试通过仅支持当前配置/故障边界，不作吞吐提升推断。任务细节见 docs/tasks/2026-10-17-v3-work-unify-04.md。
