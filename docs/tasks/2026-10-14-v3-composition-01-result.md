# V3-COMPOSITION-01 结果：安全持久会话组合回归

日期：2026-10-14
输入提交：5d676dccc98e9ae608c63e47a2e41181e7439e83
权威源码：/root/projects/CPNetFlux
任务范围：docs/tasks/2026-10-14-v3-composition-01.md

## RED

新增 loopback 组合用例首次在输入版本失败。控制面使用 TLS server name localhost，但持久 data session 建立数据 TLS 时把连接地址 127.0.0.1 当作证书身份，导致首个数据 payload 前握手失败。任务返回失败，未提交半成品，证明回归覆盖了真实连接边界。

## 修复

connectFramedDataSocket 增加可选的 TLS peer name；tree client 建立持久数据连接时沿用控制面 tlsServerName。未配置时仍使用连接 host，保持既有调用兼容。没有改变 wire framing、认证策略、range planner 或默认模式。

## GREEN 证据

- python3 tools/test/run_v3_stability_matrix.py --build-dir /tmp/cpnetflux-delivery-build --rounds 2
  - persistent TLS composition：CRC32C、data TLS required、动态 range、双向传输、上传/下载 committed resume 通过。
  - 128x1MiB 与 mixed 256MiB 目录 hash/文件数/字节数通过。
  - 控制 TLS RST、握手 RST、data TLS RST 各 2 轮，服务保持可重连。
- 定向 CTest：8/8 通过，包含 data TLS smoke、V3 stability、data-session reuse、server setup 和 4 项桌面门禁。
- 完整 CTest：CPNETFLUX_TEST_TOKEN 临时注入后运行 ctest --test-dir /tmp/cpnetflux-delivery-build --output-on-failure
  - 298/298 通过。
  - FileIoTest.IoUringContextReadWriteSmokeWhenAvailable 因当前环境没有 io_uring 能力而 skip。
- 构建：cmake --build /tmp/cpnetflux-delivery-build -j2 通过。
- 本地 loopback 只证明协议组合、事务身份和错误回收，不是跨域稳定性或性能结论。

## 未完成项

global scheduler、control pipeline 与 persistent data worker 的统一 lease/取消/断线回收仍是后续 V3-WORKER-STATE-01；当前不把未测试组合报告为支持。尚未启动深圳—上海或 GridFTP 性能实验。

## 回滚

对本阶段提交执行 git revert <commit>；保留此前 V3 事务引擎、服务器初始化和桌面节点导入提交。不删除用户配置、凭据或测试证据。
