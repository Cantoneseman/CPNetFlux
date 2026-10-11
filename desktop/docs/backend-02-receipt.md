# AGENT-BACKEND-02 交接回执

## 范围

本阶段基于已冻结的 desktop-agent API 2.0.0，实现 Qt UI 对版本握手、断线 stale 状态、订阅 watermark 以及 attempt/file/range 分页的消费。详情页显示 snapshot_id、revision、agent_epoch、snapshot_sequence，拒绝旧 attempt/generation 或不一致 watermark 的页面。cursor_expired 会提示刷新。

同步输入：codex/AGENT-API-01 提交 344c326f5584482a8eeb586c5a24ef1d2f5d38ea；本分支原始基线：66c66e42c9c1aa8a10334d271fec3ddbb11ae80f。未修改核心传输源码、真实 adapter、凭据入口或安装器。

## 验收命令

python3 -B desktop/tests/test_contract.py
python3 -B desktop/tests/test_agent.py
cmake --build desktop/build --parallel 2
ctest --test-dir desktop/build --output-on-failure

结果：contract 8/8、agent 7/7、CTest 3/3（desktop-contract、desktop-agent、desktop-offscreen）。UI 测试覆盖七页离屏加载、UI 退出不停止 agent，以及不兼容 api_version 只允许 agent.getInfo。

## 边界

当前仍为 demo adapter。effective policy、TLS、checksum、mode、resume 和文件/range 内容都是演示状态，UI 持续标注“演示”；未知字段保留未知。真实 transfer、节点探测和目录浏览仍显式 unsupported。凭据只允许 credential_ref，没有 token/password/private key 进入 argv、环境、RPC、日志或临时明文文件。

## 下一步

需要 00/底层提供结构化 transfer summary/event、真实 per-mode capability/effective-policy 回读、取消 quiescence/checkpoint 证据和安全 secret-store 注入后，才能进入受控真实 adapter 接入。

阶段提交：51a781a1f82f9c1fa698758abb1edc46020116c。push 与远端 SHA 回读由本阶段结束命令记录。
