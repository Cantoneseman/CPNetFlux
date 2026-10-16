# UI-STATE-PRESENTATION-02

## 范围
- 在桌面端把真实 agent 的扫描、准备、传输、校验和提交状态映射为可理解的阶段卡与时间线。
- 将 attempt/file/range、调度、回退、阶段计时等保留证据收纳到可展开的技术详情。
- 对 agent 未提供的字段显示明确的“agent 未提供”，不以 0、假百分比或推算速度替代。
- 补充 UI source contract，覆盖阶段文案、技术详情折叠和真实证据提示。

## 非目标
- 不修改传输协议、schema、core、TLS、认证、agent 或桌面端外部功能。
- 不启动跨域、长时间或性能实验；只做本地回归、loopback/UI contract 和打包门禁。

## 输入与依赖
- 输入提交：`293be7085615a87b71405f0088726ca026c8c4d6`。
- 依赖：现有 `transfer.get` 快照、任务状态、metrics、backend_evidence、recovery、action 状态；真实 agent 未提供的 manifest/file 分页仍保持不可用提示。

## 验收标准
1. scanning/queued/connecting/negotiating/transferring/committing/completed/failed/cancelled/recoverable 在主视图有清晰中文状态与下一步说明。
2. 扫描阶段显示“正在扫描目录”和 agent 返回的已发现/总文件数（缺失则明确未提供），不伪造百分比或速度。
3. 主视图显示文件/字节进度、完整性摘要、错误和恢复提示；技术字段可展开查看。
4. source contract、agent、Qt RPC、Qt offscreen、CTest 与 DEB 打包通过。

## 回滚
- 回滚本阶段提交即可恢复上一阶段的 QML 与契约测试；不影响 agent 状态目录或传输任务。

## 交接
- 完成后记录提交 SHA、远端 SHA 回读、测试命令/结果、DEB 路径与 SHA256，以及后端仍未提供的证据能力。