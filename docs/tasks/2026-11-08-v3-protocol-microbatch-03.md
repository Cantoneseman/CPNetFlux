# V3 Protocol Microbatch 03：固定 pending window 评估

日期：2026-11-08

## 本轮任务

在 manifest/coalescing 两个候选均未通过后，验证现有 V3 持久数据协议的固定微批边界
data-pending-window。窗口表示同一 TCP 数据连接上已经发送、但尚未读到
FILE_RESULT 的文件数；它保留每个成员独立的 FILE_BEGIN/FILE_END/FILE_RESULT、
checksum、原子发布、恢复和工作租约语义，不拼接文件 payload，也不改变线格式。

本轮先做固定窗口 1/2/4/8 的同提交对照，不把更大的窗口或自动策略直接设为默认。

## 非目标

- 不改 RDMA CQ/RQ/QP、系统网络参数或服务端生产进程。
- 不改 V3 文件帧格式、PersistentDataSession 的单文件接收状态机、checksum、
  checkpoint、原子发布和恢复语义。
- 不扩大三阶段队列、线程池或内存预算。
- 不把小文件结果解释为大文件 100 Gbps readiness。
- 不使用 64 GiB payload；实验用 4 KiB/16 KiB 小文件和受控 tmpfs，完成后清理。

## 验收标准

1. R770-1/R770-2 固定同一提交，三项二进制 SHA-256 一致。
2. 相关 CTest 和 pending-window smoke 通过；每个窗口的摘要记录实际窗口和
   data_pending_high_watermark <= window。
3. 4 KiB×10,000、16 KiB×10,000 作为快速筛选；候选窗口与基线窗口 1 交错重复。
4. 若筛选有信号，再对 4 KiB×100,000、16 KiB×100,000 做 5 对交错样本。每个样本
   必须 rc=0、源/目标树成员数/字节数/SHA-256 一致、内存门通过、临时树立即删除。
5. 晋级要求：两种尺寸的 T_ready 中位数均下降至少 20%，配对 p95 不恶化超过 10%，
   至少 4/5 配对改善；否则固定窗口微批候选 no-go，默认和后续路线不改变。

## 输入与输出证据

代码输入提交：a42e6bf0f2b1877218f78a5fb4cf8fad23d9c5fc。
实验根目录：/home/Su/experiments/CPNetFlux/2026-11-08-v3-protocol-microbatch-03。
只保留 compact-summary、日志、事件和分析结果；删除每样本大 JSON 与所有临时树。

## 当前判断

现有实现已经提供固定窗口和观测字段，因此本轮先验证真实收益；只有验收门通过，
才新增自动选择或默认调整。若不通过，关闭该微批线路，转入稳定测量窗口和 raw TCP /
V3 帧保留路径的服务率归因。

## 结果（2026-11-08）

固定窗口的 4 KiB/16 KiB × 10,000 快速筛选和 100,000 交错复测均通过完整性门。100,000 规模的 5 对交错样本结果如下：

| 文件大小 | window=1 T_ready 中位数 | window=8 T_ready 中位数 | 中位降幅 | p95 变化 | 改善配对 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4 KiB | 32.5537 s | 19.7689 s | 39.3% | 下降 | 5/5 |
| 16 KiB | 36.0170 s | 21.1992 s | 41.1% | 下降 | 5/5 |

所有样本返回码为 0，源/目标树成员数、逻辑字节数和 SHA-256 一致；实际
`data_pending_high_watermark` 分别为 1 或 8，owned client VmSwap 为零，实验临时树
已清理。原始逐文件 timing 曾造成证据膨胀，已压缩为 compact-summary/compact-raw，
没有保留大 payload。

## 工程实现

新增显式的 `--data-pending-window auto`。它不改变数值窗口和默认值：当 manifest
至少有 10,000 个文件且平均文件大小不超过 64 KiB 时选择窗口 8，否则选择安全默认
窗口 2。JSON 摘要增加 `data_pending_window_policy`，并报告实际选择的窗口；窗口
8 也纳入 pending-window smoke 的默认覆盖。协议帧、恢复、checksum、原子发布和
服务端生产进程均未改变。

该候选通过本轮晋级门，但只适用于密集小文件的 V3 TCP 路径；不能外推为大文件或
100 Gbps readiness。`auto` 尚未替换全局默认，调用方必须显式选择它。

## 验证与下一步

R770-1 先构建并运行单元测试、tree 相关 CTest 和 pending-window smoke；固定提交
同步到 R770-2 后重复构建、测试并核对三项二进制 SHA-256。下一步进入稳定的 raw
TCP/V3 帧服务率归因，评估 `auto` 在真实目录分布中的收益；不继续扩大三阶段队列、
writer 或 RDMA 路线。