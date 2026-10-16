# AGENT-LIVE-PROGRESS-03

## 范围

为真实目录传输补齐运行期间的可观察进度：CPNetFlux core 在调度运行期间周期性写入 `tree_progress` event，agent 增量消费 event-log，持续发布文件数、字节数、速度和 ETA；对本地上传目录在启动阶段完成一次受控扫描，为进度条提供真实总量。桌面端把尚未到达的阶段显示为用户可理解的“待处理/等待后端确认”，不把内部字段名或“agent 未提供”作为主要状态。

## 非目标

- 不改变 TLS、token、认证、传输协议、恢复语义或调度策略。
- 不启用性能实验，不修改跨域拓扑，不增加新的桌面端依赖。
- 不把未知总量推算成百分比；下载端在远端清单尚未返回时保持“正在获取清单”。

## 验收标准

1. 真实 agent 在 core 进程仍运行时读取 event-log，并至少每个文件完成事件发布一次状态；若 core 提供 `tree_progress` 事件，发布实时字节进度、速度和 ETA。
2. 上传任务在扫描结果可得后填充 files_total/bytes_total，完成前不直接跳到终态。
3. 真实模式回归测试验证运行中状态和指标会变化，最终仍以摘要、checksum 和 finalize 证据判定完成。
4. 相关 Python/JSON/QML 门禁及 CMake/CTest 通过，修改不包含凭据、构建产物或 payload。

## 允许修改路径

- `desktop/agent/cpnetflux_agent.py`
- `desktop/schemas/desktop-agent.schema.json`
- `desktop/tests/`
- `desktop/ui/Main.qml`
- `desktop/docs/`
- `src/core/io/tree_transfer_client.cpp`

## 回滚

回滚本任务提交即可恢复旧的状态呈现；不回退已有任务状态文件或远端传输数据。


## 追加修复：核心进度与产品引擎显示

- 持久目录上传核心通过可选 progress callback 将成功写入的逻辑字节增量汇总到 TreeRunStats；TreeProgressEmitter 每 250ms 写入 tree_progress 事件，agent 读取并计算速度/ETA。
- 桌面端将实际 dynamic_ring/range_stripe 调度显示为“CPNetFlux V3”，V2 仅作为协议兼容数据面说明；技术字段移入可展开区域，逐文件分页未接入时不再显示“agent 未提供”作为用户主状态。
- 新增允许路径：include/cpnetflux/config/file_transfer_options.h、src/core/io/persistent_tree_transfer.cpp、desktop/ui/Main.qml、desktop/agent/cpnetflux_agent.py、desktop/tests/test_ui_source_contract.py。


## 验证记录

- Desktop contract/agent/UI source tests：全部通过；Qt CTest 5/5 通过；DEB 已重新生成。
- Core 定向持久目录、动态队列、range、事件日志测试：46/46 通过；全量 CTest 291 项中 289 通过、1 项因 io_uring 环境跳过、2 项 token smoke 因未设置 CPNETFLUX_TEST_TOKEN 未运行通过，非本次代码失败。
- 深圳演示机真实上传：512 MiB、65 文件，事件日志 179 行，其中 177 条 tree_progress；最终文件 65/65、CRC32C pass、finalize pass。
