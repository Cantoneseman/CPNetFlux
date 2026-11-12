# 阶段67：有界TCP源端口计划实现与实际映射核验

实现提交839352cd8273c8badf52d4b2da58fa784c4780c8已推送GitHub并回读SHA。R770-1单人串行；R770-2同提交、同配置构建，四个二进制SHA-256一致。候选默认关闭，仅显式--tcp-data-source-port-plan启用；不改默认吞吐配置、网络、pacing或RDMA。

## 已验证能力

计划限定为单link、数字IPv4、TCP下载，控制连接不使用计划；与正常TLS能力兼容。任务启动一次加载只读计划，按(channel,实际被动端口)选择候选。只有EADDRINUSE换下一端口，最多128次；缺失行、耗尽、错误地址或deadline明确失败，不回退临时端口，不使用REUSEADDR/REUSEPORT绕过TIME_WAIT。JSON保留实际四元组、计划目标队列、bind尝试次数和快照来源SHA；快照SHA不是实时网卡校验。

完整512被动端口窗口生成2048行、262144候选，1594497字节，低于2MiB上限。离线生成及独立逐位算法核验64.72秒，所有候选预测一致。这是一次离线规划成本，尚未摊销为性能结果；后续完整任务必须计入必要规划、配置核验成本。当前RSS symmetric-or-xor/Toeplitz、256项表、tcp4四元组快照与阶段65一致；不支持模式明确拒绝。源端口按channel分池。

R1/R2最终相关CTest各61/61通过，无跳过。覆盖真实TIME_WAIT占用跳过、耗尽、deadline、无降级、TLS、重复任务、完整内容及中途data/control恢复。但后续原512B早期边界定向复核暴露额外失败，不能据此宣布完整恢复闭环。

## 唯一跨机功能样本

双端固定上述实现后，只运行一次1MiB tmpfs文件下载，bind192.168.100.1/.2，TCP数据面、4channel、CRC32C/V3，未运行1GiB或8GiB性能矩阵。源与新目标SHA-256均fbbab289f7f94b25736c58be46a994c441fd02552cc6022352e3d86d2fab7c83。

|channel|接收源端口|发送被动端口|计划/预测队列|最后接收CPU/NAPI|
|---|---:|---:|---:|---|
|0|20035|61201|9|9 / 8202|
|1|30278|61200|27|27 / 8220|
|2|40531|61202|7|7 / 8200|
|3|50754|61203|28|28 / 8221|

四路均命中计划候选及阶段65独立probe观测。所有接收队列上的最后包观测匹配；单文件只需一个chunk，此样本不证明4路各自持续传输了大负载，不是吞吐或队列容量试验。观察窗两端OOB/missed及其他门禁错误增量0、host pswpout增量0，owned swap0。原2121服务PID/startticks保留、隔离49367服务和owned tmpfs源/目标已清理。

原跨机runner退出1：我把四队列全集断言放在for循环内，在第一路就报失败。原脚本、失败report和raw summary原样保留；只修正该断言缩进，verify_small67.py用同一次实际summary、两端快照、清理前源/目标SHA及已验证CPU/NAPI离线复核通过。没有重传、覆盖原失败或把runner原始状态改成pass。

## 恢复门的真实失败与后续

早期ctest-final-r1.log的一次10秒total deadline失败曾未复现。保留512B注入和2MiB+约2.9KiB混合文件，最多三对串行检查：early-boundary-review在第二对data reset出现persistent file begin identity mismatch，停止；追加协议trace三对在第三对control reset复现total deadline。全部case日志/代理事件/summary保留，payload已hash登记并删除，私钥已删除。中途512KiB场景通过不能替代这些早期边界。

因此阶段67状态为 **CANDIDATE IMPLEMENTED / MAPPING FUNCTIONALLY VERIFIED / EARLY RECOVERY FAIL**。阶段68定向修复工作租约迁移的resume/attempt语义；本报告冻结修复前事实。正式GridFTP比较仍阶段64 NO-GO（仅1/5完整有效配对），稳定性NOT_RUN，上机NOT_READY；没有新增合格Gbps，不宣布交付C整体完成。

证据目录/home/Su/experiments/CPNetFlux/2026-11-12-tcp-source-plan-67；冻结清单同名freeze.json，离线核验python3 verify67.py。两端版本/配置/四hash见pinned-receipt-r*.json；单次实际样本见header-summary、两端header-*-report/cleanup、small-offline-result；所有原始失败保留。下一步在阶段68修复、回归、提交同步后，另立有界完整文件网络验收合同；通过后才能恢复正式GridFTP、稳定性与现场准备。
