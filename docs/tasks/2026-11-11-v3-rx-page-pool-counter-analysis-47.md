# V3 接收队列 page-pool 计数分析47

日期：2026-11-11
输入提交：2df98f5d1f2ba093cc2d474ec0f27f8481b7e897
关联证据：Task46结果（2026-11-11-v3-profile-overhead-counter-correlation-46-result.md）
状态：DONE / 只复算 Task46，不发起传输。

## 任务范围

扩展只读网络分析器，使每个 RX 队列的 packet、page-pool allocation/refill、
WQE 与 buffer allocation 错误计数以 delta 形式单独呈现。用 Task46 已保存的
before/after 快照复算队列计数，并更新其结果和性能路线图。

## 工作假设

1. Task46 的 rx13/rx16 page-pool empty 与 slow 增量可由保存快照稳定复算。
2. 队列级 page-pool 计数能描述队列分配路径活动，但不会将接口聚合的
   rx_out_of_buffer 归属到某个队列或 TCP 流。
3. 明确暴露 buff_alloc_err、wqe_err 的零增量，有助于区分已观测的
   page-pool 慢路径与直接记录的队列错误；不能据此排除未暴露的其他驱动机制。

## 非目标

- 不发起 TCP/RDMA 传输，不生成新性能样本，不读取或写入 payload。
- 不修改 C++ 运行时、发送/接收路径、默认配置或协议。
- 不调整 NIC、ring、RSS、IRQ、RPS、coalescing、路由、swap 或内核参数。
- 不把 page-pool 计数与 rx_out_of_buffer 增量建立队列级或流级因果关系。

## 验收

- 分析器保留现有接口/主机范围字段，并添加可用 RX 队列计数的原始 delta。
- 单测覆盖 page-pool、packet、WQE/buffer 错误 delta，以及全局 rx_out_of_buffer
  不被错误映射至队列的边界。
- 使用 Task46 原始 before/after 快照离线复算；结果与已记录的全局 +237 和
  rx13/rx16 的 page-pool delta 一致。
- 相关 Python 测试、git diff --check 通过；不宣称性能收益或根因已定位。

## 受影响文件

- tools/perf/receive_network.py
- tools/test/run_receive_network_test.py
- docs/tasks/2026-11-11-v3-profile-overhead-counter-correlation-46-result.md
- docs/README.md
