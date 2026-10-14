# V3-COMPOSITION-02 结果：持久 V3 上传压缩组合

日期：2026-10-14
输入提交：31ed679bde7a128ec2d15d791a9c26750d86f782
代码提交：66277c736d52448903b710b3218934c48620909a
权威源码：/root/projects/CPNetFlux

## RED

新增回归先在输入版本运行，持久 tree 路径拒绝 --compression auto，确认现状缺少该组合。实现后回归要求 2 MiB 高可压缩文件实际使用压缩帧，确定性伪随机文件走 raw fallback，并核验逐文件和任务级计数。

## GREEN

- cmake --build /tmp/cpnetflux-delivery-build -j2：通过。
- python3 tools/test/run_v3_stability_matrix.py --build-dir /tmp/cpnetflux-delivery-build --rounds 2：通过。persistent TLS required、CRC32C、动态 range、混合压缩/raw 上传、上传/下载恢复、dense 128x1MiB 与 mixed 256MiB hash，以及 RST 重连用例均通过。
- CPNETFLUX_TEST_TOKEN=V3CompositionTemporaryToken20261014 ctest --test-dir /tmp/cpnetflux-delivery-build --output-on-failure：298/298 通过；io_uring 可选用例因环境能力不可用而 skip。
- CPack DEB：/tmp/cpnetflux-delivery-build/cpnetflux_0.1.0+git.66277c736d52_amd64.deb。包内 source_revision 为 66277c736d52448903b710b3218934c48620909a，TLS=1、desktop=ON、public_release_ready=false；Qt/QML 运行依赖核对通过。SHA256：dcbf40ec42d93ec7b29e2aaa257fb75beb433ca1959c3424165a6c1edd991574。
- 代码已推送至 origin/main；git ls-remote origin refs/heads/main 回读为 66277c736d52448903b710b3218934c48620909a。

本阶段仅启用持久 V3 上传的自动压缩收益判断，并将策略、实际帧决策和回退计数写入 JSON 与文件时序。协议帧、默认策略、服务端下载压缩协商和桌面端未变。Loopback 结果不构成跨域性能或稳定性证据。

## 未完成

统一 scheduler/pipeline 与持久 worker 的 lease、控制/数据连接生命周期、取消/超时/断线回收和能力协商仍未完成，留给 V3-WORKER-STATE-01。整体发布就绪状态仍为 false；该包是内部候选包，不应作为稳定版交付。

## 回滚

代码回滚使用 git revert 66277c736d52448903b710b3218934c48620909a；验收记录保留。该提交不影响此前事务引擎、安全会话和桌面节点导入。
