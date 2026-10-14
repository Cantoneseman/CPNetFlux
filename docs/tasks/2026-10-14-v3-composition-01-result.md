# V3-COMPOSITION-01 结果：安全持久会话组合回归

日期：2026-10-14
输入提交：5d676dccc98e9ae608c63e47a2e41181e7439e83
权威源码：/root/projects/CPNetFlux
任务范围：docs/tasks/2026-10-14-v3-composition-01.md

## RED

首次 RED：在 5d676dc 输入版本，loopback 组合用例于首个 data payload 前因持久数据 TLS 使用 127.0.0.1 而非证书 server name localhost 握手失败。TLS 修复提交 8d3b122 后，新增的恢复断言又发现下载汇总把完成文件总大小误报成本次传输字节数；range 时序表明已完成数据实际没有重传。

## 修复

connectFramedDataSocket 增加可选 TLS peer name，tree client 沿用 tlsServerName。持久下载恢复现在先校验远端 size/mtime 并复用原 tree manifest 条目；恢复控制会话以持久 worker 的单连接参数协商并由首个 worker 复用。bytes_transferred 改为只计本次实际 payload。源文件元数据变化时在数据连接开始前拒绝恢复。wire framing、认证策略和 range planner 未改变。

## GREEN 证据

- python3 tools/test/run_v3_stability_matrix.py --build-dir /tmp/cpnetflux-delivery-build --rounds 2
  - persistent TLS composition：CRC32C、data TLS required、动态 range、双向传输、提交 manifest、上传和下载 resume 实际 payload 为 0 通过。
  - 128x1MiB 与 mixed 256MiB 目录 hash/文件数/字节数通过。
  - RST 重连 2 轮通过；改变远端文件 size 后，恢复被拒且 bytes_transferred=0。
- 完整 CTest：CPNETFLUX_TEST_TOKEN 临时注入后运行 ctest --test-dir /tmp/cpnetflux-delivery-build --output-on-failure
  - 298/298 通过。
  - FileIoTest.IoUringContextReadWriteSmokeWhenAvailable 因当前环境没有 io_uring 能力而 skip。
- 构建：cmake --build /tmp/cpnetflux-delivery-build -j2 通过。
- CPack：以 code revision 688ce60 构建 DEB：cpnetflux_0.1.0+git.688ce60c7bed_amd64.deb；dpkg-deb 元数据、build-info revision、TLS/桌面标识及 Qt/QML 依赖核对通过。包位于 /tmp/cpnetflux-runs/V3-COMPOSITION-01/，没有进入 Git。
- 该 DEB 是内部候选包，build-info 明确标记 public_release_ready=false；不代表后续未覆盖的 scheduler/pipeline/persistent worker 组合已稳定。
- 本地 loopback 只证明协议组合、事务身份和错误回收，不是跨域稳定性或性能结论。

## 未完成项

global scheduler、control pipeline 与 persistent data worker 的统一 lease/取消/断线回收仍是后续 V3-WORKER-STATE-01；当前不把未测试组合报告为支持。尚未启动深圳—上海或 GridFTP 性能实验。

## 回滚

对本阶段提交执行 git revert <commit>；保留此前 V3 事务引擎、服务器初始化和桌面节点导入提交。不删除用户配置、凭据或测试证据。
