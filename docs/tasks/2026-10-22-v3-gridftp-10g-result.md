# V3-GRIDFTP-10G-01：约 10 GiB 对照实验结果

日期：2026-10-22（Asia/Shanghai）  
代码分支：`codex/V3-PARALLEL-COMPRESSION-01`  
实验方向：R730 -> R750  
状态：完成，结果可复核

## 结论摘要

本批在 R730 与 R750 的直连数据接口上完成了同一批约 10.13 GiB 数据的
CPNetFlux V3 与 GridFTP 对照。两端接口均为 100000 Mbps 的 RDMA-capable
Ethernet，但本实验使用普通 TCP 数据路径，未使用 libibverbs/native RDMA，
也不代表跨域链路或阿里云性能。

- CPNetFlux V3：24 个正式样本（4 个数据集 × compression off/auto × 3 次），全部 `pass`。
- GridFTP：12 个正式样本（4 个数据集 × 3 次），全部 `pass`；GSI 控制认证、
  私密数据通道、远端 tree hash、文件数和总字节均核对一致。
- GridFTP 在本次链路和现有配置下的 wall time 明显低于 V3；这反映当前
  CPNetFlux V3 的控制/调度/文件级开销和实现状态，不能解释为协议本身的普遍结论。
- CPNetFlux `compression=auto` 的收益取决于内容：重复大文件线字节下降约
  99.03%，混合目录下降约 49.34%，dense 目录下降约 49.08%，随机大文件不压缩。

## 环境与认证

| 项目 | R730 | R750 |
|---|---|---|
| 管理别名/IP | `gridflux-lab-small` / `192.168.1.112` | `gridflux-lab-main` / `192.168.1.110` |
| 数据接口 | `enp130s0np0` / `192.168.100.1` | `enp177s0np0` / `192.168.100.2` |
| 数据路径 | 普通 TCP over RDMA-capable Ethernet | 普通 TCP over RDMA-capable Ethernet |

实验开始前在两端刷新了 GSI proxy，权限为 `600`，`grid-proxy-info -timeleft`
返回 `43200` 秒，身份为 `/O=XioTransfer/OU=SimpleCA/CN=Su`。GridFTP
服务只临时运行在 R750 数据地址的 TCP `32811` 端口，使用 GSI 控制认证、
`-encrypt-data` 私密数据通道和 8 路并行；没有修改正式服务、系统认证策略或防火墙。
初始控制 smoke 通过后才进入正式矩阵。实验结束时临时服务、监听端口、proxy、
服务根目录和 payload 均已清理。

## 数据集

所有数据由确定性生成器产生，并在传输前后用 tree hash、文件数和总字节校验。

| 数据集 | 文件数 | 逻辑字节 |
|---|---:|---:|
| `dense` | 2048 | 128 MiB |
| `large-random` | 1 | 4 GiB |
| `large-repeated` | 1 | 4 GiB |
| `mixed` | 514 | 2,051 MiB |
| 合计 | 2567 | 10,875,830,272 bytes（约 10.13 GiB） |

## 正式结果（每项为 3 次中位数）

吞吐按逻辑字节除 wall time 计算。GridFTP 的 `NIC tx` 是 R730 网卡
`tx_bytes` 增量，包含协议和链路开销；CPNetFlux 的 `wire` 是其应用帧统计，
两者的线字节口径不同，不能直接当作同一计数器。

| 数据集 | V3 off wall / 逻辑吞吐 | V3 auto wall / 逻辑吞吐 | GridFTP wall / 逻辑吞吐 | V3 off wire | V3 auto wire | GridFTP NIC tx |
|---|---:|---:|---:|---:|---:|---:|
| `dense` | 22.726 s / 0.047 Gbps | 25.382 s / 0.042 Gbps | 2.391 s / 0.449 Gbps | 135,050,026 | 68,772,650 | 138,476,874 |
| `large-random` | 22.826 s / 1.505 Gbps | 41.571 s / 0.827 Gbps | 6.305 s / 5.450 Gbps | 4,299,498,832 | 4,299,498,832 | 4,332,950,867 |
| `large-repeated` | 22.830 s / 1.505 Gbps | 22.426 s / 1.532 Gbps | 6.391 s / 5.376 Gbps | 4,299,498,832 | 41,610,576 | 4,332,993,893 |
| `mixed` | 13.501 s / 1.275 Gbps | 14.947 s / 1.152 Gbps | 4.998 s / 3.444 Gbps | 2,154,119,339 | 1,091,219,970 | 2,171,694,140 |

### 压缩消融观察

- `dense`：auto 相对 off wall time 增加约 11.68%，应用 wire 下降 49.08%。
- `large-random`：auto 相对 off wall time 增加约 82.14%，wire 不变；系统正确
  识别为不可压缩内容，不能把这类内容的耗时增加当作收益。
- `large-repeated`：auto wall time 比 off 低约 1.77%，wire 下降 99.03%。
- `mixed`：auto 相对 off wall time 增加约 10.70%，wire 下降 49.34%。

### V3 10 GiB 调度说明

4 GiB 文件被 range planner 切为 16 个约 256 MiB range。默认 1 ms profile
的有界 admission 预算会在首个大文件上拒绝合法 range；runner 增加了显式
`--scheduler-default-rtt-ms`，正式批使用 `160 ms`，将预留上限提高到约 4 GiB。
该值只影响有界调度器的预留，不改变实际网络 RTT，也不应被当成链路性能参数。
初始被拒绝的 1 ms 和 80 ms 运行保留为 blocked evidence，未纳入统计。

## 可复核证据

- CPNetFlux V3 正式 run：
  `/tmp/cpnetflux-runs/V3-PARALLEL-COMPRESSION-01/20261021T194504-be5cffbeee3b`
- V3 `results.jsonl`：24 个正式行另含 8 个 warmup 行；所有正式 `summary.result=pass`。
- GridFTP 结果：
  `/tmp/cpnetflux-runs/V3-GRIDFTP-10G-01/gridftp/results.jsonl`
- GridFTP 结果共 12 行，所有 `result=pass`、`hash_match=true`、`exit_code=0`，
  `auth_mode=gsi`、`data_channel_protection=private`、`parallelism=8`。
- CPNetFlux runtime commit：`4a51e6eddde916c8c9f9ec868514ab455539e5bd`。
- 实验 runner（含 160 ms 参数）：`03f0bc68ef1b5da3acee0c2d832ced0a4bf3325364566c11940d7142c776f664`。

## 限制与下一步

本结果只说明当前 R730/R750 直连 TCP 环境、当前 V3 runtime 和当前 GridFTP
配置下的可复现实测。它没有覆盖 native RDMA verbs、跨域网络、阿里云公网、
双向并发或 GridFTP 多种参数调优。若要定位 V3 与 GridFTP 的速度差距，下一轮
应先单独拆分 V3 的 control/scheduler/file-I/O 开销，再在相同并行度和相同
保护策略下复测；不能直接用本表宣称生产或跨域 readiness。
