# V3 S1 验收记录：CRC32C 与 DATA 前缀复制

- 日期：2026-10-30
- 阶段：S1（S0-S8 性能路线）
- 输入提交：d4da9dfa4c850a2b8b89a2e92b0c1edc5df2cda5
- 权威目录：深圳 /root/projects/CPNetFlux
- 构建：Release，GCC 11.4，TLS ON，io_uring OFF，ISA-L ON

## 本轮实现

1. CMake 固定拉取 Intel ISA-L v2.32.1（commit
   7c3479e0a9dac17f448603ec1ad64c7c625f530c），关闭 ISA-L 自带测试、性能测试、
   fuzz、shim 和 CLI，静态链接到 cpnetflux_core。x86 构建通过 ISA-L 的运行时
   dispatch 使用可用的 CRC32C 实现；未启用 ISA-L 时保留原 SSE4.2/软件路径。
2. FramedDataSocket 增加三段写入 API。明文 socket 用最多三个 iovec 的一次
   sendmsg；TLS 数据路径在 header+4 字节前缀适合时只在栈上合并 68 字节，再写
   原始 payload。现有两段 API 保持不变。
3. PersistentDataSession::writeData 不再创建“4 字节 CRC + 全量 payload”的
   临时 vector，直接发送 CRC 前缀和原始 payload。帧头的 payloadSize 仍包含前缀，
   接收端的校验和恢复语义未改。

## 行为验收

- 预期失败测试：在增加三段 API 测试后，旧实现因无六参数 writeFrame 而编译失败。
- ctest --test-dir /tmp/cpnetflux-s1-build --output-on-failure -R
  '^(ChecksumTest|PersistentSessionTest|FramedDataSocketTest)'：50/50 通过。
- 全量 ctest --test-dir /tmp/cpnetflux-s1-build --output-on-failure：
  470/472 通过；1 个 io_uring 环境条件 skip。失败的 2 项为
  cpnetflux_gridftp_control_token_smoke 和 cpnetflux_event_log_smoke，原因是
  环境未设置 CPNETFLUX_TEST_TOKEN，不是代码断言失败。
- FramedDataSocketTest：11/11 通过，覆盖三段线序、一次 sendmsg、跨 header/
  prefix/payload 的短写、EINTR、零进度、EAGAIN、非法 span 和真实回压。
- ChecksumTest：15/15 通过，软件与 ISA-L 硬件路径在空输入、边界长度、分段更新、
  大输入和组合校验上结果一致。
- PersistentSessionTest.CarriesChecksumAlgorithmAndFileEndDigest：通过，并直接
  检查 DATA 线上 4 字节 CRC 前缀和原始 payload 字节。

## CRC 单项 A/B

命令：

cpnetflux-checksum-bench --backend <software|hardware> --bytes 268435456
--iterations 1 --buffer-size 65536

同一深圳 2 vCPU、同一 Release 构建参数下各运行 3 次：

| 路径 | wall 秒（3 次） | CRC 结果 |
|---|---:|---:|
| ISA-L hardware | 0.02160，0.01173，0.00618 | 488492415 |
| ISA-L OFF 的 SSE4.2 hardware | 0.02973，0.03326，0.02837 | 488492415 |
| software table | 0.98326，0.88530，0.89586 | 488492415 |

ISA-L 与旧 SSE4.2 硬件路径的中位耗时约从 0.02973 秒降到 0.01173 秒；这是
CRC 单项结果，不是完整 CPNetFlux 传输吞吐或 CPU 总成本结论。它超过 S1 的
“低于 5% 则 no-go”筛选线，但仍需后续实验包确认端到端收益。

## 边界与下一步

- 本轮没有改传输单元大小、锁模型、checkpoint 策略、TLS 模式、kTLS、零拷贝、
  物理链路或跨服务器性能实验。
- ISA-L 构建依赖 NASM 2.15.05；深圳和上海均已安装同版本，之后按同一提交构建
  并比较关键二进制 SHA-256。
- 下一阶段进入 S2：只改变传输单元/提交批量，先用 S0 观测确认帧数、写入和 TLS
  计时变化；S1 的 CRC 单项结果不能单独证明 S2 或 S3 的收益。
